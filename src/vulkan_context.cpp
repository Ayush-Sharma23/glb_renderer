#include "vulkan_context.hpp"

#include <cstdio>
#include <cstdlib>

#define VK_CHECK(x) do { \
    VkResult err = x; \
    if (err != VK_SUCCESS) { \
        fprintf(stderr, "Vulkan error @ %s:%d: %d\n", __FILE__, __LINE__, (int)err); \
        abort(); \
    } \
} while(0)

static VKAPI_ATTR VkBool32 VKAPI_CALL debug_callback(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT,
    const VkDebugUtilsMessengerCallbackDataEXT* data,
    void*)
{
    if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
        fprintf(stderr, "VK_ERROR: %s\n", data->pMessage);
    else if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
        fprintf(stderr, "VK_WARN:  %s\n", data->pMessage);
    return VK_FALSE;
}

bool VulkanContext::init() {
    if (!glfwInit()) {
        fprintf(stderr, "Failed to initialize GLFW\n");
        return false;
    }
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    window_ = glfwCreateWindow(1600, 900, "GLB Renderer", nullptr, nullptr);
    if (!window_) {
        fprintf(stderr, "Failed to create GLFW window\n");
        return false;
    }

    if (!create_instance()) return false;
    if (!create_device()) return false;
    if (!create_allocator()) return false;
    if (!create_swapchain()) return false;
    if (!create_render_pass()) return false;
    if (!create_depth_image()) return false;
    if (!create_framebuffers()) return false;
    if (!create_command_pools()) return false;
    if (!create_sync_objects()) return false;

    last_time_ = (float)glfwGetTime();
    return true;
}

void VulkanContext::cleanup() {
    vkDeviceWaitIdle(device_);

    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        if (frames_[i].command_pool)
            vkDestroyCommandPool(device_, frames_[i].command_pool, nullptr);
        if (frames_[i].render_fence)
            vkDestroyFence(device_, frames_[i].render_fence, nullptr);
        if (frames_[i].swapchain_semaphore)
            vkDestroySemaphore(device_, frames_[i].swapchain_semaphore, nullptr);
        if (frames_[i].render_semaphore)
            vkDestroySemaphore(device_, frames_[i].render_semaphore, nullptr);
    }

    if (single_time_pool_)
        vkDestroyCommandPool(device_, single_time_pool_, nullptr);

    if (depth_image_view_) vkDestroyImageView(device_, depth_image_view_, nullptr);
    if (depth_image_) vmaDestroyImage(allocator_, depth_image_, depth_allocation_);

    for (auto fb : swapchain_.framebuffers)
        vkDestroyFramebuffer(device_, fb, nullptr);
    for (auto iv : swapchain_.image_views)
        vkDestroyImageView(device_, iv, nullptr);

    if (render_pass_) vkDestroyRenderPass(device_, render_pass_, nullptr);
    if (swapchain_.swapchain) vkDestroySwapchainKHR(device_, swapchain_.swapchain, nullptr);
    if (allocator_) vmaDestroyAllocator(allocator_);
    if (surface_) vkDestroySurfaceKHR(instance_, surface_, nullptr);
    if (device_) vkDestroyDevice(device_, nullptr);
    if (debug_messenger_) {
        auto func = (PFN_vkDestroyDebugUtilsMessengerEXT)
            vkGetInstanceProcAddr(instance_, "vkDestroyDebugUtilsMessengerEXT");
        if (func) func(instance_, debug_messenger_, nullptr);
    }
    if (instance_) vkDestroyInstance(instance_, nullptr);

    glfwDestroyWindow(window_);
    glfwTerminate();
}

