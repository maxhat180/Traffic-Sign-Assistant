#include "../recognizer.h"
#include "../cpp/recognizer_features.hpp"

#include <opencv2/imgproc.hpp>
#include <opencv2/ml.hpp>

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {

unsigned int checks = 0;
const char *model_path = "recognizer-test-model.yml";
const char *output_prefix = "recognizer-test-output";

void cleanup()
{
    std::remove(model_path);
    std::remove("recognizer-test-output-recognition.csv");
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

cv::Mat pattern(bool vertical, int offset)
{
    cv::Mat image(32, 32, CV_8UC3, cv::Scalar(245, 245, 245));
    if (vertical) {
        cv::rectangle(image, cv::Rect(13 + offset, 7, 5, 18),
                      cv::Scalar(10, 10, 10), cv::FILLED);
    } else {
        cv::rectangle(image, cv::Rect(7, 13 + offset, 18, 5),
                      cv::Scalar(10, 10, 10), cv::FILLED);
    }
    return image;
}

} // namespace

int main()
{
    cv::Mat samples;
    cv::Mat labels;
    for (int repeat = 0; repeat < 12; ++repeat) {
        for (int kind = 0; kind < 2; ++kind) {
            cv::Mat features;
            std::string error;
            const cv::Mat image = pattern(kind == 0, repeat % 3 - 1);
            check(sign_features_bgr(image, cv::Rect(0, 0, 32, 32),
                                    features, error),
                  "synthetic features are extracted");
            samples.push_back(features);
            labels.push_back(kind == 0 ? 30 : 70);
        }
    }
    cv::Ptr<cv::ml::RTrees> forest = cv::ml::RTrees::create();
    forest->setMaxDepth(8);
    forest->setMinSampleCount(1);
    forest->setMaxCategories(2);
    forest->setActiveVarCount(20);
    forest->setTermCriteria(cv::TermCriteria(cv::TermCriteria::MAX_ITER, 25, 0.0));
    check(forest->train(samples, cv::ml::ROW_SAMPLE, labels),
          "synthetic forest trains");
    forest->save(model_path);

    const char *error = nullptr;
    Recognizer *recognizer = recognizer_load(model_path, 600U, &error);
    check(recognizer != nullptr && error == nullptr, "recognizer model loads");

    const cv::Mat query = pattern(true, 0);
    std::vector<unsigned char> rgb(static_cast<size_t>(query.rows * query.cols * 3));
    for (int y = 0; y < query.rows; ++y) {
        for (int x = 0; x < query.cols; ++x) {
            const cv::Vec3b pixel = query.at<cv::Vec3b>(y, x);
            const size_t index = static_cast<size_t>((y * query.cols + x) * 3);
            rgb[index] = pixel[2];
            rgb[index + 1U] = pixel[1];
            rgb[index + 2U] = pixel[0];
        }
    }
    const Frame frame = {32U, 32U, 255U, rgb.data()};
    Candidate candidate = {0U, 0U, 32U, 32U, 100U};
    Recognition result = {};
    check(recognizer_predict(recognizer, &frame, &candidate, &result, &error),
          "synthetic candidate predicts");
    check(result.known && result.speed == 30U && result.predicted_speed == 30U &&
          result.confidence >= 600U,
          "prediction reports speed and confidence");

    const Detection detection = {{}, {}, &candidate, 1U, 1U};
    size_t known = 0;
    check(recognition_save(recognizer, &frame, &detection, output_prefix,
                           &known, &error),
          "recognition CSV saves");
    check(known == 1U, "known count is reported");
    std::ifstream csv("recognizer-test-output-recognition.csv");
    std::string contents((std::istreambuf_iterator<char>(csv)),
                         std::istreambuf_iterator<char>());
    check(contents.find(",30,") != std::string::npos &&
          contents.find(",true") != std::string::npos,
          "recognition CSV contains prediction");

    recognizer_destroy(recognizer);
    check(recognizer_load(model_path, 1001U, &error) == nullptr,
          "invalid confidence threshold is rejected");
    cleanup();
    std::cout << "PASS: " << checks << " recognizer checks\n";
    return EXIT_SUCCESS;
}
