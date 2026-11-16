#include "gltf_image_usage.hpp"

#include <fastgltf/types.hpp>
#include <logger/logger.hpp>

std::vector<ImageUsage> collect_image_usage(const fastgltf::Asset &asset)
{
    std::vector<ImageUsage> usage(asset.images.size(), ImageUsage::Unused);

    // Marks the image behind one material texture slot. `slot` is `auto`, because some slots
    // (e.g. `normalTexture`) hold subclasses of `fastgltf::TextureInfo`.
    auto mark = [&](const auto &slot, ImageUsage slot_usage) {
        if (!slot.has_value() || slot->textureIndex >= asset.textures.size()) {
            return;
        }
        const fastgltf::Optional<usize> &image_index =
            asset.textures[slot->textureIndex].imageIndex;
        if (!image_index.has_value() || *image_index >= usage.size()) {
            return;
        }

        ImageUsage &current = usage[*image_index];
        if (current == ImageUsage::Unused) {
            current = slot_usage;
        } else if (current != slot_usage) {
            OH_LOG_WARN("Image {} is used as both sRGB color and linear data. Keeping its first "
                        "usage.",
                describe_image(asset.images[*image_index], *image_index));
        }
    };

    // These are the texture slots the material shader reads.
    // TODO:When the shader starts reading another slot (e.g. `normalTexture`), add it here.
    for (const fastgltf::Material &mat : asset.materials) {
        mark(mat.pbrData.baseColorTexture, ImageUsage::Color);
        mark(mat.pbrData.metallicRoughnessTexture, ImageUsage::Linear);
    }

    return usage;
}

std::string describe_image(const fastgltf::Image &image, usize index)
{
    if (!image.name.empty()) {
        return std::string(image.name);
    }
    if (const auto *uri = std::get_if<fastgltf::sources::URI>(&image.data)) {
        return std::string(uri->uri.string());
    }
    return fmt::format("#{}", index);
}
