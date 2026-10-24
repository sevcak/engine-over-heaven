#pragma once

/**
 * @file
 * The renderer's view of a scene: `DrawContext` is the flat list of surfaces handed to the
 * renderer, and `RenderScene` is the GPU-side layout built from it once at load time.
 */

#include "vk_material_system.hpp"
#include "vk_types.hpp"

/**
 * Persistent result of building a scene. Built once when the scene loads; per frame only the
 * camera-dependent work (culling, transparent sort) touches it.
 *
 * Slots: every surface gets a slot number at build time. Slot `i` is the object's permanent
 * identity. All per-draw arrays below are indexed by slot, and the object buffer is never
 * reordered. Draw order is changed by reordering the indirect commands instead, which point back to
 * a slot through `firstInstance`.
 *
 * Slot layout: [0, first_transparent_index) is opaque, sorted once by (material, index buffer).
 * [first_transparent_index, end) is transparent, re-sorted by camera distance every frame.
 *
 * Does not own the meshes, textures or materials the pointers refer to. Those live in the
 * `LoadedGLTF`s in `Scene`, which must outlive this.
 */
struct RenderScene
{
    /**
     * Device-local array of `GPUObjectData` (world matrix, bounding sphere, vertex buffer address),
     * one per slot. Uploaded once at load, read-only on the GPU afterwards.
     */
    AllocatedBuffer object_buffer;

    /** Index buffer of the draw at each slot. Read by draw recording for vkCmdBindIndexBuffer. */
    std::vector<VkBuffer> index_buffers;
    // TODO: We should consider storing indices here instead for better control and potential memory
    // savings.
    /** Material of the draw at each slot. Read by draw recording to decide when to rebind. */
    std::vector<MaterialInstance *> materials;
    /** First transparent slot. Equals the opaque draw count. */
    u32 first_transparent_index = 0;

    /**
     * World-space center of each transparent object (indexed by `slot - first_transparent_index`).
     * Computed once at load. The per-frame transparent sort uses it to get the camera distance.
     */
    std::vector<glm::vec3> transparent_centers;

    /**
     * Triangles in the whole scene, every draw counted once whether visible or not. Computed once
     * at load, since it never changes. Reported as the "total triangles" stat.
     */
    u32 total_triangle_count = 0;

    /**
     * Device-local `VkDrawIndexedIndirectCommand` per slot, uploaded once. Read-only input
     * to the shader, which copies the visible ones into the current frame's output buffer.
     */
    AllocatedBuffer input_command_buffer;
};

struct RenderObject
{
    u32 index_count;
    u32 first_index;
    VkBuffer index_buffer;

    MaterialInstance *material;
    Bounds bounds;
    glm::mat4 transform;
    VkDeviceAddress vertex_buffer_address;
};

struct DrawContext
{
    std::vector<RenderObject> opaque_surfaces;
    std::vector<RenderObject> transparent_surfaces;
};