bool VulkanContext::create_instance() {
    auto builder = vkb::InstanceBuilder()
        .set_app_name("GLB Renderer")
        .set_engine_name("glb_renderer")
        .set_app_version(1, 0, 0)
        .require_api_version(1, 3, 0)
        .use_default_debug_messenger()
        .set_debug_callback(debug_callback);

    auto result = builder.build();
    if (!result) {
        fprintf(stderr, "Failed to create Vulkan instance: %s\n", result.error().message().c_str());
        return false;
    }

    vkb_instance_ = result.value();
    instance_ = vkb_instance_.instance;
    debug_messenger_ = vkb_instance_.debug_messenger;

    if (glfwCreateWindowSurface(instance_, window_, nullptr, &surface_) != VK_SUCCESS) {
        fprintf(stderr, "Failed to create window surface\n");
        return false;
    }
    return true;
}

bool VulkanContext::create_device() {
    VkPhysicalDeviceVulkan13Features features13{};
    features13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    features13.dynamicRendering = VK_TRUE;
    features13.synchronization2 = VK_TRUE;

    VkPhysicalDeviceVulkan12Features features12{};
    features12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
    features12.descriptorIndexing = VK_TRUE;
    features12.bufferDeviceAddress = VK_TRUE;
    features12.scalarBlockLayout = VK_TRUE;

    vkb::PhysicalDeviceSelector selector{vkb_instance_, surface_};
    auto phys_ret = selector
        .set_surface(surface_)
        .set_minimum_version(1, 3)
        .set_required_features_13(features13)
        .set_required_features_12(features12)
        .prefer_gpu_device_type(vkb::PreferredDeviceType::discrete)
        .select();

    if (!phys_ret) {
        fprintf(stderr, "Failed to select physical device: %s\n", phys_ret.error().message().c_str());
        return false;
    }

    auto phys_device = phys_ret.value();
    physical_device_ = phys_device.physical_device;

    vkb::DeviceBuilder device_builder{phys_device};
    auto dev_ret = device_builder.build();
    if (!dev_ret) {
        fprintf(stderr, "Failed to create logical device: %s\n", dev_ret.error().message().c_str());
        return false;
    }

    auto vkb_device = dev_ret.value();
    device_ = vkb_device.device;

    auto gq = vkb_device.get_queue(vkb::QueueType::graphics);
    if (!gq) { fprintf(stderr, "No graphics queue\n"); return false; }
    graphics_queue_ = gq.value();

    auto pq = vkb_device.get_queue(vkb::QueueType::present);
    if (!pq) { fprintf(stderr, "No present queue\n"); return false; }
    present_queue_ = pq.value();

    auto tq = vkb_device.get_queue(vkb::QueueType::transfer);
    if (tq) {
        transfer_queue_ = tq.value();
    } else {
        transfer_queue_ = graphics_queue_;
    }

    auto gqi = vkb_device.get_queue_index(vkb::QueueType::graphics);
    if (gqi) graphics_queue_family_ = gqi.value();

    auto tqi = vkb_device.get_queue_index(vkb::QueueType::transfer);
    if (tqi) {
        transfer_queue_family_ = tqi.value();
    } else {
        transfer_queue_family_ = graphics_queue_family_;
    }

    printf("Vulkan device: %s\n", phys_device.properties.deviceName);
    printf("Graphics queue family: %u\n", graphics_queue_family_);
    printf("Transfer queue family: %u\n", transfer_queue_family_);
    return true;
}

bool VulkanContext::create_allocator() {
    VmaAllocatorCreateInfo alloc_info{};
    alloc_info.vulkanApiVersion = VK_API_VERSION_1_3;
    alloc_info.physicalDevice = physical_device_;
    alloc_info.device = device_;
    alloc_info.instance = instance_;
    alloc_info.flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;

    if (vmaCreateAllocator(&alloc_info, &allocator_) != VK_SUCCESS) {
        fprintf(stderr, "Failed to create VMA allocator\n");
        return false;
    }
    return true;
}

