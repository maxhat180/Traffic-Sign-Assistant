#include "../video_adapter.h"

#include <opencv2/imgcodecs.hpp>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>

namespace {

unsigned int checks = 0;

void cleanup()
{
    std::remove("video-test-00.png");
    std::remove("video-test-01.png");
    std::remove("video-test-02.png");
}

void check(bool condition, const char *message)
{
    ++checks;
    if (!condition) {
        cleanup();
        std::cerr << "FAIL: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

void write_frame(const char *path, const cv::Scalar &bgr)
{
    const cv::Mat image(3, 4, CV_8UC3, bgr);
    check(cv::imwrite(path, image), "PNG sequence frame saves");
}

} // namespace

int main()
{
    if (std::filesystem::exists("video-test-00.png") ||
        std::filesystem::exists("video-test-01.png") ||
        std::filesystem::exists("video-test-02.png")) {
        std::cerr << "FAIL: refusing to overwrite existing video test artifacts\n";
        return EXIT_FAILURE;
    }
    write_frame("video-test-00.png", cv::Scalar(30, 20, 10));
    write_frame("video-test-01.png", cv::Scalar(60, 50, 40));
    write_frame("video-test-02.png", cv::Scalar(90, 80, 70));

    const char *error = nullptr;
    VideoInfo video = {};
    VideoReader *reader = video_reader_open("video-test-%02d.png", &video, &error);
    check(reader != nullptr && error == nullptr, "image sequence opens as video");
    check(video.width == 4U && video.height == 3U, "video dimensions are reported");

    for (size_t index = 0; index < 3U; ++index) {
        Frame frame = {};
        VideoFrameInfo info = {};
        check(video_reader_next(reader, &frame, &info, &error) == VIDEO_READ_FRAME,
              "video frame decodes");
        check(error == nullptr && info.index == index, "frame index is reported");
        check(frame.width == 4U && frame.height == 3U && frame.max_value == 255U,
              "decoded frame metadata is valid");
        const unsigned char expected_red = static_cast<unsigned char>(10U + index * 30U);
        const unsigned char expected_green = static_cast<unsigned char>(20U + index * 30U);
        const unsigned char expected_blue = static_cast<unsigned char>(30U + index * 30U);
        check(frame.pixels[0] == expected_red && frame.pixels[1] == expected_green &&
              frame.pixels[2] == expected_blue, "decoded BGR becomes packed RGB");
        frame_destroy(&frame);
    }
    Frame end_frame = {};
    VideoFrameInfo end_info = {};
    check(video_reader_next(reader, &end_frame, &end_info, &error) == VIDEO_READ_END,
          "end of sequence is distinct from failure");
    check(error == nullptr && end_frame.pixels == nullptr, "end leaves frame empty");
    video_reader_destroy(reader);

    check(video_reader_open("video-test-missing.mp4", &video, &error) == nullptr,
          "missing video is rejected");
    check(error != nullptr, "missing video reports a diagnostic");
    check(video_reader_next(nullptr, &end_frame, &end_info, &error) == VIDEO_READ_ERROR,
          "null reader is rejected");
    cleanup();
    std::cout << "PASS: " << checks << " video adapter checks\n";
    return EXIT_SUCCESS;
}
