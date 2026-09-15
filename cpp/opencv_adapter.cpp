#include "image_adapter.h"

#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include <cmath>
#include <limits>

namespace {

bool empty_destination(const Frame *out)
{
    return out != nullptr && out->pixels == nullptr && out->width == 0 &&
           out->height == 0 && out->max_value == 0;
}

bool valid_source(const Frame *source)
{
    return source != nullptr && source->pixels != nullptr && source->width != 0 &&
           source->height != 0 && source->max_value == 255U &&
           source->width <= static_cast<size_t>(std::numeric_limits<int>::max()) &&
           source->height <= static_cast<size_t>(std::numeric_limits<int>::max()) &&
           source->width <= std::numeric_limits<size_t>::max() / source->height &&
           source->width * source->height <= std::numeric_limits<size_t>::max() / 3U;
}

} // namespace

extern "C" bool frame_read_image(const char *path, Frame *out, const char **error)
{
    if (error == nullptr) {
        return false;
    }
    *error = "invalid image path or destination";
    if (path == nullptr || *path == '\0' || !empty_destination(out)) {
        return false;
    }
    try {
        const cv::Mat image = cv::imread(path, cv::IMREAD_COLOR);
        if (image.empty() || image.type() != CV_8UC3) {
            *error = "OpenCV could not decode an 8-bit color image";
            return false;
        }
        return frame_from_bgr(image.data, static_cast<size_t>(image.cols),
                              static_cast<size_t>(image.rows), image.step, out, error);
    } catch (const cv::Exception &) {
        *error = "OpenCV failed while decoding the image";
        return false;
    } catch (...) {
        *error = "unexpected failure while decoding the image";
        return false;
    }
}

extern "C" bool frame_normalize_quad(const Frame *source,
                                      const FramePoint corners[4], size_t width,
                                      size_t height, Frame *out, const char **error)
{
    if (error == nullptr) {
        return false;
    }
    *error = "invalid normalization input or destination";
    if (!valid_source(source) || corners == nullptr || !empty_destination(out) ||
        width < 2U || height < 2U ||
        width > static_cast<size_t>(std::numeric_limits<int>::max()) ||
        height > static_cast<size_t>(std::numeric_limits<int>::max())) {
        return false;
    }
    for (size_t i = 0; i < 4U; ++i) {
        if (!std::isfinite(corners[i].x) || !std::isfinite(corners[i].y)) {
            return false;
        }
    }
    try {
        const cv::Point2f input[4] = {
            {static_cast<float>(corners[0].x), static_cast<float>(corners[0].y)},
            {static_cast<float>(corners[1].x), static_cast<float>(corners[1].y)},
            {static_cast<float>(corners[2].x), static_cast<float>(corners[2].y)},
            {static_cast<float>(corners[3].x), static_cast<float>(corners[3].y)}};
        const float right = static_cast<float>(width - 1U);
        const float bottom = static_cast<float>(height - 1U);
        const cv::Point2f output[4] = {{0.0F, 0.0F}, {right, 0.0F},
                                      {right, bottom}, {0.0F, bottom}};
        const cv::Mat transform = cv::getPerspectiveTransform(input, output);
        const cv::Mat source_rgb(static_cast<int>(source->height),
                                 static_cast<int>(source->width), CV_8UC3,
                                 source->pixels, source->width * 3U);
        cv::Mat normalized_rgb;
        cv::warpPerspective(source_rgb, normalized_rgb, transform,
                            cv::Size(static_cast<int>(width), static_cast<int>(height)),
                            cv::INTER_LINEAR, cv::BORDER_REPLICATE);
        cv::Mat normalized_bgr;
        cv::cvtColor(normalized_rgb, normalized_bgr, cv::COLOR_RGB2BGR);
        return frame_from_bgr(normalized_bgr.data, width, height,
                              normalized_bgr.step, out, error);
    } catch (const cv::Exception &) {
        *error = "OpenCV failed while normalizing the image";
        return false;
    } catch (...) {
        *error = "unexpected failure while normalizing the image";
        return false;
    }
}
