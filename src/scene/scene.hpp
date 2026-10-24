#pragma once

/**
 * @file
 * Scenes as the engine sees them: which glTF files make up a scene, and the loaded result.
 * Independent of the renderer's GPU layout, which is built from `Scene::collect_surfaces`.
 */

#include <filesystem>
#include <glm/glm.hpp>
#include <string>
#include <vector>

#include "vk_loader.hpp"
#include "vk_render_scene.hpp"

struct DefaultResources;
struct GpuContext;
class MaterialSystem;

/**
 * What a scene is made of. Plain data with no GPU resources, so it is cheap to copy and to keep a
 * list of.
 */
struct SceneDefinition
{
    std::string name;
    std::vector<std::filesystem::path> gltf_paths;
    /** One per glTF, in the same order as `gltf_paths`. Missing entries mean identity. */
    std::vector<glm::mat4> root_transforms;
};

/**
 * A loaded `SceneDefinition`. Owns the glTFs (and through them the meshes, textures and materials)
 * that a `RenderScene` points into, so it must outlive the `RenderScene` built from it.
 */
struct Scene
{
    std::vector<std::unique_ptr<LoadedGLTF>> gltfs;
    /** Parallel to `gltfs`. */
    std::vector<glm::mat4> root_transforms;

    /**
     * @brief Replace the loaded glTFs with the ones in `definition`. glTFs that fail to load are
     * skipped.
     *
     * @param gpu The GPU context to create the resources with. Must outlive this scene's glTFs.
     * @param defaults Fallback textures and samplers for anything a file doesn't provide.
     * @param material_system Material system to build the materials with.
     * @param definition The files and root transforms to load.
     */
    void load_data(const GpuContext &gpu, const DefaultResources &defaults,
        MaterialSystem &material_system, const SceneDefinition &definition);

    /** Flattens every glTF's node hierarchy into `draw_ctx`, applying the root transforms. */
    void collect_surfaces(DrawContext &draw_ctx);
};
