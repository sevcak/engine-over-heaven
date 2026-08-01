#include <vk_loader.h>

#include "fastgltf/types.hpp"
#include "stb_image.h"
#include <iostream>

#include "vk_renderer.hpp"
#include "vk_types.h"
#include <glm/gtx/quaternion.hpp>

#include <fastgltf/core.hpp>
#include <fastgltf/glm_element_traits.hpp>
#include <fastgltf/tools.hpp>

std::optional<AllocatedImage> load_image(
    VulkanRenderer *engine, fastgltf::Asset &asset, fastgltf::Image &image);

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

std::optional<std::vector<std::shared_ptr<MeshAsset>>> load_gltf_meshes(
    VulkanRenderer *engine, std::filesystem::path file_path)
{
    std::cout << "Loading GLTF: " << file_path << std::endl;

    auto data = fastgltf::GltfDataBuffer::FromPath(file_path);
    if (!data) {
        fmt::print("Failed to load glTF: {}\n", fastgltf::to_underlying(data.error()));
        return {};
    }

    constexpr auto gltf_options =
        fastgltf::Options::LoadGLBBuffers | fastgltf::Options::LoadExternalBuffers;

    fastgltf::Asset gltf;
    fastgltf::Parser parser {};

    auto load = parser.loadGltfBinary(data.get(), file_path.parent_path(), gltf_options);
    if (!load) {
        fmt::print("Failed to load glTF: {} \n", fastgltf::to_underlying(load.error()));
        return {};
    }

    gltf = std::move(load.get());

    std::vector<std::shared_ptr<MeshAsset>> meshes;

    std::vector<uint32_t> indices;
    std::vector<Vertex> vertices;
    for (fastgltf::Mesh &mesh : gltf.meshes) {
        MeshAsset new_mesh;

        new_mesh.name = mesh.name;

        indices.clear();
        vertices.clear();

        for (auto &&p : mesh.primitives) {
            GeoSurface new_surface;
            new_surface.start_index = (uint32_t)indices.size();
            new_surface.count = (uint32_t)gltf.accessors[p.indicesAccessor.value()].count;

            size_t initial_vert = vertices.size();

            {
                fastgltf::Accessor &index_accessor = gltf.accessors[p.indicesAccessor.value()];
                indices.reserve(indices.size() + index_accessor.count);

                fastgltf::iterateAccessor<std::uint32_t>(gltf, index_accessor,
                    [&](std::uint32_t index) { indices.push_back(index + initial_vert); });
            }

            {
                auto position_it = p.findAttribute("POSITION");
                if (position_it == p.attributes.end()) {
                    fmt::println("Didn't find position attribute.");
                    return {};
                }
                fastgltf::Accessor &pos_accessor = gltf.accessors[position_it->accessorIndex];

                vertices.resize(vertices.size() + pos_accessor.count);

                fastgltf::iterateAccessorWithIndex<glm::vec3>(
                    gltf, pos_accessor, [&](glm::vec3 v, size_t index) {
                        Vertex new_vertex;
                        new_vertex.position = v;
                        new_vertex.normal = { 1.0f, 0.0f, 0.0f };
                        new_vertex.color = glm::vec4 { 1.0f };
                        new_vertex.uv_x = 0;
                        new_vertex.uv_y = 0;
                        vertices[initial_vert + index] = new_vertex;
                    });
            }

            auto normals_it = p.findAttribute("NORMAL");
            if (normals_it != p.attributes.end()) {
                fastgltf::Accessor &norm_accessor = gltf.accessors[normals_it->accessorIndex];

                fastgltf::iterateAccessorWithIndex<glm::vec3>(gltf, norm_accessor,
                    [&](glm::vec3 v, size_t index) { vertices[initial_vert + index].normal = v; });
            }

            auto uv_it = p.findAttribute("TEXCOORD_0");
            if (uv_it != p.attributes.end()) {
                fastgltf::Accessor &uv_accessor = gltf.accessors[uv_it->accessorIndex];

                fastgltf::iterateAccessorWithIndex<glm::vec2>(
                    gltf, uv_accessor, [&](glm::vec2 v, size_t index) {
                        vertices[initial_vert + index].uv_x = v.x;
                        vertices[initial_vert + index].uv_y = v.y;
                    });
            }

            auto color_it = p.findAttribute("COLOR_0");
            if (color_it != p.attributes.end()) {
                fastgltf::Accessor &color_accessor = gltf.accessors[color_it->accessorIndex];

                fastgltf::iterateAccessorWithIndex<glm::vec4>(gltf, color_accessor,
                    [&](glm::vec4 v, size_t index) { vertices[initial_vert + index].color = v; });
            }

            new_mesh.surfaces.push_back(new_surface);
        }

        // Display the vertex normals.
        constexpr bool ovverride_colors = false;
        if (ovverride_colors) {
            for (Vertex &vertex : vertices) {
                vertex.color = glm::vec4(vertex.normal, 1.0f);
            }
        }

        new_mesh.mesh_buffers = engine->upload_mesh(indices, vertices);

        meshes.emplace_back(std::make_shared<MeshAsset>(std::move(new_mesh)));
    }

    return meshes;
}

