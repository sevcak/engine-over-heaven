#include "gltf_loader.hpp"

#include "gltf_image_usage.hpp"
#include "vk_context.hpp"
#include "vk_default_resources.hpp"
#include "vk_render_scene.hpp"
#include "vk_types.hpp"

#include <chrono>
#include <fastgltf/types.hpp>
#include <iostream>
// The loader is the only user of stb_image, so its implementation is compiled here.
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include <fastgltf/core.hpp>
#include <fastgltf/glm_element_traits.hpp>
#include <fastgltf/tools.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/quaternion.hpp>
#include <logger/logger.hpp>

std::optional<AllocatedImage> load_image(const GpuContext &gpu, fastgltf::Asset &asset,
    fastgltf::Image &image, std::filesystem::path gltf_path);

VkFilter extract_filter(fastgltf::Filter filter)
{
    switch (filter) {
    case fastgltf::Filter::Nearest:
    case fastgltf::Filter::NearestMipMapNearest:
    case fastgltf::Filter::NearestMipMapLinear:
        return VK_FILTER_NEAREST;

    case fastgltf::Filter::Linear:
    case fastgltf::Filter::LinearMipMapNearest:
    case fastgltf::Filter::LinearMipMapLinear:
    default:
        return VK_FILTER_LINEAR;
    }
}

VkSamplerMipmapMode extract_mipmap_mode(fastgltf::Filter filter)
{
    switch (filter) {
    case fastgltf::Filter::NearestMipMapNearest:
    case fastgltf::Filter::LinearMipMapNearest:
        return VK_SAMPLER_MIPMAP_MODE_NEAREST;

    case fastgltf::Filter::NearestMipMapLinear:
    case fastgltf::Filter::LinearMipMapLinear:
    default:
        return VK_SAMPLER_MIPMAP_MODE_LINEAR;
    }
}

std::optional<fastgltf::Asset> parse_gltf_file(const std::filesystem::path &path)
{
    fastgltf::Parser parser {};

    const auto gltf_options = fastgltf::Options::DontRequireValidAssetMember |
                              fastgltf::Options::AllowDouble |
                              fastgltf::Options::LoadExternalBuffers;
    // | fastgltf::Options::LoadExternalImages;

    auto data = fastgltf::GltfDataBuffer::FromPath(path);
    if (!data) {
        std::cerr << "Failed to load glTF data: " << fastgltf::to_underlying(data.error())
                  << std::endl;
        return {};
    }

    fastgltf::Asset gltf;

    auto type = fastgltf::determineGltfFileType(data.get());
    if (type == fastgltf::GltfType::glTF) {
        auto load = parser.loadGltf(data.get(), path.parent_path(), gltf_options);
        if (load) {
            gltf = std::move(load.get());
        } else {
            std::cerr << "Failed to load glTF: " << fastgltf::to_underlying(load.error())
                      << std::endl;
            return {};
        }
    } else if (type == fastgltf::GltfType::GLB) {
        auto load = parser.loadGltfBinary(data.get(), path.parent_path(), gltf_options);
        if (load) {
            gltf = std::move(load.get());
        } else {
            std::cerr << "Failed to load glTF: " << fastgltf::to_underlying(load.error())
                      << std::endl;
            return {};
        }
    } else {
        std::cerr << "Failed to determine glTF container." << std::endl;
        return {};
    }

    return gltf;
}

std::vector<VkSampler> load_samplers(VkDevice device, const fastgltf::Asset &asset)
{
    std::vector<VkSampler> samplers;
    samplers.reserve(asset.samplers.size());

    for (const fastgltf::Sampler &sampler : asset.samplers) {
        VkSamplerCreateInfo sampl = { .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
            .pNext = nullptr };
        sampl.maxLod = VK_LOD_CLAMP_NONE;
        sampl.minLod = 0;

        sampl.magFilter = extract_filter(sampler.magFilter.value_or(fastgltf::Filter::Nearest));
        sampl.minFilter = extract_filter(sampler.minFilter.value_or(fastgltf::Filter::Nearest));

        sampl.mipmapMode =
            extract_mipmap_mode(sampler.minFilter.value_or(fastgltf::Filter::Nearest));

        VkSampler new_sampler;
        vkCreateSampler(device, &sampl, nullptr, &new_sampler);

        samplers.push_back(new_sampler);
    }

    return samplers;
}

