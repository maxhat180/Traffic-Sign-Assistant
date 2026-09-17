#include "../recognizer.h"
#include "recognizer_features.hpp"

#include <opencv2/ml.hpp>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <string>
#include <utility>

struct Recognizer {
    cv::Ptr<cv::ml::RTrees> forest;
    unsigned int min_confidence;
};

namespace {

bool predict_features(const Recognizer &recognizer, const cv::Mat &features,
                      Recognition &result, std::string &error)
{
    cv::Mat votes;
    recognizer.forest->getVotes(features, votes, cv::ml::DTrees::PREDICT_AUTO);
    if (votes.rows != 2 || votes.cols == 0 || votes.type() != CV_32SC1) {
        error = "recognition model returned invalid votes";
        return false;
    }
    int best_column = 0;
    int best_votes = votes.at<int>(1, 0);
    int total_votes = 0;
    for (int column = 0; column < votes.cols; ++column) {
        const int count = votes.at<int>(1, column);
        if (count < 0) {
            error = "recognition model returned negative votes";
            return false;
        }
        total_votes += count;
        if (count > best_votes) {
            best_votes = count;
            best_column = column;
        }
    }
    if (total_votes <= 0) {
        error = "recognition model returned no votes";
        return false;
    }
    const int label = votes.at<int>(0, best_column);
    if (label <= 0) {
        error = "recognition model returned an invalid speed";
        return false;
    }
    const unsigned int confidence = static_cast<unsigned int>(
        (static_cast<unsigned long long>(best_votes) * 1000ULL +
         static_cast<unsigned int>(total_votes / 2)) /
        static_cast<unsigned int>(total_votes));
    result.confidence = confidence;
    result.predicted_speed = static_cast<unsigned int>(label);
    result.known = confidence >= recognizer.min_confidence;
    result.speed = result.known ? result.predicted_speed : 0U;
    error.clear();
    return true;
}

} // namespace

extern "C" Recognizer *recognizer_load(const char *path,
                                        unsigned int min_confidence,
                                        const char **error)
{
    if (error == nullptr) {
        return nullptr;
    }
    *error = "invalid recognition model path or confidence threshold";
    if (path == nullptr || *path == '\0' || min_confidence > 1000U) {
        return nullptr;
    }
    try {
        cv::Ptr<cv::ml::RTrees> forest = cv::ml::RTrees::load(path);
        if (forest.empty() || !forest->isTrained() || !forest->isClassifier() ||
            forest->getVarCount() != recognition_feature_count) {
            *error = "invalid or incompatible recognition model";
            return nullptr;
        }
        Recognizer *recognizer = new (std::nothrow) Recognizer;
        if (recognizer == nullptr) {
            *error = "could not allocate recognizer";
            return nullptr;
        }
        recognizer->forest = std::move(forest);
        recognizer->min_confidence = min_confidence;
        *error = nullptr;
        return recognizer;
    } catch (const cv::Exception &) {
        *error = "OpenCV could not load the recognition model";
        return nullptr;
    } catch (...) {
        *error = "unexpected failure while loading recognition model";
        return nullptr;
    }
}

extern "C" void recognizer_destroy(Recognizer *recognizer)
{
    delete recognizer;
}

extern "C" bool recognizer_predict(const Recognizer *recognizer,
                                    const Frame *source,
                                    const Candidate *candidate,
                                    Recognition *out, const char **error)
{
    if (error == nullptr) {
        return false;
    }
    *error = "invalid recognition input or output";
    if (recognizer == nullptr || source == nullptr || candidate == nullptr ||
        out == nullptr) {
        return false;
    }
    try {
        cv::Mat features;
        std::string diagnostic;
        if (!sign_features_rgb(*source, *candidate, features, diagnostic) ||
            !predict_features(*recognizer, features, *out, diagnostic)) {
            *error = diagnostic == "candidate is too small for recognition" ?
                "candidate is too small for recognition" :
                "recognition preprocessing or prediction failed";
            return false;
        }
        *error = nullptr;
        return true;
    } catch (const cv::Exception &) {
        *error = "OpenCV failed while recognizing the candidate";
        return false;
    } catch (...) {
        *error = "unexpected failure while recognizing the candidate";
        return false;
    }
}

extern "C" bool recognition_save(const Recognizer *recognizer,
                                  const Frame *source,
                                  const Detection *detection,
                                  const char *prefix, size_t *known_count,
                                  const char **error)
{
    if (error == nullptr) {
        return false;
    }
    *error = "invalid recognition output input";
    if (recognizer == nullptr || source == nullptr || detection == nullptr ||
        prefix == nullptr || known_count == nullptr ||
        (detection->count != 0 && detection->boxes == nullptr)) {
        return false;
    }
    const size_t length = std::strlen(prefix);
    if (length > static_cast<size_t>(-1) - 24U) {
        *error = "recognition output prefix too long";
        return false;
    }
    char *path = static_cast<char *>(std::malloc(length + 24U));
    if (path == nullptr) {
        *error = "could not allocate recognition output path";
        return false;
    }
    std::snprintf(path, length + 24U, "%s-recognition.csv", prefix);
    std::FILE *csv = std::fopen(path, "w");
    std::free(path);
    if (csv == nullptr) {
        *error = "could not open recognition CSV";
        return false;
    }
    bool ok = std::fprintf(csv,
        "id,x,y,width,height,speed,predicted_speed,confidence,known\n") >= 0;
    size_t known = 0;
    for (size_t i = 0; i < detection->count && ok; ++i) {
        Recognition result = {};
        if (!recognizer_predict(recognizer, source, &detection->boxes[i],
                                &result, error)) {
            std::fclose(csv);
            return false;
        }
        known += result.known ? 1U : 0U;
        const Candidate &box = detection->boxes[i];
        ok = std::fprintf(csv, "%zu,%zu,%zu,%zu,%zu,%u,%u,%.3f,%s\n",
            i, box.x, box.y, box.width, box.height, result.speed,
            result.predicted_speed,
            static_cast<double>(result.confidence) / 1000.0,
            result.known ? "true" : "false") >= 0;
    }
    if (std::fclose(csv) != 0) {
        ok = false;
    }
    if (!ok) {
        *error = "could not write recognition CSV";
        return false;
    }
    *known_count = known;
    *error = nullptr;
    return true;
}
