#include "../detect.h"
#include "../recognizer.h"
#include "../video_adapter.h"
#include "tracker.hpp"

#include <charconv>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>

namespace fs = std::filesystem;

namespace {

struct ReaderDestroy {
    void operator()(VideoReader *reader) const { video_reader_destroy(reader); }
};

struct RecognizerDestroy {
    void operator()(Recognizer *recognizer) const { recognizer_destroy(recognizer); }
};

using ReaderPtr = std::unique_ptr<VideoReader, ReaderDestroy>;
using RecognizerPtr = std::unique_ptr<Recognizer, RecognizerDestroy>;

struct Options {
    fs::path input;
    fs::path output_directory;
    std::string model;
    size_t sample_ms = 1000;
    size_t max_samples = 0;
    unsigned int confidence = 600;
    unsigned int track_iou = 100;
    size_t track_gap_ms = 0;
    size_t confirmation_hits = 2;
    unsigned int track_confidence = 500;
    DetectorConfig detector = detector_defaults();
};

int usage(const char *program)
{
    std::cerr << "Usage: " << program
              << " <video> <output-directory> [--recognize MODEL]\n"
              << "       [--sample-ms N] [--max-samples N] [--confidence N]\n"
              << "Tracking: --track-iou N --track-gap-ms N --confirm-hits N\n"
              << "          --track-confidence N (ratios/confidence per mille)\n"
              << "Detector: --red-min N --red-ratio N --min-area N --min-side N\n"
              << "          --max-aspect N --min-fill N --max-fill N --no-cleanup\n";
    return EXIT_FAILURE;
}

bool number(const char *text, bool allow_zero, size_t &out)
{
    if (text == nullptr || *text == '\0') {
        return false;
    }
    size_t value = 0;
    const char *end = text;
    while (*end != '\0') {
        ++end;
    }
    const auto result = std::from_chars(text, end, value);
    if (result.ec != std::errc() || result.ptr != end || (!allow_zero && value == 0)) {
        return false;
    }
    out = value;
    return true;
}

bool parse_options(int argc, char **argv, Options &options)
{
    if (argc < 3) {
        return false;
    }
    options.input = argv[1];
    options.output_directory = argv[2];
    bool confidence_seen = false;
    for (int index = 3; index < argc; ++index) {
        const std::string option = argv[index];
        if (option == "--no-cleanup") {
            options.detector.cleanup = false;
            continue;
        }
        if (index + 1 >= argc) {
            return false;
        }
        const char *value_text = argv[++index];
        if (option == "--recognize" && options.model.empty()) {
            options.model = value_text;
            continue;
        }
        size_t value = 0;
        if (option == "--max-samples") {
            if (!number(value_text, true, value)) { return false; }
            options.max_samples = value;
        } else if (option == "--sample-ms") {
            if (!number(value_text, false, value)) { return false; }
            options.sample_ms = value;
        } else if (option == "--confidence" && !confidence_seen) {
            if (!number(value_text, true, value) || value > 1000U) { return false; }
            options.confidence = static_cast<unsigned int>(value);
            confidence_seen = true;
        } else if (option == "--track-gap-ms") {
            if (!number(value_text, true, value)) { return false; }
            options.track_gap_ms = value;
        } else if (option == "--confirm-hits") {
            if (!number(value_text, false, value)) { return false; }
            options.confirmation_hits = value;
        } else if (option == "--track-iou") {
            if (!number(value_text, false, value) || value > 1000U) { return false; }
            options.track_iou = static_cast<unsigned int>(value);
        } else if (option == "--track-confidence") {
            if (!number(value_text, true, value) || value > 1000U) { return false; }
            options.track_confidence = static_cast<unsigned int>(value);
        } else {
            if (!number(value_text, false, value) ||
                value > static_cast<size_t>(std::numeric_limits<unsigned int>::max())) {
                return false;
            }
            const unsigned int narrow = static_cast<unsigned int>(value);
            if (option == "--red-min") { options.detector.red_min = narrow; }
            else if (option == "--red-ratio") { options.detector.red_ratio = narrow; }
            else if (option == "--min-area") { options.detector.min_area = value; }
            else if (option == "--min-side") { options.detector.min_side = value; }
            else if (option == "--max-aspect") { options.detector.max_aspect = narrow; }
            else if (option == "--min-fill") { options.detector.min_fill = narrow; }
            else if (option == "--max-fill") { options.detector.max_fill = narrow; }
            else { return false; }
        }
    }
    return !options.input.empty() && !options.output_directory.empty() &&
           (!confidence_seen || !options.model.empty());
}

std::string frame_prefix(const fs::path &directory, size_t source_index)
{
    std::ostringstream name;
    name << "frame-" << std::setw(6) << std::setfill('0') << source_index;
    return (directory / name.str()).string();
}

void require_stream(const std::ofstream &stream, const char *message)
{
    if (!stream) {
        throw std::runtime_error(message);
    }
}

} // namespace

