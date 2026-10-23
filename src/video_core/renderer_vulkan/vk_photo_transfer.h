// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <algorithm>
#include <vector>
#include "video_core/renderer_vulkan/vk_texture_runtime.h"

namespace Vulkan::PhotoTransfer {
struct Pixels {
    u32 width{},height{};
    std::vector<u8> bytes;
};
// Private photos store their full GPU dimensions at res_scale=1. Unlike native
// framebuffer downloads, they can exceed TextureRuntime's 16 MiB staging ring.
// Copy complete rows in bounded batches and consume each before reusing staging.
inline std::vector<u8> Read(TextureRuntime& runtime, Surface& image,
                           const Common::Rectangle<u32>& rect) {
    constexpr u32 BatchBytes = 8 * 1024 * 1024;
    if (image.pixel_format != VideoCore::PixelFormat::RGBA8 || image.res_scale != 1 ||
        image.width > 3840 || image.height > 6827 || rect.left >= rect.right ||
        rect.bottom >= rect.top || rect.right > image.width || rect.top > image.height)
        return {};
    const u32 row_bytes = rect.GetWidth() * 4;
    const u32 rows_per_batch = BatchBytes / row_bytes;
    std::vector<u8> pixels(std::size_t(row_bytes) * rect.GetHeight());
    for (u32 y = 0; y < rect.GetHeight();) {
        const u32 rows = std::min(rows_per_batch, rect.GetHeight() - y);
        const u32 bytes = rows * row_bytes;
        const auto staging = runtime.FindStaging(bytes, false);
        image.Download({.buffer_offset = staging.offset, .buffer_size = bytes,
            .texture_rect = {rect.left, rect.bottom + y + rows, rect.right, rect.bottom + y},
            .texture_level = 0}, staging);
        std::copy_n(staging.mapped.data(), bytes, pixels.data() + std::size_t(y) * row_bytes);
        y += rows;
    }
    return pixels;
}
}