std::vector<AllocatedImage> load_images(const GpuContext &gpu, fastgltf::Asset &asset,
    const std::span<const ImageUsage> image_usage, std::filesystem::path gltf_path)
{
    std::vector<AllocatedImage> images;
    images.reserve(asset.images.size());

    for (usize i = 0; i < asset.images.size(); ++i) {
        if (image_usage[i] == ImageUsage::Unused) {
            OH_LOG_DEBUG("Skipping unused image {}.", describe_image(asset.images[i], i));
            images.push_back(AllocatedImage {});
            continue;
        }

        fastgltf::Image &image = asset.images[i];
        std::optional<AllocatedImage> img = load_image(gpu, asset, image, gltf_path);

        if (img.has_value()) {
            images.push_back(*img);
        } else {
            OH_LOG_ERROR("Failed to load texture {}.", describe_image(image, i));
            // Push an empty image, so the image indices stay in glTF order.
            // This is safe, because whenever we see a null handle, we will use a default image and
            // destroying null handles is a no-op.
            images.push_back(AllocatedImage {});
        }
    }

    return images;
}

/**
 * @brief Picks the image a glTF texture refers to, falling back to a default if it failed to load.
 * @param images Images in glTF image order. Images that failed to load have a null handle.
 * @param index glTF image index from the texture.
 * @param defaults Provides the error checkerboard.
 * @param fallback Image to use when the texture names no valid image index.
 * @return The image to bind.
 */
static AllocatedImage resolve_image(std::span<const AllocatedImage> images,
    const fastgltf::Optional<usize> &index, const DefaultResources &defaults,
    const AllocatedImage &fallback)
{
    if (!index.has_value() || *index >= images.size()) {
        return fallback;
    }
    const AllocatedImage &image = images[*index];
    return image.image != VK_NULL_HANDLE ? image : defaults.error_checkerboard_image;
}

/**
 * @brief Creates the materials, their uniform buffer and the descriptor pool they allocate from.
 * @param gpu Context to create the GPU resources with.
 * @param defaults Fallback textures and samplers for materials that don't specify them.
 * @param material_system Builds the material descriptor sets.
 * @param asset Parsed glTF asset.
 * @param images Images in glTF image order.
 * @param scene Receives the descriptor pool, the material buffer and the materials.
 */
void load_materials(const GpuContext &gpu, const DefaultResources &defaults,
    MaterialSystem &material_system, const fastgltf::Asset &asset,
    std::span<const AllocatedImage> images, LoadedGLTF &scene)

{
    scene.materials.reserve(asset.materials.size());

    std::vector<DescriptorAllocatorGrowable::PoolSizeRatio> sizes = {
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 3 }, { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 3 },
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1 }
    };
    scene.descriptor_pool.init(gpu.device, asset.materials.size(), sizes);

    // Create a buffer to hold the material data.
    scene.material_data_buffer = gpu.create_buffer(
        sizeof(GLTFMetallic_Roughness::MaterialConstants) * asset.materials.size(),
        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
    GLTFMetallic_Roughness::MaterialConstants *scene_material_constants =
        (GLTFMetallic_Roughness::MaterialConstants *)scene.material_data_buffer.info.pMappedData;

    // Load the materials.
    usize data_index = 0;
    for (const fastgltf::Material &mat : asset.materials) {
        std::shared_ptr<GLTFMaterial> new_mat = std::make_shared<GLTFMaterial>();
        scene.materials.push_back(new_mat);

        GLTFMetallic_Roughness::MaterialConstants constants;
        constants.color_factors.x = mat.pbrData.baseColorFactor[0];
        constants.color_factors.y = mat.pbrData.baseColorFactor[1];
        constants.color_factors.z = mat.pbrData.baseColorFactor[2];
        constants.color_factors.w = mat.pbrData.baseColorFactor[3];

        constants.metal_rough_factors.x = mat.pbrData.metallicFactor;
        constants.metal_rough_factors.y = mat.pbrData.roughnessFactor;

        // Write the material constants to the buffer.
        scene_material_constants[data_index] = constants;

        MaterialPass pass_type = MaterialPass::Opaque;
        if (mat.alphaMode == fastgltf::AlphaMode::Blend) {
            pass_type = MaterialPass::Transparent;
        }

        GLTFMetallic_Roughness::MaterialResources material_resources;
        // Default the material textures.
        material_resources.color_image = defaults.white_image;
        material_resources.color_sampler = defaults.sampler_linear;
        material_resources.metal_rough_image = defaults.white_image;
        material_resources.metal_rough_sampler = defaults.sampler_linear;

        material_resources.data_buffer = scene.material_data_buffer.buffer;
        material_resources.data_buffer_offset =
            data_index * sizeof(GLTFMetallic_Roughness::MaterialConstants);
        if (mat.pbrData.baseColorTexture.has_value()) {
            auto &tex = asset.textures[mat.pbrData.baseColorTexture.value().textureIndex];

            material_resources.color_image =
                resolve_image(images, tex.imageIndex, defaults, defaults.white_image);

            if (tex.samplerIndex.has_value() && *tex.samplerIndex < scene.samplers.size()) {
                usize sampler = *tex.samplerIndex;
                material_resources.color_sampler = scene.samplers[sampler];
            }
        }
        if (mat.pbrData.metallicRoughnessTexture.has_value()) {
            auto &tex = asset.textures[mat.pbrData.metallicRoughnessTexture.value().textureIndex];

            material_resources.metal_rough_image =
                resolve_image(images, tex.imageIndex, defaults, defaults.white_image);

            if (tex.samplerIndex.has_value() && *tex.samplerIndex < scene.samplers.size()) {
                usize sampler = *tex.samplerIndex;
                material_resources.metal_rough_sampler = scene.samplers[sampler];
            }
        }
        // Build the material.
        new_mat->data = material_system.get_type<GLTFMetallic_Roughness>().write_material(
            gpu.device, pass_type, material_resources, scene.descriptor_pool);

        data_index++;
    }
}

