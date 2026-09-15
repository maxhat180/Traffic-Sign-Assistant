#ifndef TRAFFIC_SIGN_IMAGE_ADAPTER_H
#define TRAFFIC_SIGN_IMAGE_ADAPTER_H

#include "frame.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    double x;
    double y;
} FramePoint;

/* Copy row-oriented 8-bit BGR data into an owned packed RGB Frame.
 * This dependency-free function is the tested boundary between C and C++.
 */
bool frame_from_bgr(const unsigned char *pixels, size_t width, size_t height,
                    size_t row_stride, Frame *out, const char **error);

/* OpenCV-backed operations. These symbols are available only in a build where
 * CMake found OpenCV. Corners are ordered top-left, top-right, bottom-right,
 * bottom-left. Both functions leave an empty destination unchanged on failure.
 */
bool frame_read_image(const char *path, Frame *out, const char **error);
bool frame_normalize_quad(const Frame *source, const FramePoint corners[4],
                          size_t width, size_t height, Frame *out,
                          const char **error);

#ifdef __cplusplus
}
#endif

#endif
