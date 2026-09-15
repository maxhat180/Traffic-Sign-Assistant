#include "frame.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static bool valid_frame(const Frame *frame, const char **error)
{
    *error = NULL;
    if (frame == NULL || frame->pixels == NULL || frame->width == 0 ||
        frame->height == 0 || frame->max_value == 0 || frame->max_value > 255U ||
        frame->width > SIZE_MAX / frame->height ||
        frame->width * frame->height > SIZE_MAX / 3U) {
        *error = "invalid RGB frame";
        return false;
    }
    const size_t count = frame->width * frame->height * 3U;
    for (size_t i = 0; i < count; ++i) {
        if (frame->pixels[i] > frame->max_value) {
            *error = "pixel channel exceeds the maximum color value";
            return false;
        }
    }
    return true;
}

bool frame_write_ppm(FILE *file, const Frame *frame, const char **error)
{
    if (!valid_frame(frame, error)) {
        return false;
    }
    const size_t bytes = frame->width * frame->height * 3U;
    if (fprintf(file, "P6\n%zu %zu\n%u\n", frame->width, frame->height,
                frame->max_value) < 0 ||
        fwrite(frame->pixels, 1U, bytes, file) != bytes || fflush(file) == EOF) {
        *error = "could not write PPM image";
        return false;
    }
    return true;
}

bool frame_grayscale(Frame *frame, const char **error)
{
    if (!valid_frame(frame, error)) {
        return false;
    }
    const size_t bytes = frame->width * frame->height * 3U;
    for (size_t i = 0; i < bytes; i += 3U) {
        /* uint32_t also keeps the weighted sum safe on 16-bit-int targets. */
        const uint32_t sum = UINT32_C(299) * frame->pixels[i] +
                             UINT32_C(587) * frame->pixels[i + 1U] +
                             UINT32_C(114) * frame->pixels[i + 2U];
        const unsigned char gray = (unsigned char)((sum + 500U) / 1000U);
        frame->pixels[i] = gray;
        frame->pixels[i + 1U] = gray;
        frame->pixels[i + 2U] = gray;
    }
    return true;
}

/* Advance floor(i * source / target) without multiplying large dimensions. */
static void advance_axis(size_t *coordinate, size_t *remainder,
                         size_t source, size_t target)
{
    *coordinate += source / target;
    const size_t increment = source % target;
    if (*remainder >= target - increment) {
        *remainder -= target - increment;
        ++*coordinate;
    } else {
        *remainder += increment;
    }
}

bool frame_resize(const Frame *src, size_t width, size_t height,
                  Frame *out, const char **error)
{
    if (!valid_frame(src, error)) {
        return false;
    }
    if (out == NULL || out == src || out->pixels != NULL || out->width != 0 ||
        out->height != 0 || out->max_value != 0) {
        *error = "resize destination must be a distinct empty frame";
        return false;
    }
    if (width == 0 || height == 0 || width > SIZE_MAX / height ||
        width * height > SIZE_MAX / 3U) {
        *error = "invalid or overflowing resize dimensions";
        return false;
    }
    Frame result = {width, height, src->max_value, NULL};
    result.pixels = malloc(width * height * 3U);
    if (result.pixels == NULL) {
        *error = "could not allocate resized frame";
        return false;
    }
    size_t sy = 0;
    size_t y_remainder = 0;
    for (size_t y = 0; y < height; ++y) {
        size_t sx = 0;
        size_t x_remainder = 0;
        for (size_t x = 0; x < width; ++x) {
            memcpy(result.pixels + (y * width + x) * 3U,
                   src->pixels + (sy * src->width + sx) * 3U, 3U);
            advance_axis(&sx, &x_remainder, src->width, width);
        }
        advance_axis(&sy, &y_remainder, src->height, height);
    }
    *out = result;
    return true;
}
