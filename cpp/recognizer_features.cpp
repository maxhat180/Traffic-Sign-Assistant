#include "recognizer_features.hpp"

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <limits>

namespace {

bool features_from_gray(const cv::Mat &gray, const cv::Rect &requested,
                        cv::Mat &features, std::string &error)
{
    const cv::Rect bounds(0, 0, gray.cols, gray.rows);
    const cv::Rect box = requested & bounds;
    if (box.width < 4 || box.height < 4) {
        error = "candidate is too small for recognition";
        return false;
    }
    const int inset = std::max(1, std::min(box.width, box.height) / 8);
    const cv::Rect inner(box.x + inset, box.y + inset,
                         box.width - 2 * inset, box.height - 2 * inset);
    if (inner.width < 2 || inner.height < 2) {
        error = "candidate interior is too small for recognition";
        return false;
    }
    cv::Mat resized;
    cv::resize(gray(inner), resized, cv::Size(recognition_side, recognition_side),
               0.0, 0.0, cv::INTER_AREA);
    cv::equalizeHist(resized, resized);
    resized.reshape(1, 1).convertTo(features, CV_32F, 1.0 / 255.0);
    error.clear();
    return true;
}

} // namespace

bool sign_features_bgr(const cv::Mat &image, const cv::Rect &box,
                       cv::Mat &features, std::string &error)
{
    if (image.empty() || image.type() != CV_8UC3) {
        error = "recognition requires an 8-bit BGR image";
        return false;
    }
    cv::Mat gray;
    cv::cvtColor(image, gray, cv::COLOR_BGR2GRAY);
    return features_from_gray(gray, box, features, error);
}

bool sign_features_rgb(const Frame &source, const Candidate &candidate,
                       cv::Mat &features, std::string &error)
{
    if (source.pixels == nullptr || source.width == 0 || source.height == 0 ||
        source.max_value != 255U ||
        source.width > static_cast<size_t>(std::numeric_limits<int>::max()) ||
        source.height > static_cast<size_t>(std::numeric_limits<int>::max()) ||
        candidate.x > source.width || candidate.y > source.height ||
        candidate.width > source.width - candidate.x ||
        candidate.height > source.height - candidate.y ||
        candidate.x > static_cast<size_t>(std::numeric_limits<int>::max()) ||
        candidate.y > static_cast<size_t>(std::numeric_limits<int>::max()) ||
        candidate.width > static_cast<size_t>(std::numeric_limits<int>::max()) ||
        candidate.height > static_cast<size_t>(std::numeric_limits<int>::max())) {
        error = "invalid RGB frame or candidate";
        return false;
    }
    const cv::Mat rgb(static_cast<int>(source.height), static_cast<int>(source.width),
                      CV_8UC3, source.pixels, source.width * 3U);
    cv::Mat gray;
    cv::cvtColor(rgb, gray, cv::COLOR_RGB2GRAY);
    return features_from_gray(gray,
        cv::Rect(static_cast<int>(candidate.x), static_cast<int>(candidate.y),
                 static_cast<int>(candidate.width), static_cast<int>(candidate.height)),
        features, error);
}
