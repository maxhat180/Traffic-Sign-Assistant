#ifndef TRAFFIC_SIGN_RECOGNIZER_FEATURES_HPP
#define TRAFFIC_SIGN_RECOGNIZER_FEATURES_HPP

#include "../detect.h"

#include <opencv2/core.hpp>

#include <string>

constexpr int recognition_side = 20;
constexpr int recognition_feature_count = recognition_side * recognition_side;

bool sign_features_bgr(const cv::Mat &image, const cv::Rect &box,
                       cv::Mat &features, std::string &error);
bool sign_features_rgb(const Frame &source, const Candidate &candidate,
                       cv::Mat &features, std::string &error);

#endif
