#include "scene.hpp"

void Scene::load_data(VulkanRenderer *renderer, const SceneDefinition &definition)
{
    gltfs.clear();
    root_transforms.clear();
    for (usize i = 0; i < definition.gltf_paths.size(); i++) {
        if (auto loaded_gltf = load_gltf(renderer, definition.gltf_paths[i])) {
            gltfs.push_back(std::move(loaded_gltf));
            root_transforms.push_back(i < definition.root_transforms.size()
                                          ? definition.root_transforms[i]
                                          : glm::mat4(1.0f));
        }
    }
}

void Scene::collect_surfaces(DrawContext &draw_ctx)
{
    for (usize i = 0; i < gltfs.size(); i++) {
        gltfs[i]->draw(root_transforms[i], draw_ctx);
    }
}
