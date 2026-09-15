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

/* Free the pixel allocation and reset the frame. Safe to repeat. */
void frame_destroy(Frame *frame);

#endif
