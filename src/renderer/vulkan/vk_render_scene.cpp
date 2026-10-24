#include "vk_render_scene.hpp"

#include <algorithm>
#include <tuple>
#include <vector>

#include <logger/logger.hpp>

#include "vk_context.hpp"
#include "vk_gpu_data.hpp"

RenderScene build_render_scene(const GpuContext &gpu, DrawContext &ctx)
{
    const usize total_surfaces = ctx.opaque_surfaces.size() + ctx.transparent_surfaces.size();
    if (total_surfaces == 0) {
        return {};
    }
    if (total_surfaces > MAX_OBJECTS) {
        OH_LOG_FATAL("Too many surfaces in the scene ({}), exceeds MAX_OBJECTS.", total_surfaces);
        return {};
    }

    RenderScene render_scene;
    render_scene.first_transparent_index = ctx.opaque_surfaces.size();

    std::sort(ctx.opaque_surfaces.begin(), ctx.opaque_surfaces.end(),
        [](const RenderObject &a, const RenderObject &b) {
            return std::tuple(a.material->pipeline, a.material->material_set, a.index_buffer) <
                   std::tuple(b.material->pipeline, b.material->material_set, b.index_buffer);
        });

    std::vector<VkDrawIndexedIndirectCommand> input_commands;
    input_commands.reserve(total_surfaces);
    std::vector<GPUObjectData> gpu_object_data;
    gpu_object_data.reserve(total_surfaces);

    // Gives `obj` the next slot: one entry in every per-slot array, all at the same index.
    auto append_slot = [&](const RenderObject &obj) {
        const u32 slot = static_cast<u32>(gpu_object_data.size());

        input_commands.push_back({ .indexCount = obj.index_count,
            .instanceCount = 1,
            .firstIndex = obj.first_index,
            .vertexOffset = 0,
            .firstInstance = slot });

        gpu_object_data.push_back({ .world_matrix = obj.transform,
            .sphere_bounds = glm::vec4(obj.bounds.origin, obj.bounds.sphere_radius),
            .vertex_buffer = obj.vertex_buffer_address });

        render_scene.index_buffers.push_back(obj.index_buffer);
        render_scene.materials.push_back(obj.material);
    };

    for (const RenderObject &obj : ctx.opaque_surfaces) {
        append_slot(obj);
    }
    for (const RenderObject &obj : ctx.transparent_surfaces) {
        append_slot(obj);
        render_scene.transparent_centers.push_back(
            glm::vec3(obj.transform * glm::vec4(obj.bounds.origin, 1.0f)));
    }

    for (const VkDrawIndexedIndirectCommand &command : input_commands) {
        render_scene.total_triangle_count += command.indexCount / 3;
    }

    render_scene.object_buffer = gpu.upload_to_device_local(gpu_object_data.data(),
        gpu_object_data.size() * sizeof(GPUObjectData), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    render_scene.input_command_buffer = gpu.upload_to_device_local(input_commands.data(),
        input_commands.size() * sizeof(VkDrawIndexedIndirectCommand),
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);

    OH_LOG_DEBUG("Opaque count: {}", ctx.opaque_surfaces.size());
    OH_LOG_DEBUG("Transparent count: {}", ctx.transparent_surfaces.size());
    OH_LOG_DEBUG("Total surfaces: {}", total_surfaces);
    OH_LOG_DEBUG("Input commands count: {}", input_commands.size());

    render_scene.transparent_render_order.resize(render_scene.transparent_centers.size());
    render_scene.center_distances_sq.resize(render_scene.transparent_centers.size());

    return render_scene;
}

void destroy_render_scene(const GpuContext &gpu, RenderScene &scene)
{
    gpu.destroy_buffer(scene.object_buffer);
    gpu.destroy_buffer(scene.input_command_buffer);
    scene = {};
}