/**
 * @brief Uploads every glTF mesh to the GPU, with one index and one vertex buffer per mesh.
 *
 * @param gpu Context to upload the buffers with.
 * @param defaults Provides the material for primitives that don't name a valid one.
 * @param asset Parsed glTF asset.
 * @param materials Materials in glTF material order.
 * @return The meshes, in glTF mesh order.
 */
std::vector<std::shared_ptr<MeshAsset>> load_meshes(const GpuContext &gpu,
    const DefaultResources &defaults, const fastgltf::Asset &asset,
    std::span<const std::shared_ptr<GLTFMaterial>> materials)
{
    std::vector<std::shared_ptr<MeshAsset>> meshes;
    meshes.reserve(asset.meshes.size());

    // Reused across meshes so they don't reallocate for every mesh.
    std::vector<u32> indices;
    std::vector<Vertex> vertices;

    for (const fastgltf::Mesh &mesh : asset.meshes) {
        std::shared_ptr<MeshAsset> new_mesh = std::make_shared<MeshAsset>();
        new_mesh->name = mesh.name;

        indices.clear();
        vertices.clear();

        for (const fastgltf::Primitive &p : mesh.primitives) {
            // Check everything that can be missing before appending anything, so a skipped
            // primitive leaves no stray indices or vertices behind.
            auto position_it = p.findAttribute("POSITION");
            if (position_it == p.attributes.end() || !p.indicesAccessor.has_value()) {
                OH_LOG_ERROR(
                    "Skipping a primitive in mesh {} without positions or indices.", mesh.name);
                continue;
            }
            const fastgltf::Accessor &pos_accessor = asset.accessors[position_it->accessorIndex];
            if (pos_accessor.count == 0) {
                OH_LOG_ERROR("Skipping a primitive in mesh {} with no vertices.", mesh.name);
                continue;
            }
            const fastgltf::Accessor &index_accessor = asset.accessors[*p.indicesAccessor];

            GeoSurface new_surface;
            new_surface.start_index = (u32)indices.size();
            new_surface.count = (u32)index_accessor.count;

            const usize initial_vert = vertices.size();

            // Offset the indices so they point into this mesh's shared vertex buffer.
            indices.reserve(indices.size() + index_accessor.count);
            fastgltf::iterateAccessor<u32>(asset, index_accessor,
                [&](u32 idx) { indices.push_back(idx + (u32)initial_vert); });

            // Positions set every vertex, so give the optional attributes their defaults here.
            vertices.resize(vertices.size() + pos_accessor.count);
            fastgltf::iterateAccessorWithIndex<glm::vec3>(
                asset, pos_accessor, [&](glm::vec3 v, usize index) {
                    Vertex new_vert;
                    new_vert.position = v;
                    new_vert.normal = { 1.0f, 0.0f, 0.0f };
                    new_vert.color = glm::vec4 { 1.0f };
                    new_vert.uv_x = 0;
                    new_vert.uv_y = 0;
                    vertices[initial_vert + index] = new_vert;
                });

            auto normals_it = p.findAttribute("NORMAL");
            if (normals_it != p.attributes.end()) {
                const fastgltf::Accessor &norm_accessor =
                    asset.accessors[normals_it->accessorIndex];
                fastgltf::iterateAccessorWithIndex<glm::vec3>(asset, norm_accessor,
                    [&](glm::vec3 v, usize index) { vertices[initial_vert + index].normal = v; });
            }

            auto uv_it = p.findAttribute("TEXCOORD_0");
            if (uv_it != p.attributes.end()) {
                const fastgltf::Accessor &uv_accessor = asset.accessors[uv_it->accessorIndex];
                fastgltf::iterateAccessorWithIndex<glm::vec2>(
                    asset, uv_accessor, [&](glm::vec2 v, usize index) {
                        vertices[initial_vert + index].uv_x = v.x;
                        vertices[initial_vert + index].uv_y = v.y;
                    });
            }

            auto color_it = p.findAttribute("COLOR_0");
            if (color_it != p.attributes.end()) {
                const fastgltf::Accessor &color_accessor = asset.accessors[color_it->accessorIndex];
                fastgltf::iterateAccessorWithIndex<glm::vec4>(asset, color_accessor,
                    [&](glm::vec4 v, usize index) { vertices[initial_vert + index].color = v; });
            }

            const bool has_material =
                p.materialIndex.has_value() && *p.materialIndex < materials.size();
            new_surface.material =
                has_material ? &materials[*p.materialIndex]->data : &defaults.default_material;

            // Compute the bounds from the min/max of this surface's vertices.
            glm::vec3 min_pos = vertices[initial_vert].position;
            glm::vec3 max_pos = min_pos;
            for (usize i = initial_vert; i < vertices.size(); i++) {
                min_pos = glm::min(min_pos, vertices[i].position);
                max_pos = glm::max(max_pos, vertices[i].position);
            }
            new_surface.bounds.origin = (max_pos + min_pos) / 2.0f;
            new_surface.bounds.extents = (max_pos - min_pos) / 2.0f;
            new_surface.bounds.sphere_radius = glm::length(new_surface.bounds.extents);

            new_mesh->surfaces.push_back(new_surface);
        }

        // A mesh whose primitives were all skipped keeps null buffers and draws nothing.
        if (!new_mesh->surfaces.empty()) {
            new_mesh->mesh_buffers = gpu.upload_mesh(indices, vertices);
        }
        meshes.push_back(std::move(new_mesh));
    }

    return meshes;
}

