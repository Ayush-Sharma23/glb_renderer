#include "ibl.hpp"
#include "shader_path.hpp"
#include <glm/glm.hpp>
#include <GLFW/glfw3.h>
#include <cmath>
#include <cstring>
#include <vector>
#include <algorithm>

static constexpr int SKY_SIZE = 128;
static constexpr int IRRADIANCE_SIZE = 32;
static constexpr int PREFILTERED_SIZE = 64;
static constexpr int BRDF_LUT_SIZE = 128;

// Generate a simple gradient skybox cubemap
static void generate_skybox_cubemap(std::vector<float>& pixels, int size) {
    pixels.resize(size * size * 6 * 4);
    for (int face = 0; face < 6; face++) {
        for (int y = 0; y < size; y++) {
            for (int x = 0; x < size; x++) {
                float u = (x + 0.5f) / size * 2.0f - 1.0f;
                float v = (y + 0.5f) / size * 2.0f - 1.0f;
                float dir[3];
                switch (face) {
                    case 0: dir[0]=1; dir[1]=-v; dir[2]=-u; break;
                    case 1: dir[0]=-1; dir[1]=-v; dir[2]=u; break;
                    case 2: dir[0]=u; dir[1]=1; dir[2]=v; break;
                    case 3: dir[0]=u; dir[1]=-1; dir[2]=-v; break;
                    case 4: dir[0]=u; dir[1]=-v; dir[2]=1; break;
                    case 5: dir[0]=-u; dir[1]=-v; dir[2]=-1; break;
                }
                float len = std::sqrt(dir[0]*dir[0]+dir[1]*dir[1]+dir[2]*dir[2]);
                float ny = dir[1]/len;

                float horizon = std::max(0.0f, -ny * 0.5f + 0.5f);
                float sky = std::max(0.0f, ny * 0.7f + 0.3f);
                float r = 0.35f + horizon * 0.15f + sky * 0.25f;
                float g = 0.35f + horizon * 0.15f + sky * 0.45f;
                float b = 0.50f + horizon * 0.10f + sky * 0.70f;

                int idx = ((face * size + y) * size + x) * 4;
                pixels[idx + 0] = r;
                pixels[idx + 1] = g;
                pixels[idx + 2] = b;
                pixels[idx + 3] = 1.0f;
            }
        }
    }
}

// Generate BRDF LUT on CPU
static void generate_brdf_lut(std::vector<float>& pixels) {
    pixels.resize(BRDF_LUT_SIZE * BRDF_LUT_SIZE * 2);

    auto radical_inverse_vdC = [](uint32_t bits) {
        bits = (bits << 16u) | (bits >> 16u);
        bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
        bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
        bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
        bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
        return float(bits) * 2.3283064365386963e-10f;
    };

    for (int y = 0; y < BRDF_LUT_SIZE; y++) {
        for (int x = 0; x < BRDF_LUT_SIZE; x++) {
            float NdotV = (y + 0.5f) / BRDF_LUT_SIZE;
            float roughness = (x + 0.5f) / BRDF_LUT_SIZE;

            glm::vec3 V(std::sqrt(1.0f - NdotV * NdotV), 0.0f, NdotV);
            glm::vec3 N(0.0f, 0.0f, 1.0f);

            const uint32_t SAMPLE_COUNT = 128;
            float A = 0.0f, B = 0.0f;

            for (uint32_t i = 0; i < SAMPLE_COUNT; i++) {
                float xi_x = float(i) / float(SAMPLE_COUNT);
                float xi_y = radical_inverse_vdC(i);

                float a = roughness * roughness;
                float phi = 6.28318530718f * xi_x;
                float cos_theta = std::sqrt((1.0f - xi_y) / (1.0f + (a*a - 1.0f) * xi_y));
                float sin_theta = std::sqrt(1.0f - cos_theta * cos_theta);

                glm::vec3 H(std::cos(phi) * sin_theta, std::sin(phi) * sin_theta, cos_theta);

                glm::vec3 up = std::abs(N.z) < 0.999f ? glm::vec3(0,0,1) : glm::vec3(1,0,0);
                glm::vec3 T = glm::normalize(glm::cross(up, N));
                glm::vec3 Bv = glm::cross(N, T);
                H = glm::normalize(T * H.x + Bv * H.y + N * H.z);

                glm::vec3 L = glm::normalize(2.0f * glm::dot(V, H) * H - V);

                float NdotL = std::max(L.z, 0.0f);
                float NdotH = std::max(H.z, 0.0f);
                float VdotH = std::max(glm::dot(V, H), 0.0f);

                if (NdotL > 0.0f) {
                    float k = (roughness * roughness) / 2.0f;
                    float G1 = NdotV / (NdotV * (1.0f - k) + k);
                    float G2 = NdotL / (NdotL * (1.0f - k) + k);
                    float G = G1 * G2;
                    float G_Vis = G * VdotH / (NdotH * NdotV);
                    float Fc = std::pow(1.0f - VdotH, 5.0f);
                    A += (1.0f - Fc) * G_Vis;
                    B += Fc * G_Vis;
                }
            }
            A /= float(SAMPLE_COUNT);
            B /= float(SAMPLE_COUNT);

            int idx = (y * BRDF_LUT_SIZE + x) * 2;
            pixels[idx + 0] = A;
            pixels[idx + 1] = B;
        }
    }
}

