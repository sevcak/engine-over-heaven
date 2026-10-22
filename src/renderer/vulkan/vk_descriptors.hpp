#pragma once

#include <deque>
#include <span>
#include <vk_types.hpp>

struct DescriptorLayoutBuilder
{
    std::vector<VkDescriptorSetLayoutBinding> bindings;

    void add_binding(u32 binding, VkDescriptorType type);

    void clear();

    VkDescriptorSetLayout build(VkDevice device, VkShaderStageFlags shader_stages,
        void *p_next = nullptr, VkDescriptorSetLayoutCreateFlags flags = 0);
};


struct DescriptorAllocatorGrowable
{
public:
    struct PoolSizeRatio
    {
        VkDescriptorType type;
        f32 ratio;
    };

    void init(VkDevice device, u32 initial_sets, std::span<PoolSizeRatio> pool_ratios);

    void clear_pools(VkDevice device);

    void destroy_pools(VkDevice device);

    VkDescriptorSet try_allocate(
        VkDevice device, VkDescriptorSetLayout layout, void *p_next = nullptr);

    VkDescriptorSet allocate(VkDevice device, VkDescriptorSetLayout layout, void *p_next = nullptr);

private:
    std::vector<PoolSizeRatio> ratios;
    std::vector<VkDescriptorPool> full_pools;
    std::vector<VkDescriptorPool> ready_pools;
    u32 sets_per_pool;

    VkDescriptorPool get_pool(VkDevice device);

    VkDescriptorPool create_pool(
        VkDevice device, u32 set_count, std::span<PoolSizeRatio> pool_ratios);
};

struct DescriptorWriter
{
    std::deque<VkDescriptorImageInfo> image_infos;
    std::deque<VkDescriptorBufferInfo> buffer_infos;
    std::vector<VkWriteDescriptorSet> writes;

    void write_image(int binding, VkImageView image, VkSampler sampler, VkImageLayout layout,
        VkDescriptorType type);

    void write_buffer(
        int binding, VkBuffer buffer, usize size, usize offset, VkDescriptorType type);

    void clear();

    void update_set(VkDevice device, VkDescriptorSet set);
};