/**
 * @brief Creates the scene nodes, links them into a hierarchy and computes their world transforms.
 * @param asset Parsed glTF asset.
 * @param meshes Meshes in glTF mesh order.
 * @return The root nodes, which own the rest of the hierarchy.
 */
std::vector<std::shared_ptr<Node>> build_node_hierarchy(
    const fastgltf::Asset &asset, std::span<const std::shared_ptr<MeshAsset>> meshes)
{
    // Every node in glTF order, so children can be linked by index.
    std::vector<std::shared_ptr<Node>> nodes;
    nodes.reserve(asset.nodes.size());

    for (const fastgltf::Node &node : asset.nodes) {
        std::shared_ptr<Node> new_node;

        // A node with a bad mesh index still becomes a plain node, so the indices of the nodes
        // after it stay valid and its children still render.
        if (node.meshIndex.has_value() && *node.meshIndex < meshes.size()) {
            auto mesh_node = std::make_shared<MeshNode>();
            mesh_node->mesh = meshes[*node.meshIndex];
            new_node = std::move(mesh_node);
        } else {
            if (node.meshIndex.has_value()) {
                OH_LOG_ERROR("Node {} refers to mesh {}, but the file has only {} meshes.",
                    node.name, *node.meshIndex, meshes.size());
            }
            new_node = std::make_shared<Node>();
        }

        nodes.push_back(new_node);

        auto on_matrix = [&](const fastgltf::math::fmat4x4 &matrix) {
            new_node->local_transform = glm::make_mat4(matrix.data());
        };

        auto on_trs = [&](const fastgltf::TRS &transform) {
            glm::vec3 tl(
                transform.translation[0], transform.translation[1], transform.translation[2]);
            glm::quat rot(transform.rotation[3], transform.rotation[0], transform.rotation[1],
                transform.rotation[2]); // w, x, y, z
            glm::vec3 sc(transform.scale[0], transform.scale[1], transform.scale[2]);

            glm::mat4 tm = glm::translate(glm::mat4(1.f), tl);
            glm::mat4 rm = glm::toMat4(rot);
            glm::mat4 sm = glm::scale(glm::mat4(1.f), sc);

            new_node->local_transform = tm * rm * sm;
        };

        std::visit(fastgltf::visitor { on_matrix, on_trs }, node.transform);
    }

    // Link every node to its children.
    for (usize i = 0; i < asset.nodes.size(); i++) {
        const fastgltf::Node &node = asset.nodes[i];
        const std::shared_ptr<Node> &scene_node = nodes[i];

        for (usize c : node.children) {
            if (c >= nodes.size()) {
                OH_LOG_ERROR("Node {} refers to child node {}, but the file has only {} nodes.",
                    node.name, c, nodes.size());
                continue;
            }
            scene_node->children.push_back(nodes[c]);
            nodes[c]->parent = scene_node;
        }
    }

    // The parentless nodes are the roots. Computing their world transforms also computes those of
    // every node below them.
    std::vector<std::shared_ptr<Node>> top_nodes;
    for (const auto &node : nodes) {
        if (node->parent.lock() == nullptr) {
            top_nodes.push_back(node);
            node->refresh_transform(glm::mat4(1.0f));
        }
    }

    return top_nodes;
}