bool VulkanContext::create_swapchain() {
    vkb::SwapchainBuilder swap_builder{physical_device_, device_, surface_};
    auto swap_ret = swap_builder
        .set_desired_format({VK_FORMAT_B8G8R8A8_SRGB, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR})
        .set_desired_present_mode(VK_PRESENT_MODE_MAILBOX_KHR)
        .set_desired_extent(1600, 900)
        .set_image_usage_flags(VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT)
        .build();

    if (!swap_ret) {
        fprintf(stderr, "Failed to create swapchain: %s\n", swap_ret.error().message().c_str());
        return false;
    }

    auto vkb_swap = swap_ret.value();
    swapchain_.swapchain = vkb_swap.swapchain;
    swapchain_.format = vkb_swap.image_format;
    swapchain_.extent = vkb_swap.extent;
    swapchain_.images = vkb_swap.get_images().value();
    swapchain_.image_views = vkb_swap.get_image_views().value();

    printf("Swapchain: %dx%d, format=%d, images=%zu\n",
        swapchain_.extent.width, swapchain_.extent.height,
        swapchain_.format, swapchain_.images.size());
    return true;
}

bool VulkanContext::create_render_pass() {
    VkAttachmentDescription color_attachment{};
    color_attachment.format = swapchain_.format;
    color_attachment.samples = VK_SAMPLE_COUNT_1_BIT;
    color_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    color_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    color_attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    color_attachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentDescription depth_attachment{};
    depth_attachment.format = VK_FORMAT_D32_SFLOAT;
    depth_attachment.samples = VK_SAMPLE_COUNT_1_BIT;
    depth_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depth_attachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depth_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depth_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depth_attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depth_attachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference color_ref{};
    color_ref.attachment = 0;
    color_ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentReference depth_ref{};
    depth_ref.attachment = 1;
    depth_ref.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &color_ref;
    subpass.pDepthStencilAttachment = &depth_ref;

    VkSubpassDependency dep{};
    dep.srcSubpass = VK_SUBPASS_EXTERNAL;
    dep.dstSubpass = 0;
    dep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep.srcAccessMask = 0;
    dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    VkAttachmentDescription attachments[2] = {color_attachment, depth_attachment};

    VkRenderPassCreateInfo rp_info{};
    rp_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    rp_info.attachmentCount = 2;
    rp_info.pAttachments = attachments;
    rp_info.subpassCount = 1;
    rp_info.pSubpasses = &subpass;
    rp_info.dependencyCount = 1;
    rp_info.pDependencies = &dep;

    if (vkCreateRenderPass(device_, &rp_info, nullptr, &render_pass_) != VK_SUCCESS) {
        fprintf(stderr, "Failed to create render pass\n");
        return false;
    }
    return true;
}

bool VulkanContext::create_depth_image() {
    VkExtent3D extent{swapchain_.extent.width, swapchain_.extent.height, 1};

    VkImageCreateInfo img_info{};
    img_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    img_info.imageType = VK_IMAGE_TYPE_2D;
    img_info.format = VK_FORMAT_D32_SFLOAT;
    img_info.extent = extent;
    img_info.mipLevels = 1;
    img_info.arrayLayers = 1;
    img_info.samples = VK_SAMPLE_COUNT_1_BIT;
    img_info.tiling = VK_IMAGE_TILING_OPTIMAL;
    img_info.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    img_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VmaAllocationCreateInfo alloc_info{};
    alloc_info.usage = VMA_MEMORY_USAGE_GPU_ONLY;
    alloc_info.requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

    if (vmaCreateImage(allocator_, &img_info, &alloc_info, &depth_image_, &depth_allocation_, nullptr) != VK_SUCCESS) {
        fprintf(stderr, "Failed to create depth image\n");
        return false;
    }

    VkImageViewCreateInfo iv_info{};
    iv_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    iv_info.image = depth_image_;
    iv_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    iv_info.format = VK_FORMAT_D32_SFLOAT;
    iv_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    iv_info.subresourceRange.levelCount = 1;
    iv_info.subresourceRange.layerCount = 1;

    if (vkCreateImageView(device_, &iv_info, nullptr, &depth_image_view_) != VK_SUCCESS) {
        fprintf(stderr, "Failed to create depth image view\n");
        return false;
    }
    return true;
}

