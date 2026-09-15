#include "frame.h"

#include <ctype.h>
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>

_Static_assert(CHAR_BIT == 8, "PPM loader requires eight-bit bytes");

/* Used only before header numbers, never for binary pixel data. */
static int next_header_character(FILE *file)
{
    int c;
    for (;;) {
        c = fgetc(file);
        if (c == EOF) {
            return EOF;
        }
        if (isspace((unsigned char)c)) {
            continue;
        }
        if (c != '#') {
            return c;
        }
        do {
            c = fgetc(file);
        } while (c != EOF && c != '\n' && c != '\r');
        if (c == EOF) {
            return EOF;
        }
    }
}

static bool read_number(FILE *file, size_t *value)
{
    int c = next_header_character(file);
    if (c < '0' || c > '9') {
        return false;
    }
    size_t number = 0;
    do {
        const size_t digit = (size_t)(c - '0');
        if (number > (SIZE_MAX - digit) / 10U) {
            return false;
        }
        number = number * 10U + digit;
        c = fgetc(file);
    } while (c >= '0' && c <= '9');
    if (c == EOF || (!isspace((unsigned char)c) && c != '#')) {
        return false;
    }
    /* Leave the delimiter for the next header read, or the raster boundary. */
    if (ungetc(c, file) == EOF) {
        return false;
    }
    *value = number;
    return true;
}

bool frame_read_ppm(FILE *file, Frame *out, const char **error)
{
    Frame frame = {0};
    size_t max_value = 0;
    unsigned char magic[2];
    *error = NULL;
    if (fread(magic, 1U, sizeof magic, file) != sizeof magic ||
        magic[0] != 'P' || magic[1] != '6') {
        *error = "expected a binary PPM (P6) signature";
        goto fail;
    }
    const int separator = fgetc(file);
    if (separator == EOF || !isspace((unsigned char)separator)) {
        *error = "expected whitespace after P6";
        goto fail;
    }
    if (!read_number(file, &frame.width) ||
        !read_number(file, &frame.height) ||
        !read_number(file, &max_value)) {
        *error = "invalid, missing, or overflowing header number";
        goto fail;
    }
    if (frame.width == 0 || frame.height == 0) {
        *error = "width and height must be positive";
        goto fail;
    }
    if (max_value == 0 || max_value > 255U) {
        *error = "supported maximum color values are 1 through 255 (8-bit RGB)";
        goto fail;
    }
    frame.max_value = (unsigned int)max_value;
    /* Consume exactly one byte; additional whitespace may be pixel data. */
    const int raster_separator = fgetc(file);
    if (raster_separator == EOF || !isspace((unsigned char)raster_separator)) {
        *error = "expected one whitespace byte before pixel data";
        goto fail;
    }
    if (frame.width > SIZE_MAX / frame.height ||
        frame.width * frame.height > SIZE_MAX / 3U) {
        *error = "image dimensions overflow the RGB allocation size";
        goto fail;
    }
    const size_t byte_count = frame.width * frame.height * 3U;
    frame.pixels = malloc(byte_count);
    if (frame.pixels == NULL) {
        *error = "could not allocate RGB pixel memory";
        goto fail;
    }
    if (fread(frame.pixels, 1U, byte_count, file) != byte_count) {
        *error = "truncated RGB pixel data";
        goto fail;
    }
    for (size_t i = 0; i < byte_count; ++i) {
        if (frame.pixels[i] > frame.max_value) {
            *error = "pixel channel exceeds the maximum color value";
            goto fail;
        }
    }
    *out = frame;
    return true;

fail:
    if (ferror(file)) {
        *error = "I/O error while reading the image";
    }
    frame_destroy(&frame);
    return false;
}

void frame_destroy(Frame *frame)
{
    free(frame->pixels);
    *frame = (Frame){0};
}
