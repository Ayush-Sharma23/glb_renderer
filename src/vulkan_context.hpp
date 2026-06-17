#pragma once

#include <vulkan/vulkan.h>
#include <VkBootstrap.h>
#include <vk_mem_alloc.h>
#include <GLFW/glfw3.h>

#include <vector>
#include <cstdint>

struct FrameData {
    VkFence render_fence = VK_NULL_HANDLE;
    VkSemaphore swapchain_semaphore = VK_NULL_HANDLE;
    VkSemaphore render_semaphore = VK_NULL_HANDLE;
    VkCommandPool command_pool = VK_NULL_HANDLE;
    VkCommandBuffer command_buffer = VK_NULL_HANDLE;
};

struct SwapchainBundle {
    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    VkFormat format = VK_FORMAT_UNDEFINED;
    VkExtent2D extent{};
    std::vector<VkImage> images;
    std::vector<VkImageView> image_views;
    std::vector<VkFramebuffer> framebuffers;
};

class VulkanContext {
public:
    bool init();
    void cleanup();

    void resize_swapchain();

    VkInstance instance() const { return instance_; }
    VkPhysicalDevice physical_device() const { return physical_device_; }
    VkDevice device() const { return device_; }

    VkQueue graphics_queue() const { return graphics_queue_; }
    VkQueue present_queue() const { return present_queue_; }
    VkQueue transfer_queue() const { return transfer_queue_; }
    uint32_t graphics_queue_family() const { return graphics_queue_family_; }
    uint32_t transfer_queue_family() const { return transfer_queue_family_; }

    VmaAllocator allocator() const { return allocator_; }

    const SwapchainBundle& swapchain() const { return swapchain_; }
    VkRenderPass render_pass() const { return render_pass_; }
    VkExtent2D window_extent() const { return swapchain_.extent; }

    int frame_index() const { return frame_index_; }
    int image_index() const { return image_index_; }
    GLFWwindow* window() const { return window_; }
    FrameData& current_frame() { return frames_[frame_index_]; }

    bool begin_frame();
    void end_frame();
    void submit_and_present();

    float delta_time() const { return delta_time_; }

    VkCommandBuffer begin_single_time();
    void end_single_time(VkCommandBuffer cmd);

private:
    bool create_instance();
    bool create_device();
    bool create_allocator();
    bool create_swapchain();
    bool create_render_pass();
    bool create_depth_image();
    bool create_framebuffers();
    bool create_sync_objects();
    bool create_command_pools();

    VkInstance instance_ = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT debug_messenger_ = VK_NULL_HANDLE;
    VkPhysicalDevice physical_device_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;

    VkQueue graphics_queue_ = VK_NULL_HANDLE;
    VkQueue present_queue_ = VK_NULL_HANDLE;
    VkQueue transfer_queue_ = VK_NULL_HANDLE;
    uint32_t graphics_queue_family_ = VK_QUEUE_FAMILY_IGNORED;
    uint32_t transfer_queue_family_ = VK_QUEUE_FAMILY_IGNORED;

    VmaAllocator allocator_ = VK_NULL_HANDLE;

    VkSurfaceKHR surface_ = VK_NULL_HANDLE;
    GLFWwindow* window_ = nullptr;

    vkb::Instance vkb_instance_;

    SwapchainBundle swapchain_;

    VkRenderPass render_pass_ = VK_NULL_HANDLE;

    VkImage depth_image_ = VK_NULL_HANDLE;
    VmaAllocation depth_allocation_ = VK_NULL_HANDLE;
    VkImageView depth_image_view_ = VK_NULL_HANDLE;

    static constexpr int MAX_FRAMES_IN_FLIGHT = 3;
    FrameData frames_[MAX_FRAMES_IN_FLIGHT];
    int frame_index_ = 0;
    int image_index_ = 0;

    float delta_time_ = 0.0f;
    float last_time_ = 0.0f;

    bool recreate_swapchain_ = false;

    VkCommandPool single_time_pool_ = VK_NULL_HANDLE;
    VkCommandBuffer single_time_cmd_ = VK_NULL_HANDLE;
};