// Generate irradiance cubemap by convolving skybox
static void generate_irradiance_cubemap(const std::vector<float>& sky, std::vector<float>& irrad,
                                        int sky_size, int irr_size) {
    irrad.resize(irr_size * irr_size * 6 * 4);
    auto sample_sky = [&](float nx, float ny, float nz) -> glm::vec3 {
        int best_face = 0;
        float best = -999.0f;
        float dir[3] = {nx, ny, nz};
        float faces[6][3] = {{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}};
        for (int f = 0; f < 6; f++) {
            float d = dir[0]*faces[f][0] + dir[1]*faces[f][1] + dir[2]*faces[f][2];
            if (d > best) { best = d; best_face = f; }
        }
        int f = best_face;
        float ax = std::abs(dir[0]), ay = std::abs(dir[1]), az = std::abs(dir[2]);
        float s, t;
        if (f == 0) { s = -dir[2]/ax; t = -dir[1]/ax; }
        else if (f == 1) { s = dir[2]/ax; t = -dir[1]/ax; }
        else if (f == 2) { s = dir[0]/ay; t = dir[2]/ay; }
        else if (f == 3) { s = dir[0]/ay; t = -dir[2]/ay; }
        else if (f == 4) { s = dir[0]/az; t = -dir[1]/az; }
        else { s = -dir[0]/az; t = -dir[1]/az; }
        s = s * 0.5f + 0.5f;
        t = t * 0.5f + 0.5f;
        int px = std::min((int)(s * sky_size), sky_size - 1);
        int py = std::min((int)(t * sky_size), sky_size - 1);
        int idx = ((f * sky_size + py) * sky_size + px) * 4;
        return {sky[idx], sky[idx+1], sky[idx+2]};
    };

    for (int face = 0; face < 6; face++) {
        for (int y = 0; y < irr_size; y++) {
            for (int x = 0; x < irr_size; x++) {
                float u = (x + 0.5f) / irr_size * 2.0f - 1.0f;
                float v = (y + 0.5f) / irr_size * 2.0f - 1.0f;
                float dir[3];
                switch (face) {
                    case 0: dir[0]=1; dir[1]=-v; dir[2]=-u; break;
                    case 1: dir[0]=-1; dir[1]=-v; dir[2]=u; break;
                    case 2: dir[0]=u; dir[1]=1; dir[2]=v; break;
                    case 3: dir[0]=u; dir[1]=-1; dir[2]=-v; break;
                    case 4: dir[0]=u; dir[1]=-v; dir[2]=1; break;
                    case 5: dir[0]=-u; dir[1]=-v; dir[2]=-1; break;
                }
                float len = std::sqrt(dir[0]*dir[0]+dir[1]*dir[1]+dir[2]*dir[2]);
                float nx = dir[0]/len, ny = dir[1]/len, nz = dir[2]/len;

                glm::vec3 irradiance(0);
                const float step = 1.0f / 32.0f;
                for (float phi = 0; phi < 6.28318530718f; phi += step) {
                    for (float theta = 0; theta < 1.57079632679f; theta += step) {
                        float sx = std::sin(theta) * std::cos(phi);
                        float sy = std::sin(theta) * std::sin(phi);
                        float sz = std::cos(theta);
                        glm::vec3 upv = std::abs(ny) < 0.999f ? glm::vec3(0,1,0) : glm::vec3(1,0,0);
                        glm::vec3 T = glm::normalize(glm::cross(upv, glm::vec3(nx,ny,nz)));
                        glm::vec3 Bv = glm::cross(glm::vec3(nx,ny,nz), T);
                        glm::vec3 sample_dir = glm::normalize(T*sx + Bv*sy + glm::vec3(nx,ny,nz)*sz);

                        float NdotL = std::max(nx*sample_dir.x + ny*sample_dir.y + nz*sample_dir.z, 0.0f);
                        auto sky_col = sample_sky(sample_dir.x, sample_dir.y, sample_dir.z);
                        irradiance += sky_col * NdotL * std::sin(theta);
                    }
                }
                irradiance *= 3.14159265359f / (32.0f * 32.0f);

                int idx = ((face * irr_size + y) * irr_size + x) * 4;
                irrad[idx] = irradiance.r;
                irrad[idx+1] = irradiance.g;
                irrad[idx+2] = irradiance.b;
                irrad[idx+3] = 1.0f;
            }
        }
    }
}

