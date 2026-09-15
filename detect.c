#include "detect.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

DetectorConfig detector_defaults(void)
{
    return (DetectorConfig){40, 130, 120, 12, 2000, 150, 800, true};
}

void detection_destroy(Detection *detection)
{
    frame_destroy(&detection->raw_mask);
    frame_destroy(&detection->clean_mask);
    free(detection->boxes);
    *detection = (Detection){0};
}

/* Truncated 3x3 neighborhoods keep image-edge regions from artificial erosion. */
static unsigned int neighbors(const unsigned char *mask, size_t w, size_t h,
                              size_t x, size_t y, unsigned int *total)
{
    const size_t left = x == 0 ? 0 : x - 1U;
    const size_t top = y == 0 ? 0 : y - 1U;
    const size_t right = x + 1U < w ? x + 1U : x;
    const size_t bottom = y + 1U < h ? y + 1U : y;
    unsigned int count = 0;
    *total = 0;
    for (size_t yy = top; yy <= bottom; ++yy) {
        for (size_t xx = left; xx <= right; ++xx) {
            count += mask[yy * w + xx] != 0 ? 1U : 0U;
            ++*total;
        }
    }
    return count;
}

static void clean_mask(unsigned char *mask, unsigned char *temp, size_t w, size_t h)
{
    /* Remove foreground with no neighbor, retaining even thin connected borders. */
    for (size_t y = 0; y < h; ++y) {
        for (size_t x = 0; x < w; ++x) {
            unsigned int total;
            temp[y * w + x] = (unsigned char)(mask[y * w + x] != 0 &&
                neighbors(mask, w, h, x, y, &total) > 1U);
        }
    }
    /* Closing = dilation then erosion; fills small gaps without an opening. */
    for (size_t y = 0; y < h; ++y) {
        for (size_t x = 0; x < w; ++x) {
            unsigned int total;
            mask[y * w + x] = (unsigned char)(neighbors(temp, w, h, x, y, &total) != 0);
        }
    }
    for (size_t y = 0; y < h; ++y) {
        for (size_t x = 0; x < w; ++x) {
            unsigned int total;
            const unsigned int count = neighbors(mask, w, h, x, y, &total);
            temp[y * w + x] = (unsigned char)(count == total);
        }
    }
    memcpy(mask, temp, w * h);
}

static bool plausible(const Candidate *b, const DetectorConfig *c)
{
    const double aspect = b->width >= b->height ?
        (double)b->width / (double)b->height : (double)b->height / (double)b->width;
    const double fill = (double)b->area / ((double)b->width * (double)b->height);
    return b->area >= c->min_area && b->width >= c->min_side &&
           b->height >= c->min_side && aspect <= (double)c->max_aspect / 1000.0 &&
           fill >= (double)c->min_fill / 1000.0 && fill <= (double)c->max_fill / 1000.0;
}

