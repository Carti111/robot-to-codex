#pragma once
#include <stddef.h>
#include <stdint.h>

// Camera RGB565 arrives byte-swapped. Row flipping is local to the preview caller.
inline void CopyRgb565Preview(uint16_t* target, const uint16_t* source, size_t width, size_t height, bool flip_vertical, bool mirror_horizontal = false) {
    for (size_t y = 0; y < height; ++y) {
        size_t source_y = flip_vertical ? height - 1 - y : y;
        for (size_t x = 0; x < width; ++x) {
            size_t source_x = mirror_horizontal ? width - 1 - x : x;
            target[y * width + x] = __builtin_bswap16(source[source_y * width + source_x]);
        }
    }
}
