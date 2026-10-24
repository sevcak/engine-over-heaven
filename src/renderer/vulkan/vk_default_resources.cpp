#include "vk_default_resources.hpp"

#include <array>
#include <glm/packing.hpp>

void DefaultResources::init(const GpuContext &gpu, MaterialSystem &material_system,
    DescriptorAllocatorGrowable &descriptor_allocator)
{
    u32 white = glm::packUnorm4x8(glm::vec4(1.0f, 1.0f, 1.0f, 1.0f));
    white_image = gpu.create_image((void *)&white, VkExtent3D { 1, 1, 1 },
        VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_SAMPLED_BIT);

    u32 grey = glm::packUnorm4x8(glm::vec4(0.66f, 0.66f, 0.66f, 1.0f));
    grey_image = gpu.create_image((void *)&grey, VkExtent3D { 1, 1, 1 }, VK_FORMAT_R8G8B8A8_UNORM,
        VK_IMAGE_USAGE_SAMPLED_BIT);

    u32 black = glm::packUnorm4x8(glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
    black_image = gpu.create_image((void *)&black, VkExtent3D { 1, 1, 1 },
        VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_SAMPLED_BIT);

    u32 magenta = glm::packUnorm4x8(glm::vec4(1.0f, 0.0f, 1.0f, 1.0f));
    std::array<u32, 16 * 16> pixels;
    for (int x = 0; x < 16; x++) {
        for (int y = 0; y < 16; y++) {
            pixels[(y * 16) + x] = ((x % 2) ^ (y % 2)) ? magenta : black;
        }
    }
    error_checkerboard_image = gpu.create_image(pixels.data(), VkExtent3D { 16, 16, 1 },
        VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_SAMPLED_BIT);

    VkSamplerCreateInfo sampler_info = { .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
    sampler_info.magFilter = VK_FILTER_NEAREST;
    sampler_info.minFilter = VK_FILTER_NEAREST;

    vkCreateSampler(gpu.device, &sampler_info, nullptr, &sampler_nearest);

    sampler_info.magFilter = VK_FILTER_LINEAR;
    sampler_info.minFilter = VK_FILTER_LINEAR;

    vkCreateSampler(gpu.device, &sampler_info, nullptr, &sampler_linear);

    GLTFMetallic_Roughness::MaterialResources material_resources;
    material_resources.color_image = white_image;
    material_resources.color_sampler = sampler_linear;
    material_resources.metal_rough_image = white_image;
    material_resources.metal_rough_sampler = sampler_linear;

    default_material_constants =
        gpu.create_buffer(sizeof(GLTFMetallic_Roughness::MaterialConstants),
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);

    GLTFMetallic_Roughness::MaterialConstants *constants =
        (GLTFMetallic_Roughness::MaterialConstants *)default_material_constants.info.pMappedData;
    constants->color_factors = glm::vec4 { 1.0f, 1.0f, 1.0f, 1.0f };
    constants->metal_rough_factors = glm::vec4 { 0.0f, 0.5f, 0.0f, 0.0f };

    material_resources.data_buffer = default_material_constants.buffer;
    material_resources.data_buffer_offset = 0;

    default_material = material_system.get_type<GLTFMetallic_Roughness>().write_material(
        gpu.device, MaterialPass::Opaque, material_resources, descriptor_allocator);
}

void DefaultResources::destroy(const GpuContext &gpu)
{
    gpu.destroy_buffer(default_material_constants);

    vkDestroySampler(gpu.device, sampler_nearest, nullptr);
    vkDestroySampler(gpu.device, sampler_linear, nullptr);

    gpu.destroy_image(white_image);
    gpu.destroy_image(grey_image);
    gpu.destroy_image(black_image);
    gpu.destroy_image(error_checkerboard_image);
}