bool detect_signs(const Frame *source, const DetectorConfig *config,
                  Detection *out, const char **error)
{
    *error = "invalid detector input or configuration";
    if (source == NULL || config == NULL || out == NULL || source->pixels == NULL ||
        source->width == 0 || source->height == 0 || source->max_value == 0 ||
        source->max_value > 255U || source->width > SIZE_MAX / source->height ||
        out->raw_mask.pixels != NULL || out->clean_mask.pixels != NULL ||
        out->boxes != NULL || out->count != 0 ||
        config->red_min > 255U || config->red_ratio < 101U || config->red_ratio > 1000U ||
        config->min_area == 0 || config->min_side == 0 || config->max_aspect < 1000U ||
        config->max_aspect > 10000U || config->min_fill > config->max_fill ||
        config->max_fill > 1000U) {
        return false;
    }
    const size_t w = source->width;
    const size_t h = source->height;
    const size_t n = w * h;
    if (n > SIZE_MAX / 3U || n > SIZE_MAX / sizeof(size_t)) {
        *error = "detector allocation size overflow";
        return false;
    }
    Detection result = {0};
    unsigned char *mask = malloc(n);
    unsigned char *scratch = malloc(n);
    size_t *queue = malloc(n * sizeof *queue);
    result.raw_mask = (Frame){w, h, 255, malloc(n * 3U)};
    result.clean_mask = (Frame){w, h, 255, malloc(n * 3U)};
    if (mask == NULL || scratch == NULL || queue == NULL ||
        result.raw_mask.pixels == NULL || result.clean_mask.pixels == NULL) {
        *error = "could not allocate detection buffers";
        goto fail;
    }
    for (size_t i = 0; i < n; ++i) {
        const uint32_t r = source->pixels[i * 3U];
        const uint32_t g = source->pixels[i * 3U + 1U];
        const uint32_t b = source->pixels[i * 3U + 2U];
        if (r > source->max_value || g > source->max_value || b > source->max_value) {
            *error = "pixel exceeds maximum color value";
            goto fail;
        }
        mask[i] = (unsigned char)(r * 255U >= config->red_min * source->max_value &&
            r > 0 && r * 100U >= config->red_ratio * g && r * 100U >= config->red_ratio * b);
        memset(result.raw_mask.pixels + i * 3U, mask[i] != 0 ? 255 : 0, 3U);
    }
    if (config->cleanup) {
        clean_mask(mask, scratch, w, h);
    }
    for (size_t i = 0; i < n; ++i) {
        memset(result.clean_mask.pixels + i * 3U, mask[i] != 0 ? 255 : 0, 3U);
    }
    size_t capacity = 0;
    /* Consuming mask on enqueue visits each pixel once; queue cannot exceed n. */
    for (size_t seed = 0; seed < n; ++seed) {
        if (mask[seed] == 0) {
            continue;
        }
        ++result.components;
        size_t head = 0;
        size_t tail = 1;
        queue[0] = seed;
        mask[seed] = 0;
        size_t min_x = seed % w, max_x = min_x;
        size_t min_y = seed / w, max_y = min_y;
        while (head < tail) {
            const size_t position = queue[head++];
            const size_t x = position % w, y = position / w;
            if (x < min_x) { min_x = x; }
            if (x > max_x) { max_x = x; }
            if (y < min_y) { min_y = y; }
            if (y > max_y) { max_y = y; }
            const size_t left = x == 0 ? 0 : x - 1U;
            const size_t top = y == 0 ? 0 : y - 1U;
            const size_t right = x + 1U < w ? x + 1U : x;
            const size_t bottom = y + 1U < h ? y + 1U : y;
            for (size_t yy = top; yy <= bottom; ++yy) {
                for (size_t xx = left; xx <= right; ++xx) {
                    const size_t next = yy * w + xx;
                    if (mask[next] != 0) {
                        mask[next] = 0;
                        queue[tail++] = next;
                    }
                }
            }
        }
        const Candidate box = {min_x, min_y, max_x - min_x + 1U, max_y - min_y + 1U, tail};
        if (!plausible(&box, config)) {
            continue;
        }
        if (result.count == capacity) {
            const size_t limit = SIZE_MAX / sizeof *result.boxes;
            if (capacity > (limit - 8U) / 2U) {
                *error = "too many candidates";
                goto fail;
            }
            const size_t next_capacity = capacity * 2U + 8U;
            Candidate *grown = realloc(result.boxes, next_capacity * sizeof *grown);
            if (grown == NULL) {
                *error = "could not allocate candidate boxes";
                goto fail;
            }
            result.boxes = grown;
            capacity = next_capacity;
        }
        result.boxes[result.count++] = box;
    }
    free(mask);
    free(scratch);
    free(queue);
    *out = result;
    *error = NULL;
    return true;
fail:
    free(mask);
    free(scratch);
    free(queue);
    detection_destroy(&result);
    return false;
}

static bool save_frame(const char *path, const Frame *frame, const char **error)
{
    FILE *file = fopen(path, "wb");
    if (file == NULL) {
        *error = "could not open detection output; parent directory must exist";
        return false;
    }
    const bool written = frame_write_ppm(file, frame, error);
    if (fclose(file) != 0) {
        *error = "could not close detection output";
        return false;
    }
    return written;
}