std::unique_ptr<LoadedGLTF> load_gltf(const GpuContext &gpu, const DefaultResources &defaults,
    MaterialSystem &material_system, std::filesystem::path path)
{
    OH_LOG_INFO("Loading GLTF: {}", path.c_str());

    using clock = std::chrono::steady_clock;
    const clock::time_point load_start = clock::now();
    clock::time_point phase_start = load_start;
    auto lap_ms = [&phase_start]() {
        const clock::time_point now = clock::now();
        const f64 ms = std::chrono::duration<f64, std::milli>(now - phase_start).count();
        phase_start = now;
        return ms;
    };

    std::optional<fastgltf::Asset> asset = parse_gltf_file(path);
    if (!asset) {
        return nullptr;
    }
    const f64 parse_ms = lap_ms();

    const std::vector<ImageUsage> image_usage = collect_image_usage(*asset);

    auto scene = std::make_unique<LoadedGLTF>();
    scene->gpu = &gpu;
    scene->samplers = load_samplers(gpu.device, *asset);
    scene->images = load_images(gpu, *asset, image_usage, path);
    const f64 images_ms = lap_ms();

    load_materials(gpu, defaults, material_system, *asset, scene->images, *scene);
    const f64 materials_ms = lap_ms();

    scene->meshes = load_meshes(gpu, defaults, *asset, scene->materials);
    const f64 meshes_ms = lap_ms();

    scene->top_nodes = build_node_hierarchy(*asset, scene->meshes);
    const f64 nodes_ms = lap_ms();

    const f64 total_ms = std::chrono::duration<f64, std::milli>(phase_start - load_start).count();

    const usize loaded_image_count = std::count_if(scene->images.begin(), scene->images.end(),
        [](const AllocatedImage &img) { return img.image != VK_NULL_HANDLE; });
    OH_LOG_INFO("Loaded {} in {:.1f} ms: {}/{} images (parse {:.1f}, images {:.1f}, materials "
                "{:.1f}, meshes "
                "{:.1f}, nodes {:.1f}).",
        path.filename().c_str(), total_ms, loaded_image_count, scene->images.size(), parse_ms,
        images_ms, materials_ms, meshes_ms, nodes_ms);

    return scene;
}

void LoadedGLTF::draw(const glm::mat4 &top_matrix, DrawContext &ctx)
{
    for (const auto &node : top_nodes) {
        node->draw(top_matrix, ctx);
    }
}

void LoadedGLTF::clear_all()
{
    VkDevice dev = gpu->device;

    descriptor_pool.destroy_pools(dev);
    gpu->destroy_buffer(material_data_buffer);

    for (const auto &mesh : meshes) {
        gpu->destroy_buffer(mesh->mesh_buffers.index_buffer);
        gpu->destroy_buffer(mesh->mesh_buffers.vertex_buffer);
    }

    // Images that failed to load have null handles, and destroying those is a no-op.
    for (const AllocatedImage &image : images) {
        gpu->destroy_image(image);
    }

    for (auto &sampler : samplers) {
        vkDestroySampler(dev, sampler, nullptr);
    }
}

