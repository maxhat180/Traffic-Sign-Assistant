#include "../video_adapter.h"
#include "../image_adapter.h"

#include <opencv2/imgproc.hpp>
#include <opencv2/videoio.hpp>

#include <cmath>
#include <limits>
#include <memory>
#include <new>

struct VideoReader {
    cv::VideoCapture capture;
    double frames_per_second;
    size_t next_index;
};

namespace {

bool empty_frame(const Frame *frame)
{
    return frame != nullptr && frame->pixels == nullptr && frame->width == 0 &&
           frame->height == 0 && frame->max_value == 0;
}

size_t property_size(double value)
{
    if (!std::isfinite(value) || value <= 0.0 ||
        value > static_cast<double>(std::numeric_limits<long long>::max())) {
        return 0;
    }
    return static_cast<size_t>(std::llround(value));
}

} // namespace

extern "C" VideoReader *video_reader_open(const char *path, VideoInfo *info,
                                            const char **error)
{
    if (error == nullptr) {
        return nullptr;
    }
    *error = "invalid video path or metadata destination";
    if (path == nullptr || *path == '\0' || info == nullptr) {
        return nullptr;
    }
    try {
        std::unique_ptr<VideoReader> reader(new (std::nothrow) VideoReader);
        if (!reader) {
            *error = "could not allocate video reader";
            return nullptr;
        }
        if (!reader->capture.open(path, cv::CAP_ANY)) {
            *error = "OpenCV could not open the video";
            return nullptr;
        }
        const double fps = reader->capture.get(cv::CAP_PROP_FPS);
        reader->frames_per_second = std::isfinite(fps) && fps > 0.0 ? fps : 0.0;
        reader->next_index = 0;
        info->width = property_size(reader->capture.get(cv::CAP_PROP_FRAME_WIDTH));
        info->height = property_size(reader->capture.get(cv::CAP_PROP_FRAME_HEIGHT));
        info->frames_per_second = reader->frames_per_second;
        info->estimated_frame_count = property_size(
            reader->capture.get(cv::CAP_PROP_FRAME_COUNT));
        *error = nullptr;
        return reader.release();
    } catch (const cv::Exception &) {
        *error = "OpenCV failed while opening the video";
    } catch (...) {
        *error = "unexpected failure while opening the video";
    }
    return nullptr;
}

extern "C" void video_reader_destroy(VideoReader *reader)
{
    delete reader;
}

extern "C" VideoReadStatus video_reader_next(VideoReader *reader, Frame *out,
                                               VideoFrameInfo *info,
                                               const char **error)
{
    if (error == nullptr) {
        return VIDEO_READ_ERROR;
    }
    *error = "invalid video reader or frame destination";
    if (reader == nullptr || !empty_frame(out) || info == nullptr) {
        return VIDEO_READ_ERROR;
    }
    try {
        cv::Mat decoded;
        if (!reader->capture.read(decoded) || decoded.empty()) {
            *error = nullptr;
            return VIDEO_READ_END;
        }
        cv::Mat bgr;
        if (decoded.type() == CV_8UC3) {
            bgr = decoded;
        } else if (decoded.type() == CV_8UC4) {
            cv::cvtColor(decoded, bgr, cv::COLOR_BGRA2BGR);
        } else if (decoded.type() == CV_8UC1) {
            cv::cvtColor(decoded, bgr, cv::COLOR_GRAY2BGR);
        } else {
            *error = "video decoder returned an unsupported frame format";
            return VIDEO_READ_ERROR;
        }
        if (!frame_from_bgr(bgr.data, static_cast<size_t>(bgr.cols),
                            static_cast<size_t>(bgr.rows), bgr.step, out, error)) {
            return VIDEO_READ_ERROR;
        }
        info->index = reader->next_index;
        if (reader->frames_per_second > 0.0) {
            info->timestamp_ms = static_cast<double>(reader->next_index) * 1000.0 /
                                 reader->frames_per_second;
        } else {
            const double position = reader->capture.get(cv::CAP_PROP_POS_MSEC);
            info->timestamp_ms = std::isfinite(position) && position >= 0.0 ?
                position : static_cast<double>(reader->next_index);
        }
        ++reader->next_index;
        *error = nullptr;
        return VIDEO_READ_FRAME;
    } catch (const cv::Exception &) {
        *error = "OpenCV failed while decoding a video frame";
    } catch (...) {
        *error = "unexpected failure while decoding a video frame";
    }
    return VIDEO_READ_ERROR;
}