std::optional<std::shared_ptr<LoadedGLTF>> load_gltf(
    VulkanRenderer *engine, std::string_view file_path)
{
    fmt::print("Loading GLTF: {}\n", file_path);

    std::shared_ptr<LoadedGLTF> scene = std::make_shared<LoadedGLTF>();
    scene->creator = engine;
    LoadedGLTF &file = *scene.get();

    fastgltf::Parser parser {};

    const auto gltf_options = fastgltf::Options::DontRequireValidAssetMember |
                              fastgltf::Options::AllowDouble | fastgltf::Options::LoadGLBBuffers |
                              fastgltf::Options::LoadExternalBuffers;
    // | fastgltf::Options::LoadExternalImages;

    auto data = fastgltf::GltfDataBuffer::FromPath(file_path);
    if (!data) {
        std::cerr << "Failed to load glTF data: " << fastgltf::to_underlying(data.error())
                  << std::endl;
        return {};
    }

    fastgltf::Asset gltf;

    std::filesystem::path path = file_path;

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

    std::vector<DescriptorAllocatorGrowable::PoolSizeRatio> sizes = {
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 3 }, { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 3 },
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1 }
    };

    file.descriptor_pool.init(engine->_device, gltf.materials.size(), sizes);

    // Load samplers.
    for (fastgltf::Sampler &sampler : gltf.samplers) {
        VkSamplerCreateInfo sampl = { .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
            .pNext = nullptr };
        sampl.maxLod = VK_LOD_CLAMP_NONE;
        sampl.minLod = 0;

        sampl.magFilter = extract_filter(sampler.magFilter.value_or(fastgltf::Filter::Nearest));
        sampl.minFilter = extract_filter(sampler.minFilter.value_or(fastgltf::Filter::Nearest));

        sampl.mipmapMode =
            extract_mipmap_mode(sampler.minFilter.value_or(fastgltf::Filter::Nearest));

        VkSampler new_sampler;
        vkCreateSampler(engine->_device, &sampl, nullptr, &new_sampler);

        file.samplers.push_back(new_sampler);
    }

    std::vector<std::shared_ptr<MeshAsset>> meshes;
    std::vector<std::shared_ptr<Node>> nodes;
    std::vector<AllocatedImage> images;
    std::vector<std::shared_ptr<GLTFMaterial>> materials;

    // Load all textures.
    for (fastgltf::Image &image : gltf.images) {
        std::optional<AllocatedImage> img = load_image(engine, gltf, image);

        if (img.has_value()) {
            images.push_back(*img);
            file.images[image.name.c_str()] = *img;
        } else {
            std::cout << "Failed to load texture " << image.name << std::endl;
            images.push_back(engine->_error_checkerboard_image);
        }
    }

    // Create a buffer to hold the material data.
    file.material_data_buffer = engine->create_buffer(
        sizeof(GLTFMetallic_Roughness::MaterialConstants) * gltf.materials.size(),
        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
    GLTFMetallic_Roughness::MaterialConstants *scene_material_constants =
        (GLTFMetallic_Roughness::MaterialConstants *)file.material_data_buffer.info.pMappedData;

    // Load the materials.
    int data_index = 0;
    for (fastgltf::Material &mat : gltf.materials) {
        std::shared_ptr<GLTFMaterial> new_mat = std::make_shared<GLTFMaterial>();
        materials.push_back(new_mat);
        file.materials[mat.name.c_str()];

        GLTFMetallic_Roughness::MaterialConstants constants;
        constants.color_factors.x = mat.pbrData.baseColorFactor[0];
        constants.color_factors.y = mat.pbrData.baseColorFactor[1];
        constants.color_factors.z = mat.pbrData.baseColorFactor[2];
        constants.color_factors.w = mat.pbrData.baseColorFactor[3];

        constants.metal_rough_factors.x = mat.pbrData.metallicFactor;
        constants.metal_rough_factors.y = mat.pbrData.roughnessFactor;

        // Write the material constants to the buffer.
        scene_material_constants[data_index] = constants;

        MaterialPass pass_type = MaterialPass::MainColor;
        if (mat.alphaMode == fastgltf::AlphaMode::Blend) {
            pass_type = MaterialPass::Transparent;
        }

        GLTFMetallic_Roughness::MaterialResources material_resources;
        // Default the material textures.
        material_resources.color_image = engine->_white_image;
        material_resources.color_sampler = engine->_default_sampler_linear;
        material_resources.metal_rough_image = engine->_white_image;
        material_resources.metal_rough_smapler = engine->_default_sampler_linear;

        material_resources.data_buffer = file.material_data_buffer.buffer;
        material_resources.data_buffer_offset =
            data_index * sizeof(GLTFMetallic_Roughness::MaterialConstants);
        if (mat.pbrData.baseColorTexture.has_value()) {
            size_t img =
                gltf.textures[mat.pbrData.baseColorTexture.value().textureIndex].imageIndex.value();
            size_t sampler = gltf.textures[mat.pbrData.baseColorTexture.value().textureIndex]
                                 .samplerIndex.value();

            material_resources.color_image = images[img];
            material_resources.color_sampler = file.samplers[sampler];
        }
        // Build the material.
        new_mat->data = engine->_metal_rough_material.write_material(
            engine->_device, pass_type, material_resources, file.descriptor_pool);

        data_index++;
    }

    std::vector<uint32_t> indices;
    std::vector<Vertex> vertices;
    for (fastgltf::Mesh &mesh : gltf.meshes) {
        std::shared_ptr<MeshAsset> new_mesh = std::make_shared<MeshAsset>();
        meshes.push_back(new_mesh);
        file.meshes[mesh.name.c_str()] = new_mesh;
        new_mesh->name = mesh.name;

        indices.clear();
        vertices.clear();

        for (auto &p : mesh.primitives) {
            GeoSurface new_surface;
            new_surface.start_index = (uint32_t)indices.size();
            new_surface.count = (uint32_t)gltf.accessors[p.indicesAccessor.value()].count;

            size_t initial_vert = vertices.size();

            // Load indices.
            {
                const auto &index_accessor = gltf.accessors[p.indicesAccessor.value()];
                indices.reserve(indices.size() + index_accessor.count);

                fastgltf::iterateAccessor<std::uint32_t>(gltf, index_accessor,
                    [&](std::uint32_t idx) { indices.push_back(idx + initial_vert); });
            }

            // Load vertex positions.
            {
                auto position_it = p.findAttribute("POSITION");
                if (position_it == p.attributes.end()) {
                    fmt::println("Didn't find position attribute.");
                    return {};
                }
                const auto &pos_accessor = gltf.accessors[position_it->accessorIndex];
                vertices.resize(vertices.size() + pos_accessor.count);

                fastgltf::iterateAccessorWithIndex<glm::vec3>(
                    gltf, pos_accessor, [&](glm::vec3 v, size_t index) {
                        Vertex new_vert;
                        new_vert.position = v;
                        new_vert.normal = { 1.0f, 0.0f, 0.0f };
                        new_vert.color = glm::vec4 { 1.0f };
                        new_vert.uv_x = 0;
                        new_vert.uv_y = 0;
                        vertices[initial_vert + index] = new_vert;
                    });
            }

            // Load vertex normals.
            auto normals_it = p.findAttribute("NORMAL");
            if (normals_it != p.attributes.end()) {
                const auto &norm_accessor = gltf.accessors[normals_it->accessorIndex];
                fastgltf::iterateAccessorWithIndex<glm::vec3>(gltf, norm_accessor,
                    [&](glm::vec3 v, size_t index) { vertices[initial_vert + index].normal = v; });
            }

            // Load UVs.
            auto uv_it = p.findAttribute("TEXCOORD_0");
            if (uv_it != p.attributes.end()) {
                const auto &uv_accessor = gltf.accessors[uv_it->accessorIndex];
                fastgltf::iterateAccessorWithIndex<glm::vec2>(
                    gltf, uv_accessor, [&](glm::vec2 v, size_t index) {
                        vertices[initial_vert + index].uv_x = v.x;
                        vertices[initial_vert + index].uv_y = v.y;
                    });
            }

            // Load vertex colors.
            auto color_it = p.findAttribute("COLOR_0");
            if (color_it != p.attributes.end()) {
                const auto &color_accessor = gltf.accessors[color_it->accessorIndex];
                fastgltf::iterateAccessorWithIndex<glm::vec4>(gltf, color_accessor,
                    [&](glm::vec4 v, size_t index) { vertices[initial_vert + index].color = v; });
            }

            if (p.materialIndex.has_value()) {
                new_surface.material = materials[p.materialIndex.value()];
            } else {
                new_surface.material = materials[0];
            }

            // Loop the vertices of this surface, find min/max bounds.
            glm::vec3 min_pos = vertices[initial_vert].position;
            glm::vec3 max_pos = min_pos;
            for (int i = initial_vert; i < vertices.size(); i++) {
                min_pos = glm::min(min_pos, vertices[i].position);
                max_pos = glm::max(max_pos, vertices[i].position);
            }
            // Compute the origin and extents from min/max, use extent length for radius.
            new_surface.bounds.origin = (max_pos + min_pos) / 2.0f;
            new_surface.bounds.extents = (max_pos - min_pos) / 2.0f;
            new_surface.bounds.sphere_radius = glm::length(new_surface.bounds.extents);

            new_mesh->surfaces.push_back(new_surface);
        }

        new_mesh->mesh_buffers = engine->upload_mesh(indices, vertices);
    }

    // Load all nodes and their meshes.
    for (fastgltf::Node &node : gltf.nodes) {
        std::shared_ptr<Node> new_node;

        if (node.meshIndex.has_value()) {
            new_node = std::make_shared<MeshNode>();
            std::static_pointer_cast<MeshNode>(new_node)->mesh = meshes[*node.meshIndex];
        } else {
            new_node = std::make_shared<Node>();
        }

        nodes.push_back(new_node);
        file.nodes[node.name.c_str()];

        auto on_matrix = [&](const fastgltf::math::fmat4x4 &matrix) {
            memcpy(&new_node->local_transform, matrix.data(), sizeof(matrix));
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

    // Set up the transform hierarchy.
    for (size_t i = 0; i < gltf.nodes.size(); i++) {
        fastgltf::Node &node = gltf.nodes[i];
        std::shared_ptr<Node> &scene_node = nodes[i];

        for (auto &c : node.children) {
            scene_node->children.push_back(nodes[c]);
            nodes[c]->parent = scene_node;
        }
    }

    // Find the root nodes with no parents.
    for (const auto &node : nodes) {
        if (node->parent.lock() == nullptr) {
            file.top_nodes.push_back(node);
            node->refresh_transform(glm::mat4(1.0f));
        }
    }

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
    VkDevice dev = creator->_device;

    descriptor_pool.destroy_pools(dev);
    creator->destroy_buffer(material_data_buffer);

    for (auto &[k, mesh] : meshes) {
        creator->destroy_buffer(mesh->mesh_buffers.index_buffer);
        creator->destroy_buffer(mesh->mesh_buffers.vertex_buffer);
    }

    for (auto &[k, image] : images) {
        if (image.image == creator->_error_checkerboard_image.image) {
            // Don't destroy the default images.
            continue;
        }
        creator->destroy_image(image);
    }

    for (auto &sampler : samplers) {
        vkDestroySampler(dev, sampler, nullptr);
    }
}

std::optional<AllocatedImage> load_image(
    VulkanRenderer *engine, fastgltf::Asset &asset, fastgltf::Image &image)
{
    AllocatedImage new_image {};

    int width, height, n_channels;

    std::visit(
        fastgltf::visitor { [](auto &arg) {},
            [&](fastgltf::sources::URI &filepath) {
                assert(filepath.fileByteOffset == 0);
                assert(filepath.uri.isLocalPath());

                const std::string path { filepath.uri.path().begin(), filepath.uri.path().end() };
                unsigned char *data = stbi_load(path.c_str(), &width, &height, &n_channels, 4);
                if (data) {
                    VkExtent3D image_size;
                    image_size.width = (uint32_t)width;
                    image_size.height = (uint32_t)height;
                    image_size.depth = 1;

                    new_image = engine->create_image(data, image_size, VK_FORMAT_R8G8B8A8_UNORM,
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
                    image_size.width = static_cast<uint32_t>(width);
                    image_size.height = static_cast<uint32_t>(height);
                    image_size.depth = 1;

                    new_image = engine->create_image(data, image_size, VK_FORMAT_R8G8B8A8_UNORM,
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
                        VkExtent3D image_size = { static_cast<uint32_t>(width),
                            static_cast<uint32_t>(height), 1 };
                        new_image = engine->create_image(data, image_size, VK_FORMAT_R8G8B8A8_UNORM,
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
        def.material = &s.material->data;
        def.bounds = s.bounds;
        def.transform = node_matrix;
        def.vertex_buffer_address = mesh->mesh_buffers.vertex_buffer_address;

        if (s.material->data.pass_type == MaterialPass::Transparent) {
            ctx.transparent_surfaces.push_back(def);
        } else {
            ctx.opaque_surfaces.push_back(def);
        }
    }

    Node::draw(top_matrix, ctx);
}