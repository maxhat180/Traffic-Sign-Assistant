#include "recognizer_features.hpp"

#include <opencv2/imgcodecs.hpp>
#include <opencv2/ml.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

struct Dataset {
    cv::Mat samples;
    cv::Mat labels;
    size_t skipped = 0;
    std::map<int, size_t> counts;
};

struct Annotation {
    int speed;
    double center_x;
    double center_y;
    double width;
    double height;
};

double annotation_iou(const Annotation &left, const Annotation &right)
{
    const double left_x = left.center_x - left.width / 2.0;
    const double left_y = left.center_y - left.height / 2.0;
    const double right_x = right.center_x - right.width / 2.0;
    const double right_y = right.center_y - right.height / 2.0;
    const double intersection_width = std::max(0.0,
        std::min(left_x + left.width, right_x + right.width) -
        std::max(left_x, right_x));
    const double intersection_height = std::max(0.0,
        std::min(left_y + left.height, right_y + right.height) -
        std::max(left_y, right_y));
    const double intersection = intersection_width * intersection_height;
    const double union_area = left.width * left.height +
                              right.width * right.height - intersection;
    return union_area > 0.0 ? intersection / union_area : 0.0;
}

void remove_duplicate_annotations(std::vector<Annotation> &annotations,
                                  size_t &skipped)
{
    std::vector<Annotation> unique;
    for (const Annotation &annotation : annotations) {
        bool duplicate = false;
        for (Annotation &prior : unique) {
            if (annotation_iou(annotation, prior) >= 0.9) {
                duplicate = true;
                ++skipped;
                if (annotation.speed != prior.speed) {
                    prior.speed = 0; /* Conflicting labels are not trainable truth. */
                }
                break;
            }
        }
        if (!duplicate) {
            unique.push_back(annotation);
        }
    }
    const auto discarded = std::remove_if(unique.begin(), unique.end(),
        [](const Annotation &annotation) { return annotation.speed == 0; });
    skipped += static_cast<size_t>(std::distance(discarded, unique.end()));
    unique.erase(discarded, unique.end());
    annotations = std::move(unique);
}

int speed_from_class(int class_id)
{
    static constexpr std::array<int, 12> speeds = {
        10, 100, 110, 120, 20, 30, 40, 50, 60, 70, 80, 90};
    return class_id >= 2 && class_id <= 13 ? speeds[static_cast<size_t>(class_id - 2)] : 0;
}

fs::path image_for_label(const fs::path &images, const fs::path &label)
{
    static const std::array<const char *, 4> extensions = {
        ".jpg", ".jpeg", ".png", ".ppm"};
    for (const char *extension : extensions) {
        fs::path candidate = images / label.stem();
        candidate += extension;
        if (fs::exists(candidate)) {
            return candidate;
        }
    }
    return {};
}

Dataset load_split(const fs::path &root, const std::string &split)
{
    Dataset dataset;
    const fs::path labels = root / split / "labels";
    const fs::path images = root / split / "images";
    if (!fs::is_directory(labels) || !fs::is_directory(images)) {
        throw std::runtime_error("missing images or labels directory for split " + split);
    }
    std::vector<fs::path> files;
    for (const fs::directory_entry &entry : fs::directory_iterator(labels)) {
        if (entry.is_regular_file() && entry.path().extension() == ".txt") {
            files.push_back(entry.path());
        }
    }
    std::sort(files.begin(), files.end());
    size_t scanned = 0;
    for (const fs::path &label_path : files) {
        ++scanned;
        if (scanned % 250U == 0U) {
            std::cout << "  " << split << ": scanned " << scanned << '/'
                      << files.size() << " labels, kept " << dataset.samples.rows
                      << " crops" << std::endl;
        }
        std::vector<Annotation> annotations;
        std::ifstream input(label_path);
        int class_id = 0;
        double center_x = 0.0;
        double center_y = 0.0;
        double width = 0.0;
        double height = 0.0;
        while (input >> class_id >> center_x >> center_y >> width >> height) {
            const int speed = speed_from_class(class_id);
            if (speed != 0 && std::isfinite(center_x) && std::isfinite(center_y) &&
                std::isfinite(width) && std::isfinite(height) && width > 0.0 &&
                height > 0.0) {
                annotations.push_back({speed, center_x, center_y, width, height});
            }
        }
        remove_duplicate_annotations(annotations, dataset.skipped);
        if (annotations.empty()) {
            continue;
        }
        const fs::path image_path = image_for_label(images, label_path);
        if (image_path.empty()) {
            ++dataset.skipped;
            continue;
        }
        const cv::Mat image = cv::imread(image_path.string(), cv::IMREAD_COLOR);
        if (image.empty()) {
            ++dataset.skipped;
            continue;
        }
        for (const Annotation &annotation : annotations) {
            const int left = static_cast<int>(std::floor(
                (annotation.center_x - annotation.width / 2.0) * image.cols));
            const int top = static_cast<int>(std::floor(
                (annotation.center_y - annotation.height / 2.0) * image.rows));
            const int right = static_cast<int>(std::ceil(
                (annotation.center_x + annotation.width / 2.0) * image.cols));
            const int bottom = static_cast<int>(std::ceil(
                (annotation.center_y + annotation.height / 2.0) * image.rows));
            cv::Mat features;
            std::string error;
            if (!sign_features_bgr(image, cv::Rect(left, top, right - left, bottom - top),
                                   features, error)) {
                ++dataset.skipped;
                continue;
            }
            dataset.samples.push_back(features);
            dataset.labels.push_back(annotation.speed);
            ++dataset.counts[annotation.speed];
        }
    }
    return dataset;
}