int main(int argc, char **argv)
{
    Options options;
    if (!parse_options(argc, argv, options)) {
        return usage(argv[0]);
    }
    try {
        std::error_code directory_error;
        fs::create_directories(options.output_directory, directory_error);
        if (directory_error || !fs::is_directory(options.output_directory)) {
            throw std::runtime_error("could not create the video output directory");
        }

        const char *error = nullptr;
        VideoInfo video_info = {};
        ReaderPtr reader(video_reader_open(options.input.string().c_str(),
                                           &video_info, &error));
        if (!reader) {
            throw std::runtime_error(error == nullptr ? "could not open video" : error);
        }
        RecognizerPtr recognizer;
        if (!options.model.empty()) {
            recognizer.reset(recognizer_load(options.model.c_str(), options.confidence,
                                             &error));
            if (!recognizer) {
                throw std::runtime_error(error == nullptr ?
                    "could not load recognition model" : error);
            }
        }
        const double track_gap_ms = options.track_gap_ms == 0U ?
            static_cast<double>(options.sample_ms) * 2.0 :
            static_cast<double>(options.track_gap_ms);
        TemporalTracker tracker(options.track_iou, track_gap_ms,
                                options.confirmation_hits,
                                options.track_confidence);

        std::ofstream frames(options.output_directory / "frames.csv");
        require_stream(frames, "could not open video frame summary");
        frames << "sample,source_frame,timestamp_ms,candidates,known,unknown\n";
        std::ofstream predictions;
        if (recognizer) {
            predictions.open(options.output_directory / "recognition.csv");
            require_stream(predictions, "could not open video recognition summary");
            predictions << "sample,source_frame,timestamp_ms,candidate,track_id,x,y,"
                           "width,height,speed,predicted_speed,confidence,known\n";
        }

        std::cout << "Video: " << video_info.width << 'x' << video_info.height
                  << " fps=" << std::fixed << std::setprecision(3)
                  << video_info.frames_per_second
                  << " estimated_frames=" << video_info.estimated_frame_count << '\n';

        size_t sample = 0;
        size_t decoded = 0;
        size_t total_candidates = 0;
        size_t total_known = 0;
        double next_sample_ms = 0.0;
        while (options.max_samples == 0 || sample < options.max_samples) {
            Frame frame = {};
            VideoFrameInfo frame_info = {};
            const VideoReadStatus status = video_reader_next(reader.get(), &frame,
                                                               &frame_info, &error);
            if (status == VIDEO_READ_END) {
                break;
            }
            if (status == VIDEO_READ_ERROR) {
                throw std::runtime_error(error == nullptr ?
                    "could not decode video frame" : error);
            }
            ++decoded;
            if (frame_info.timestamp_ms + 0.001 < next_sample_ms) {
                frame_destroy(&frame);
                continue;
            }
            do {
                next_sample_ms += static_cast<double>(options.sample_ms);
            } while (next_sample_ms <= frame_info.timestamp_ms);

            Detection detection = {};
            if (!detect_signs(&frame, &options.detector, &detection, &error)) {
                frame_destroy(&frame);
                throw std::runtime_error(error == nullptr ? "detection failed" : error);
            }
            const std::string prefix = frame_prefix(options.output_directory,
                                                     frame_info.index);
            if (!detection_save(&frame, &detection, prefix.c_str(), &error)) {
                detection_destroy(&detection);
                frame_destroy(&frame);
                throw std::runtime_error(error == nullptr ?
                    "could not save frame detection" : error);
            }

            size_t known = 0;
            std::vector<Recognition> candidate_results(detection.count);
            std::vector<TrackObservation> observations;
            observations.reserve(detection.count);
            for (size_t candidate = 0; candidate < detection.count; ++candidate) {
                Recognition &result = candidate_results[candidate];
                if (recognizer &&
                    !recognizer_predict(recognizer.get(), &frame,
                                        &detection.boxes[candidate], &result, &error)) {
                    detection_destroy(&detection);
                    frame_destroy(&frame);
                    throw std::runtime_error(error == nullptr ?
                        "recognition failed" : error);
                }
                if (recognizer) {
                    known += result.known ? 1U : 0U;
                }
                observations.push_back({frame_info.index, frame_info.timestamp_ms,
                                        detection.boxes[candidate],
                                        result.predicted_speed, result.confidence});
            }
            const std::vector<size_t> track_ids = tracker.update(observations);
            if (recognizer) {
                for (size_t candidate = 0; candidate < detection.count; ++candidate) {
                    const Recognition &result = candidate_results[candidate];
                    const Candidate &box = detection.boxes[candidate];
                    predictions << sample << ',' << frame_info.index << ','
                                << std::fixed << std::setprecision(3)
                                << frame_info.timestamp_ms << ',' << candidate << ','
                                << track_ids[candidate] << ',' << box.x << ',' << box.y
                                << ',' << box.width << ',' << box.height << ','
                                << result.speed << ',' << result.predicted_speed << ','
                                << static_cast<double>(result.confidence) / 1000.0 << ','
                                << (result.known ? "true" : "false") << '\n';
                    require_stream(predictions, "could not write video recognition summary");
                }
            }
            frames << sample << ',' << frame_info.index << ',' << std::fixed
                   << std::setprecision(3) << frame_info.timestamp_ms << ','
                   << detection.count << ',' << known << ','
                   << (recognizer ? detection.count - known : 0U) << '\n';
            require_stream(frames, "could not write video frame summary");
            std::cout << "Sample " << sample << ": frame=" << frame_info.index
                      << " time_ms=" << std::fixed << std::setprecision(1)
                      << frame_info.timestamp_ms << " candidates=" << detection.count
                      << " known=" << known << '\n';
            total_candidates += detection.count;
            total_known += known;
            ++sample;
            detection_destroy(&detection);
            frame_destroy(&frame);
        }
        if (sample == 0) {
            throw std::runtime_error("video contained no decodable frames");
        }
        frames.flush();
        require_stream(frames, "could not finalize video frame summary");
        if (recognizer) {
            predictions.flush();
            require_stream(predictions, "could not finalize video recognition summary");
        }
        const std::vector<TrackSummary> tracks = tracker.summaries();
        std::ofstream track_output(options.output_directory / "tracks.csv");
        std::ofstream events(options.output_directory / "events.csv");
        require_stream(track_output, "could not open track summary");
        require_stream(events, "could not open event summary");
        const char *track_header = "track_id,start_frame,end_frame,start_ms,end_ms,"
                                   "observations,speed,speed_observations,"
                                   "mean_confidence,confirmed\n";
        track_output << track_header;
        events << "event_id," << track_header;
        size_t event_count = 0;
        for (const TrackSummary &track : tracks) {
            std::ostringstream row;
            row << track.id << ',' << track.start_frame << ',' << track.end_frame << ','
                << std::fixed << std::setprecision(3) << track.start_ms << ','
                << track.end_ms << ',' << track.observations << ',' << track.speed << ','
                << track.speed_observations << ','
                << static_cast<double>(track.mean_confidence) / 1000.0 << ','
                << (track.confirmed ? "true" : "false") << '\n';
            track_output << row.str();
            if (track.confirmed) {
                events << event_count++ << ',' << row.str();
            }
        }
        track_output.flush();
        events.flush();
        require_stream(track_output, "could not finalize track summary");
        require_stream(events, "could not finalize event summary");
        std::cout << "Decoded: " << decoded << "\nSamples: " << sample
                  << "\nCandidates: " << total_candidates
                  << "\nAccepted predictions: " << total_known
                  << "\nConfirmed events: " << event_count << '\n';
        return EXIT_SUCCESS;
    } catch (const std::exception &exception) {
        std::cerr << "Video processing failed: " << exception.what() << '\n';
        return EXIT_FAILURE;
    }
}
