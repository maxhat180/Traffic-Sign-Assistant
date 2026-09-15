#include "image_adapter.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>

extern "C" bool frame_from_bgr(const unsigned char *pixels, size_t width,
                                size_t height, size_t row_stride, Frame *out,
                                const char **error)
{
    if (error == nullptr) {
        return false;
    }
    *error = "invalid BGR image or destination";
    if (pixels == nullptr || out == nullptr || width == 0 || height == 0 ||
        out->pixels != nullptr || out->width != 0 || out->height != 0 ||
        out->max_value != 0 || width > std::numeric_limits<size_t>::max() / 3U ||
        row_stride < width * 3U || width > std::numeric_limits<size_t>::max() / height ||
        width * height > std::numeric_limits<size_t>::max() / 3U) {
        return false;
    }

    Frame result = {width, height, 255U, nullptr};
    result.pixels = static_cast<unsigned char *>(std::malloc(width * height * 3U));
    if (result.pixels == nullptr) {
        *error = "could not allocate RGB frame";
        return false;
    }
    for (size_t y = 0; y < height; ++y) {
        const unsigned char *source = pixels + y * row_stride;
        unsigned char *destination = result.pixels + y * width * 3U;
        for (size_t x = 0; x < width; ++x) {
            destination[x * 3U] = source[x * 3U + 2U];
            destination[x * 3U + 1U] = source[x * 3U + 1U];
            destination[x * 3U + 2U] = source[x * 3U];
        }
    }
    *out = result;
    *error = nullptr;
    return true;
}
