#pragma once

/**
 * @file
 * Which images of a glTF file the renderer consumes, and whether each one holds sRGB color or
 * linear data.
 */

#include <core/types.hpp>
#include <string>
#include <vector>

namespace fastgltf
{
class Asset;
struct Image;
}

/** How the renderer uses a glTF image, which decides whether and how it is loaded. */
enum class ImageUsage : u8
{
    /** No material slot the renderer reads refers to the image, so it isn't loaded. */
    Unused,
    /** sRGB-encoded color, e.g. base color. Sampled through an sRGB format. */
    Color,
    /** Linear data, e.g. metalness and roughness. Sampled through a UNORM format. */
    Linear
};

/**
 * @brief Determines how the renderer uses each image in a glTF file.
 *
 * Walks every material, so images referenced only by material variants count as used. References
 * to textures or images that don't exist are skipped. An image used both as color and as linear
 * data keeps its first usage, and a warning is logged.
 *
 * @param asset Parsed glTF asset.
 * @return One usage per image, in `asset.images` order.
 */
std::vector<ImageUsage> collect_image_usage(const fastgltf::Asset &asset);

/**
 * @brief Describes a glTF image for log messages.
 *
 * Most exporters leave out the optional `name`, so this falls back to the URI, and to the index
 * for images embedded in a buffer.
 *
 * @param image The image to describe.
 * @param index The image's index in `asset.images`.
 * @return The name if the image has one, otherwise its URI, otherwise `#<index>`.
 */
std::string describe_image(const fastgltf::Image &image, usize index);