struct Prediction {
    int speed;
    double confidence;
};

Prediction predict(const cv::Ptr<cv::ml::RTrees> &forest, const cv::Mat &sample)
{
    cv::Mat votes;
    forest->getVotes(sample, votes, cv::ml::DTrees::PREDICT_AUTO);
    if (votes.rows != 2 || votes.cols == 0 || votes.type() != CV_32SC1) {
        throw std::runtime_error("forest returned invalid votes");
    }
    int best = 0;
    int total = 0;
    for (int column = 0; column < votes.cols; ++column) {
        total += votes.at<int>(1, column);
        if (votes.at<int>(1, column) > votes.at<int>(1, best)) {
            best = column;
        }
    }
    if (total <= 0) {
        throw std::runtime_error("forest returned no votes");
    }
    return {votes.at<int>(0, best),
            static_cast<double>(votes.at<int>(1, best)) / static_cast<double>(total)};
}

void report(const cv::Ptr<cv::ml::RTrees> &forest, const Dataset &dataset,
            const std::string &name)
{
    static constexpr std::array<double, 5> thresholds = {0.0, 0.50, 0.60, 0.70, 0.80};
    std::cout << name << ": " << dataset.samples.rows << " samples, "
              << dataset.skipped << " skipped\n";
    for (double threshold : thresholds) {
        size_t accepted = 0;
        size_t correct = 0;
        for (int row = 0; row < dataset.samples.rows; ++row) {
            const Prediction result = predict(forest, dataset.samples.row(row));
            if (result.confidence >= threshold) {
                ++accepted;
                if (result.speed == dataset.labels.at<int>(row, 0)) {
                    ++correct;
                }
            }
        }
        const double coverage = dataset.samples.rows == 0 ? 0.0 :
            static_cast<double>(accepted) / static_cast<double>(dataset.samples.rows);
        const double accuracy = accepted == 0 ? 0.0 :
            static_cast<double>(correct) / static_cast<double>(accepted);
        std::cout << "  threshold=" << std::fixed << std::setprecision(2) << threshold
                  << " coverage=" << std::setprecision(3) << coverage
                  << " accepted_accuracy=" << accuracy
                  << " correct=" << correct << '/' << accepted << '\n';
    }
    std::map<int, std::pair<size_t, size_t>> per_class;
    for (int row = 0; row < dataset.samples.rows; ++row) {
        const int expected = dataset.labels.at<int>(row, 0);
        const Prediction result = predict(forest, dataset.samples.row(row));
        ++per_class[expected].second;
        if (result.speed == expected) {
            ++per_class[expected].first;
        }
    }
    std::cout << "  raw accuracy by speed:";
    for (const auto &[speed, result] : per_class) {
        std::cout << ' ' << speed << '=' << result.first << '/' << result.second;
    }
    std::cout << '\n';
}

} // namespace

int main(int argc, char **argv)
{
    if (argc != 3) {
        std::cerr << "Usage: " << argv[0] << " <dataset-root> <model.yml>\n";
        return EXIT_FAILURE;
    }
    try {
        const fs::path root = argv[1];
        std::cout << "Loading training split..." << std::endl;
        const Dataset training = load_split(root, "train");
        if (training.samples.rows == 0 || training.counts.size() < 2U) {
            throw std::runtime_error("training split has insufficient speed-sign samples");
        }
        std::cout << "Training samples: " << training.samples.rows << " (";
        for (const auto &[speed, count] : training.counts) {
            std::cout << speed << ':' << count << ' ';
        }
        std::cout << ") skipped=" << training.skipped << '\n';

        cv::setRNGSeed(20260916);
        cv::Ptr<cv::ml::RTrees> forest = cv::ml::RTrees::create();
        forest->setMaxDepth(18);
        forest->setMinSampleCount(2);
        forest->setMaxCategories(12);
        forest->setActiveVarCount(64);
        forest->setCalculateVarImportance(false);
        forest->setPriors(cv::Mat::ones(1, static_cast<int>(training.counts.size()), CV_32F));
        forest->setTermCriteria(cv::TermCriteria(cv::TermCriteria::MAX_ITER, 100, 0.0));
        if (!forest->train(training.samples, cv::ml::ROW_SAMPLE, training.labels)) {
            throw std::runtime_error("random-forest training failed");
        }
        forest->save(argv[2]);
        std::cout << "Saved model: " << argv[2] << '\n';

        report(forest, training, "train");
        report(forest, load_split(root, "valid"), "valid");
        report(forest, load_split(root, "test"), "test");
        return EXIT_SUCCESS;
    } catch (const cv::Exception &exception) {
        std::cerr << "OpenCV failure: " << exception.what() << '\n';
    } catch (const std::exception &exception) {
        std::cerr << "Training failure: " << exception.what() << '\n';
    }
    return EXIT_FAILURE;
}
