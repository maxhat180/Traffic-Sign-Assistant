#ifndef TRAFFIC_SIGN_FRAME_H
#define TRAFFIC_SIGN_FRAME_H

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

typedef struct {
    size_t width;
    size_t height;
    unsigned int max_value;
    /* Owned, contiguous RGB bytes: offset = (y * width + x) * 3. */
    unsigned char *pixels;
} Frame;

/* Read one P6 frame with max_value 1..255 at the current stream position.
 * file, out and error must be non-NULL; out must be initialized to {0}.
 * The caller owns and closes file. On success, the caller owns out->pixels.
 * On failure, out remains unchanged; *error names a static diagnostic string.
 * Subsequent frames/trailing bytes remain unread. No color scaling is applied.
 */
bool frame_read_ppm(FILE *file, Frame *out, const char **error);

/* Write a complete P6 frame; caller owns/closes file and checks close errors. */
bool frame_write_ppm(FILE *file, const Frame *frame, const char **error);

/* In-place grayscale: rounded (299 R + 587 G + 114 B) / 1000, stored as RGB.
 * These are display-space weights, not a linear-light luminance conversion.
 */
bool frame_grayscale(Frame *frame, const char **error);

/* Nearest-neighbor resize: source coordinate floor(output * source / target).
 * src must be valid; out must be a distinct empty frame initialized to {0}.
 * Success gives out its own allocation; failure leaves both frames unchanged.
 * All operations preserve max_value. Frame buffers must match their dimensions.
 */
bool frame_resize(const Frame *src, size_t width, size_t height,
                  Frame *out, const char **error);

/* Free the pixel allocation and reset the frame. Safe to repeat. */
void frame_destroy(Frame *frame);

#endif
