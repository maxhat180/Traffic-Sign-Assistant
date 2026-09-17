#ifndef TRAFFIC_SIGN_VIDEO_ADAPTER_H
#define TRAFFIC_SIGN_VIDEO_ADAPTER_H

#include "frame.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct VideoReader VideoReader;

typedef struct {
    size_t width;
    size_t height;
    double frames_per_second;
    size_t estimated_frame_count; /* Zero when the container does not report it. */
} VideoInfo;

typedef struct {
    size_t index;       /* Zero-based source-frame index. */
    double timestamp_ms;
} VideoFrameInfo;

typedef enum {
    VIDEO_READ_ERROR = -1,
    VIDEO_READ_END = 0,
    VIDEO_READ_FRAME = 1
} VideoReadStatus;

/* Open an OpenCV-supported video. The returned reader owns decoder state. */
VideoReader *video_reader_open(const char *path, VideoInfo *info,
                               const char **error);
void video_reader_destroy(VideoReader *reader);

/* Decode the next frame into an owned packed RGB Frame initialized to {0}.
 * End-of-stream leaves the destination empty and is not an error.
 */
VideoReadStatus video_reader_next(VideoReader *reader, Frame *out,
                                  VideoFrameInfo *info, const char **error);

#ifdef __cplusplus
}
#endif

#endif