// Upload cubemap data to GPU (raw VkImage, manual creation)
static bool create_cubemap_gpu(VulkanContext& ctx,
    const std::vector<float>& data, int size,
    VkFormat format, int mip_levels,
    VkImage& out_image, VmaAllocation& out_alloc, VkImageView& out_view)
{
    VkDeviceSize layer_size = size * size * 4 * sizeof(float);
    VkDeviceSize total_size = layer_size * 6;

    // Create image
    VkImageCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    info.imageType = VK_IMAGE_TYPE_2D;
    info.format = format;
    info.extent = {(uint32_t)size, (uint32_t)size, 1};
    info.mipLevels = mip_levels;
    info.arrayLayers = 6;
    info.samples = VK_SAMPLE_COUNT_1_BIT;
    info.tiling = VK_IMAGE_TILING_OPTIMAL;
    info.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    info.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
    info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VmaAllocationCreateInfo alloc_info{};
    alloc_info.usage = VMA_MEMORY_USAGE_GPU_ONLY;

    if (vmaCreateImage(ctx.allocator(), &info, &alloc_info,
        &out_image, &out_alloc, nullptr) != VK_SUCCESS) {
        fprintf(stderr, "Failed to create cubemap image\n");
        return false;
    }

    // Staging buffer
    Buffer staging = Buffer::create(ctx, total_size,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_ONLY);
    void* mapped;
    if (vmaMapMemory(ctx.allocator(), staging.allocation, &mapped) != VK_SUCCESS) {
        fprintf(stderr, "Failed to map staging buffer\n");
        staging.destroy(ctx.allocator());
        return false;
    }
    std::memcpy(mapped, data.data(), total_size);
    vmaUnmapMemory(ctx.allocator(), staging.allocation);

    // Single-time upload
    VkCommandBuffer cmd = ctx.begin_single_time();

    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.image = out_image;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = mip_levels;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 6;
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

    for (int l = 0; l < 6; l++) {
        VkBufferImageCopy region{};
        region.bufferOffset = layer_size * l;
        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.mipLevel = 0;
        region.imageSubresource.baseArrayLayer = l;
        region.imageSubresource.layerCount = 1;
        region.imageExtent = {(uint32_t)size, (uint32_t)size, 1};
        vkCmdCopyBufferToImage(cmd, staging.buffer, out_image,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    }

    // Generate mips
    if (mip_levels > 1) {
        for (int l = 0; l < 6; l++) {
            int mip_w = size, mip_h = size;
            for (int mip = 1; mip < mip_levels; mip++) {
                VkImageMemoryBarrier pre{};
                pre.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
                pre.image = out_image;
                pre.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                pre.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
                pre.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                pre.subresourceRange.baseMipLevel = mip - 1;
                pre.subresourceRange.levelCount = 1;
                pre.subresourceRange.baseArrayLayer = l;
                pre.subresourceRange.layerCount = 1;
                pre.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                pre.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
                vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                    VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &pre);

                VkImageBlit blit{};
                blit.srcOffsets[0] = {0, 0, 0};
                blit.srcOffsets[1] = {mip_w, mip_h, 1};
                blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                blit.srcSubresource.mipLevel = mip - 1;
                blit.srcSubresource.baseArrayLayer = l;
                blit.srcSubresource.layerCount = 1;
                blit.dstOffsets[0] = {0, 0, 0};
                blit.dstOffsets[1] = {std::max(1, mip_w/2), std::max(1, mip_h/2), 1};
                blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                blit.dstSubresource.mipLevel = mip;
                blit.dstSubresource.baseArrayLayer = l;
                blit.dstSubresource.layerCount = 1;
                vkCmdBlitImage(cmd, out_image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                    out_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR);
                mip_w = std::max(1, mip_w/2);
                mip_h = std::max(1, mip_h/2);
            }
        }
        // Transition last mips to shader read
        for (int mip = 0; mip < mip_levels; mip++) {
            VkImageMemoryBarrier post{};
            post.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            post.image = out_image;
            post.oldLayout = mip == mip_levels - 1 ? VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL : VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            post.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            post.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            post.subresourceRange.baseMipLevel = mip;
            post.subresourceRange.levelCount = 1;
            post.subresourceRange.baseArrayLayer = 0;
            post.subresourceRange.layerCount = 6;
            post.srcAccessMask = mip == mip_levels - 1 ? VK_ACCESS_TRANSFER_WRITE_BIT : VK_ACCESS_TRANSFER_READ_BIT;
            post.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &post);
        }
    } else {
        VkImageMemoryBarrier post{};
        post.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        post.image = out_image;
        post.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        post.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        post.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        post.subresourceRange.baseMipLevel = 0;
        post.subresourceRange.levelCount = 1;
        post.subresourceRange.baseArrayLayer = 0;
        post.subresourceRange.layerCount = 6;
        post.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        post.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &post);
    }
    ctx.end_single_time(cmd);
    staging.destroy(ctx.allocator());

    // Create image view
    VkImageViewCreateInfo view_info{};
    view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view_info.image = out_image;
    view_info.viewType = VK_IMAGE_VIEW_TYPE_CUBE;
    view_info.format = format;
    view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    view_info.subresourceRange.baseMipLevel = 0;
    view_info.subresourceRange.levelCount = mip_levels;
    view_info.subresourceRange.baseArrayLayer = 0;
    view_info.subresourceRange.layerCount = 6;
    if (vkCreateImageView(ctx.device(), &view_info, nullptr, &out_view) != VK_SUCCESS) {
        fprintf(stderr, "Failed to create cubemap view\n");
        return false;
    }
    return true;
}