bool detection_save(const Frame *source, const Detection *detection,
                    const char *prefix, const char **error)
{
    const size_t length = strlen(prefix);
    if (length > SIZE_MAX - 80U) {
        *error = "output prefix too long";
        return false;
    }
    char *path = malloc(length + 80U);
    Frame annotated = {0};
    Frame crop = {0};
    if (path == NULL) {
        *error = "could not allocate output path";
        return false;
    }
    (void)snprintf(path, length + 80U, "%s-raw.ppm", prefix);
    if (!save_frame(path, &detection->raw_mask, error)) { goto fail; }
    (void)snprintf(path, length + 80U, "%s-clean.ppm", prefix);
    if (!save_frame(path, &detection->clean_mask, error)) { goto fail; }
    if (!frame_resize(source, source->width, source->height, &annotated, error)) { goto fail; }
    for (size_t i = 0; i < detection->count; ++i) {
        const Candidate *box = &detection->boxes[i];
        for (size_t y = box->y; y < box->y + box->height; ++y) {
            for (size_t x = box->x; x < box->x + box->width; ++x) {
                if (y == box->y || y == box->y + box->height - 1U ||
                    x == box->x || x == box->x + box->width - 1U) {
                    unsigned char *pixel = annotated.pixels + (y * source->width + x) * 3U;
                    pixel[0] = (unsigned char)source->max_value;
                    pixel[1] = (unsigned char)source->max_value;
                    pixel[2] = 0;
                }
            }
        }
        size_t margin = (box->width > box->height ? box->width : box->height) / 10U;
        if (margin < 2U) { margin = 2U; }
        const size_t left = box->x > margin ? box->x - margin : 0;
        const size_t top = box->y > margin ? box->y - margin : 0;
        const size_t right_edge = box->x + box->width;
        const size_t bottom_edge = box->y + box->height;
        const size_t right = source->width - right_edge < margin ? source->width : right_edge + margin;
        const size_t bottom = source->height - bottom_edge < margin ? source->height : bottom_edge + margin;
        crop = (Frame){right - left, bottom - top, source->max_value, NULL};
        crop.pixels = malloc(crop.width * crop.height * 3U);
        if (crop.pixels == NULL) {
            *error = "could not allocate candidate crop";
            goto fail;
        }
        for (size_t y = 0; y < crop.height; ++y) {
            memcpy(crop.pixels + y * crop.width * 3U,
                   source->pixels + ((top + y) * source->width + left) * 3U, crop.width * 3U);
        }
        (void)snprintf(path, length + 80U, "%s-crop-%zu.ppm", prefix, i);
        if (!save_frame(path, &crop, error)) { goto fail; }
        frame_destroy(&crop);
    }
    (void)snprintf(path, length + 80U, "%s-boxes.ppm", prefix);
    if (!save_frame(path, &annotated, error)) { goto fail; }
    (void)snprintf(path, length + 80U, "%s-boxes.csv", prefix);
    FILE *csv = fopen(path, "w");
    if (csv == NULL) {
        *error = "could not open candidate CSV";
        goto fail;
    }
    bool ok = fprintf(csv, "id,x,y,width,height,area\n") >= 0;
    for (size_t i = 0; i < detection->count && ok; ++i) {
        const Candidate *b = &detection->boxes[i];
        ok = fprintf(csv, "%zu,%zu,%zu,%zu,%zu,%zu\n", i, b->x, b->y,
                     b->width, b->height, b->area) >= 0;
    }
    if (fclose(csv) != 0) { ok = false; }
    if (!ok) {
        *error = "could not write candidate CSV";
        goto fail;
    }
    free(path);
    frame_destroy(&annotated);
    *error = NULL;
    return true;
fail:
    free(path);
    frame_destroy(&annotated);
    frame_destroy(&crop);
    return false;
}