std::optional<AllocatedImage> load_image(const GpuContext &gpu, fastgltf::Asset &asset,
    fastgltf::Image &image, std::filesystem::path gltf_path)
{
    AllocatedImage new_image {};

    int width, height, n_channels;

    std::visit(
        fastgltf::visitor { [](auto &arg) {},
            [&](fastgltf::sources::URI &filepath) {
                assert(filepath.fileByteOffset == 0);
                assert(filepath.uri.isLocalPath());

                const std::string path = gltf_path.parent_path() / filepath.uri.fspath();
                unsigned char *data = stbi_load(path.c_str(), &width, &height, &n_channels, 4);
                if (data) {
                    VkExtent3D image_size;
                    image_size.width = (u32)width;
                    image_size.height = (u32)height;
                    image_size.depth = 1;

                    new_image = gpu.create_image(data, image_size, VK_FORMAT_R8G8B8A8_UNORM,
                        VK_IMAGE_USAGE_SAMPLED_BIT, true);

                    stbi_image_free(data);
                }
            },
            [&](fastgltf::sources::Vector &vector) {
                unsigned char *data = stbi_load_from_memory(
                    reinterpret_cast<const unsigned char *>(vector.bytes.data()),
                    static_cast<int>(vector.bytes.size()), &width, &height, &n_channels, 4);
                if (data) {
                    VkExtent3D image_size;
                    image_size.width = static_cast<u32>(width);
                    image_size.height = static_cast<u32>(height);
                    image_size.depth = 1;

                    new_image = gpu.create_image(data, image_size, VK_FORMAT_R8G8B8A8_UNORM,
                        VK_IMAGE_USAGE_SAMPLED_BIT, true);

                    stbi_image_free(data);
                }
            },
            [&](fastgltf::sources::BufferView &view) {
                auto &buffer_view = asset.bufferViews[view.bufferViewIndex];
                auto &buffer = asset.buffers[buffer_view.bufferIndex];

                const unsigned char *buffer_start = nullptr;

                std::visit(fastgltf::visitor {
                               [&](fastgltf::sources::Vector &vec) {
                                   buffer_start =
                                       reinterpret_cast<const unsigned char *>(vec.bytes.data());
                               },
                               [&](fastgltf::sources::Array &arr) {
                                   buffer_start =
                                       reinterpret_cast<const unsigned char *>(arr.bytes.data());
                               },
                               [](auto &arg) {} // Fallback for other potential types.
                           },
                    buffer.data);

                if (buffer_start != nullptr) {
                    const unsigned char *data_ptr = buffer_start + buffer_view.byteOffset;

                    unsigned char *data = stbi_load_from_memory(data_ptr,
                        static_cast<int>(buffer_view.byteLength), &width, &height, &n_channels, 4);

                    if (data) {
                        VkExtent3D image_size = { static_cast<u32>(width), static_cast<u32>(height),
                            1 };
                        new_image = gpu.create_image(data, image_size, VK_FORMAT_R8G8B8A8_UNORM,
                            VK_IMAGE_USAGE_SAMPLED_BIT, true);
                        stbi_image_free(data);
                    }
                }
            } },
        image.data);

    if (new_image.image == VK_NULL_HANDLE) {
        return {};
    }

    return new_image;
}

void MeshNode::draw(const glm::mat4 &top_matrix, DrawContext &ctx)
{
    glm::mat4 node_matrix = top_matrix * world_transform;

    for (auto &s : mesh->surfaces) {
        RenderObject def;
        def.index_count = s.count;
        def.first_index = s.start_index;
        def.index_buffer = mesh->mesh_buffers.index_buffer.buffer;
        def.material = s.material;
        def.bounds = s.bounds;
        def.transform = node_matrix;
        def.vertex_buffer_address = mesh->mesh_buffers.vertex_buffer_address;

        if (s.material->pass_type == MaterialPass::Transparent) {
            ctx.transparent_surfaces.push_back(def);
        } else {
            ctx.opaque_surfaces.push_back(def);
        }
    }

    Node::draw(top_matrix, ctx);
}