bool IBLData::build(VulkanContext& ctx) {
    double t0 = glfwGetTime();
    VkFormat cubemap_fmt = VK_FORMAT_R32G32B32A32_SFLOAT;

    // Generate skybox cubemap
    std::vector<float> sky_pixels;
    generate_skybox_cubemap(sky_pixels, SKY_SIZE);
    if (!create_cubemap_gpu(ctx, sky_pixels, SKY_SIZE, cubemap_fmt, 1,
        skybox, skybox_alloc, skybox_view)) {
        fprintf(stderr, "Failed to create skybox cubemap\n");
        return false;
    }

    // Generate irradiance cubemap
    std::vector<float> irr_pixels;
    generate_irradiance_cubemap(sky_pixels, irr_pixels, SKY_SIZE, IRRADIANCE_SIZE);
    if (!create_cubemap_gpu(ctx, irr_pixels, IRRADIANCE_SIZE, cubemap_fmt, 1,
        irradiance, irradiance_alloc, irradiance_view)) {
        fprintf(stderr, "Failed to create irradiance cubemap\n");
        return false;
    }

    // Generate prefiltered cubemap with mip chain
    int pref_mips = (int)std::floor(std::log2(PREFILTERED_SIZE)) + 1;
    // Use sky color data for prefiltered (downsampled via mips)
    std::vector<float> pref_pixels = sky_pixels;
    if (!create_cubemap_gpu(ctx, pref_pixels, PREFILTERED_SIZE, cubemap_fmt, pref_mips,
        prefiltered, prefiltered_alloc, prefiltered_view)) {
        fprintf(stderr, "Failed to create prefiltered cubemap\n");
        return false;
    }

    // Generate BRDF LUT on CPU, upload as 2D texture
    std::vector<float> brdf_pixels;
    generate_brdf_lut(brdf_pixels);

    brdf_lut_img = Image2D::create(ctx, BRDF_LUT_SIZE, BRDF_LUT_SIZE,
        VK_FORMAT_R32G32_SFLOAT,
        VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, 1);
    brdf_lut_view = brdf_lut_img.view; // reuse the view created by Image2D::create

    // Upload BRDF LUT data (reuse staging)
    Buffer brdf_staging = Buffer::create(ctx,
        BRDF_LUT_SIZE * BRDF_LUT_SIZE * 2 * sizeof(float),
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_ONLY);
    void* brdf_mapped;
    vmaMapMemory(ctx.allocator(), brdf_staging.allocation, &brdf_mapped);
    std::memcpy(brdf_mapped, brdf_pixels.data(),
        BRDF_LUT_SIZE * BRDF_LUT_SIZE * 2 * sizeof(float));
    vmaUnmapMemory(ctx.allocator(), brdf_staging.allocation);

    VkCommandBuffer cmd = ctx.begin_single_time();
    VkBufferImageCopy brdf_copy{};
    brdf_copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    brdf_copy.imageSubresource.layerCount = 1;
    brdf_copy.imageExtent = {(uint32_t)BRDF_LUT_SIZE, (uint32_t)BRDF_LUT_SIZE, 1};

    // Transition to transfer dst
    VkImageMemoryBarrier brdf_barrier{};
    brdf_barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    brdf_barrier.image = brdf_lut_img.image;
    brdf_barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    brdf_barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    brdf_barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    brdf_barrier.subresourceRange.levelCount = 1;
    brdf_barrier.subresourceRange.layerCount = 1;
    brdf_barrier.srcAccessMask = 0;
    brdf_barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &brdf_barrier);

    vkCmdCopyBufferToImage(cmd, brdf_staging.buffer, brdf_lut_img.image,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &brdf_copy);

    // Transition to shader read
    brdf_barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    brdf_barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    brdf_barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    brdf_barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &brdf_barrier);

    ctx.end_single_time(cmd);
    brdf_staging.destroy(ctx.allocator());

    // Create sampler
    cubemap_sampler = Sampler::create(ctx.device(),
        VK_FILTER_LINEAR, VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_REPEAT, true, 16.0f, true);

    printf("IBL generated in %.1f seconds: skybox=%dx%d, irradiance=%dx%d, prefiltered=%dx%d, brdf_lut=%dx%d\n",
        (glfwGetTime() - t0),
        SKY_SIZE, SKY_SIZE, IRRADIANCE_SIZE, IRRADIANCE_SIZE,
        PREFILTERED_SIZE, PREFILTERED_SIZE, BRDF_LUT_SIZE, BRDF_LUT_SIZE);
    return true;
}

void IBLData::destroy(VulkanContext& ctx) {
    auto destroy_cubemap = [&](VkImage img, VmaAllocation alloc, VkImageView view) {
        if (view) vkDestroyImageView(ctx.device(), view, nullptr);
        if (img) vmaDestroyImage(ctx.allocator(), img, alloc);
    };
    cubemap_sampler.destroy(ctx.device());
    brdf_lut_img.destroy(ctx);
    destroy_cubemap(skybox, skybox_alloc, skybox_view);
    destroy_cubemap(irradiance, irradiance_alloc, irradiance_view);
    destroy_cubemap(prefiltered, prefiltered_alloc, prefiltered_view);
}