bool VulkanContext::create_framebuffers() {
    swapchain_.framebuffers.resize(swapchain_.image_views.size());
    for (size_t i = 0; i < swapchain_.image_views.size(); ++i) {
        VkImageView attachments[2] = {swapchain_.image_views[i], depth_image_view_};

        VkFramebufferCreateInfo fb_info{};
        fb_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fb_info.renderPass = render_pass_;
        fb_info.attachmentCount = 2;
        fb_info.pAttachments = attachments;
        fb_info.width = swapchain_.extent.width;
        fb_info.height = swapchain_.extent.height;
        fb_info.layers = 1;

        if (vkCreateFramebuffer(device_, &fb_info, nullptr, &swapchain_.framebuffers[i]) != VK_SUCCESS) {
            fprintf(stderr, "Failed to create framebuffer %zu\n", i);
            return false;
        }
    }
    return true;
}

bool VulkanContext::create_command_pools() {
    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        VkCommandPoolCreateInfo pool_info{};
        pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        pool_info.queueFamilyIndex = graphics_queue_family_;

        if (vkCreateCommandPool(device_, &pool_info, nullptr, &frames_[i].command_pool) != VK_SUCCESS) {
            fprintf(stderr, "Failed to create command pool %d\n", i);
            return false;
        }

        VkCommandBufferAllocateInfo cmd_info{};
        cmd_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        cmd_info.commandPool = frames_[i].command_pool;
        cmd_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        cmd_info.commandBufferCount = 1;

        if (vkAllocateCommandBuffers(device_, &cmd_info, &frames_[i].command_buffer) != VK_SUCCESS) {
            fprintf(stderr, "Failed to allocate command buffer %d\n", i);
            return false;
        }
    }
    return true;
}

bool VulkanContext::create_sync_objects() {
    VkFenceCreateInfo fence_info{};
    fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fence_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    VkSemaphoreCreateInfo sem_info{};
    sem_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        if (vkCreateFence(device_, &fence_info, nullptr, &frames_[i].render_fence) != VK_SUCCESS) return false;
        if (vkCreateSemaphore(device_, &sem_info, nullptr, &frames_[i].swapchain_semaphore) != VK_SUCCESS) return false;
        if (vkCreateSemaphore(device_, &sem_info, nullptr, &frames_[i].render_semaphore) != VK_SUCCESS) return false;
    }
    return true;
}

void VulkanContext::resize_swapchain() {
    recreate_swapchain_ = true;
}

bool VulkanContext::begin_frame() {
    if (recreate_swapchain_) {
        glfwGetFramebufferSize(window_, (int*)&swapchain_.extent.width, (int*)&swapchain_.extent.height);
        while (swapchain_.extent.width == 0 || swapchain_.extent.height == 0) {
            glfwWaitEvents();
            glfwGetFramebufferSize(window_, (int*)&swapchain_.extent.width, (int*)&swapchain_.extent.height);
        }
        vkDeviceWaitIdle(device_);

        vkDestroyImageView(device_, depth_image_view_, nullptr); depth_image_view_ = VK_NULL_HANDLE;
        vmaDestroyImage(allocator_, depth_image_, depth_allocation_); depth_image_ = VK_NULL_HANDLE;
        for (auto fb : swapchain_.framebuffers) vkDestroyFramebuffer(device_, fb, nullptr);
        swapchain_.framebuffers.clear();
        for (auto iv : swapchain_.image_views) vkDestroyImageView(device_, iv, nullptr);
        swapchain_.image_views.clear();
        vkDestroySwapchainKHR(device_, swapchain_.swapchain, nullptr);

        create_swapchain();
        create_depth_image();
        create_framebuffers();
        recreate_swapchain_ = false;
    }

    FrameData& frame = current_frame();
    VK_CHECK(vkWaitForFences(device_, 1, &frame.render_fence, VK_TRUE, UINT64_MAX));

    uint32_t idx;
    VkResult result = vkAcquireNextImageKHR(device_, swapchain_.swapchain, UINT64_MAX,
        frame.swapchain_semaphore, VK_NULL_HANDLE, &idx);
    image_index_ = (int)idx;

    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        resize_swapchain();
        return false;
    }
    if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
        fprintf(stderr, "Failed to acquire swapchain image: %d\n", (int)result);
        return false;
    }

    VK_CHECK(vkResetFences(device_, 1, &frame.render_fence));
    VK_CHECK(vkResetCommandPool(device_, frame.command_pool, 0));

    VkCommandBufferBeginInfo begin_info{};
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    VK_CHECK(vkBeginCommandBuffer(frame.command_buffer, &begin_info));

    float current_time = (float)glfwGetTime();
    delta_time_ = current_time - last_time_;
    last_time_ = current_time;
    return true;
}

void VulkanContext::end_frame() {
    VK_CHECK(vkEndCommandBuffer(current_frame().command_buffer));
}

void VulkanContext::submit_and_present() {
    FrameData& frame = current_frame();

    VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

    VkSubmitInfo submit_info{};
    submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit_info.waitSemaphoreCount = 1;
    submit_info.pWaitSemaphores = &frame.swapchain_semaphore;
    submit_info.pWaitDstStageMask = &wait_stage;
    submit_info.signalSemaphoreCount = 1;
    submit_info.pSignalSemaphores = &frame.render_semaphore;
    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &frame.command_buffer;

    VK_CHECK(vkQueueSubmit(graphics_queue_, 1, &submit_info, frame.render_fence));

    VkPresentInfoKHR present_info{};
    present_info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    present_info.waitSemaphoreCount = 1;
    present_info.pWaitSemaphores = &frame.render_semaphore;
    present_info.swapchainCount = 1;
    present_info.pSwapchains = &swapchain_.swapchain;
    present_info.pImageIndices = (uint32_t*)&image_index_;

    VkResult result = vkQueuePresentKHR(present_queue_, &present_info);
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
        resize_swapchain();
    } else if (result != VK_SUCCESS) {
        fprintf(stderr, "Present failed: %d\n", (int)result);
    }

    frame_index_ = (frame_index_ + 1) % MAX_FRAMES_IN_FLIGHT;
}

VkCommandBuffer VulkanContext::begin_single_time() {
    // Destroy any existing single-time pool
    if (single_time_pool_ != VK_NULL_HANDLE) {
        vkDestroyCommandPool(device_, single_time_pool_, nullptr);
        single_time_pool_ = VK_NULL_HANDLE;
    }

    VkCommandPoolCreateInfo pool_info{};
    pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pool_info.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    pool_info.queueFamilyIndex = graphics_queue_family_;

    if (vkCreateCommandPool(device_, &pool_info, nullptr,
        &single_time_pool_) != VK_SUCCESS) {
        fprintf(stderr, "Failed to create single-time command pool\n");
        return VK_NULL_HANDLE;
    }

    VkCommandBufferAllocateInfo alloc_info{};
    alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    alloc_info.commandPool = single_time_pool_;
    alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc_info.commandBufferCount = 1;

    VkCommandBuffer cmd;
    vkAllocateCommandBuffers(device_, &alloc_info, &cmd);

    VkCommandBufferBeginInfo begin_info{};
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &begin_info);

    single_time_cmd_ = cmd;
    return cmd;
}

void VulkanContext::end_single_time(VkCommandBuffer cmd) {
    vkEndCommandBuffer(cmd);

    VkSubmitInfo submit_info{};
    submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &cmd;
    vkQueueSubmit(graphics_queue_, 1, &submit_info, VK_NULL_HANDLE);
    vkQueueWaitIdle(graphics_queue_);

    vkDestroyCommandPool(device_, single_time_pool_, nullptr);
    single_time_pool_ = VK_NULL_HANDLE;
    single_time_cmd_ = VK_NULL_HANDLE;
}
