#include "tracker.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <limits>
#include <stdexcept>
#include <utility>

struct TemporalTracker::Track {
    struct Evidence {
        size_t hits = 0U;
        unsigned long long confidence_sum = 0U;
        double confirmation_ms = -1.0;
    };

    size_t id;
    size_t start_frame;
    size_t end_frame;
    double start_ms;
    double end_ms;
    Candidate last_box;
    size_t observations;
    bool active;
    std::map<unsigned int, Evidence> evidence;
};

TemporalTracker::~TemporalTracker() = default;

namespace {

double intersection_over_union(const Candidate &left, const Candidate &right)
{
    const double left_x2 = static_cast<double>(left.x) +
                           static_cast<double>(left.width);
    const double left_y2 = static_cast<double>(left.y) +
                           static_cast<double>(left.height);
    const double right_x2 = static_cast<double>(right.x) +
                            static_cast<double>(right.width);
    const double right_y2 = static_cast<double>(right.y) +
                            static_cast<double>(right.height);
    const double intersection_width = std::max(0.0,
        std::min(left_x2, right_x2) -
        std::max(static_cast<double>(left.x), static_cast<double>(right.x)));
    const double intersection_height = std::max(0.0,
        std::min(left_y2, right_y2) -
        std::max(static_cast<double>(left.y), static_cast<double>(right.y)));
    const double intersection = intersection_width * intersection_height;
    const double left_area = static_cast<double>(left.width) *
                             static_cast<double>(left.height);
    const double right_area = static_cast<double>(right.width) *
                              static_cast<double>(right.height);
    const double union_area = left_area + right_area - intersection;
    return union_area > 0.0 ? intersection / union_area : 0.0;
}

} // namespace

void TemporalTracker::add_evidence(Track &track,
                                   const TrackObservation &observation)
{
    if (track.observations == std::numeric_limits<size_t>::max()) {
        throw std::overflow_error("tracker observation count overflow");
    }
    track.end_frame = observation.source_frame;
    track.end_ms = observation.timestamp_ms;
    track.last_box = observation.box;
    ++track.observations;
    if (observation.predicted_speed != 0U) {
        auto &entry = track.evidence[observation.predicted_speed];
        if (entry.hits == std::numeric_limits<size_t>::max() ||
            entry.confidence_sum >
                std::numeric_limits<unsigned long long>::max() -
                    observation.confidence) {
            throw std::overflow_error("tracker confidence sum overflow");
        }
        ++entry.hits;
        entry.confidence_sum += observation.confidence;
        const unsigned int mean = static_cast<unsigned int>(
            (entry.confidence_sum + entry.hits / 2U) / entry.hits);
        if (entry.confirmation_ms < 0.0 && entry.hits >= confirmation_hits_ &&
            mean >= confirmation_confidence_) {
            entry.confirmation_ms = observation.timestamp_ms;
        }
    }
}

TemporalTracker::TemporalTracker(unsigned int minimum_iou,
                                 double maximum_gap_ms,
                                 size_t confirmation_hits,
                                 unsigned int confirmation_confidence)
    : minimum_iou_(minimum_iou), maximum_gap_ms_(maximum_gap_ms),
      confirmation_hits_(confirmation_hits),
      confirmation_confidence_(confirmation_confidence)
{
    if (minimum_iou == 0U || minimum_iou > 1000U ||
        !std::isfinite(maximum_gap_ms) ||
        maximum_gap_ms < 0.0 || confirmation_hits == 0U ||
        confirmation_confidence > 1000U) {
        throw std::invalid_argument("invalid temporal tracker configuration");
    }
}

std::vector<size_t> TemporalTracker::update(
    const std::vector<TrackObservation> &observations)
{
    if (observations.empty()) {
        return {};
    }
    const double timestamp = observations.front().timestamp_ms;
    if (!std::isfinite(timestamp) || timestamp < 0.0) {
        throw std::invalid_argument("invalid observation timestamp");
    }
    for (const TrackObservation &observation : observations) {
        if (!std::isfinite(observation.timestamp_ms) ||
            observation.timestamp_ms != timestamp || observation.box.width == 0U ||
            observation.box.height == 0U || observation.confidence > 1000U) {
            throw std::invalid_argument("invalid or mixed tracker observations");
        }
    }
    for (Track &track : tracks_) {
        if (track.end_ms > timestamp) {
            throw std::invalid_argument("tracker timestamps must not decrease");
        }
        if (track.active && timestamp - track.end_ms > maximum_gap_ms_) {
            track.active = false;
        }
    }

    struct Pair {
        size_t track;
        size_t observation;
        double score;
    };
    std::vector<Pair> pairs;
    const double threshold = static_cast<double>(minimum_iou_) / 1000.0;
    for (size_t track = 0; track < tracks_.size(); ++track) {
        if (!tracks_[track].active) {
            continue;
        }
        for (size_t observation = 0; observation < observations.size(); ++observation) {
            const double score = intersection_over_union(
                tracks_[track].last_box, observations[observation].box);
            if (score >= threshold) {
                pairs.push_back({track, observation, score});
            }
        }
    }
    std::sort(pairs.begin(), pairs.end(), [](const Pair &left, const Pair &right) {
        if (left.score != right.score) { return left.score > right.score; }
        if (left.track != right.track) { return left.track < right.track; }
        return left.observation < right.observation;
    });
    std::vector<bool> used_tracks(tracks_.size(), false);
    std::vector<bool> used_observations(observations.size(), false);
    std::vector<size_t> result(observations.size(), 0U);
    for (const Pair &pair : pairs) {
        if (used_tracks[pair.track] || used_observations[pair.observation]) {
            continue;
        }
        used_tracks[pair.track] = true;
        used_observations[pair.observation] = true;
        add_evidence(tracks_[pair.track], observations[pair.observation]);
        result[pair.observation] = tracks_[pair.track].id;
    }
    for (size_t observation = 0; observation < observations.size(); ++observation) {
        if (used_observations[observation]) {
            continue;
        }
        const TrackObservation &source = observations[observation];
        Track track = {tracks_.size(), source.source_frame, source.source_frame,
                       source.timestamp_ms, source.timestamp_ms, source.box,
                       0U, true, {}};
        add_evidence(track, source);
        result[observation] = track.id;
        tracks_.push_back(std::move(track));
    }
    return result;
}

std::vector<TrackSummary> TemporalTracker::summaries() const
{
    std::vector<TrackSummary> result;
    result.reserve(tracks_.size());
    for (const Track &track : tracks_) {
        unsigned int best_speed = 0;
        size_t best_hits = 0;
        unsigned long long best_confidence_sum = 0;
        for (const auto &[speed, evidence] : track.evidence) {
            if (evidence.confidence_sum > best_confidence_sum ||
                (evidence.confidence_sum == best_confidence_sum &&
                 evidence.hits > best_hits)) {
                best_speed = speed;
                best_hits = evidence.hits;
                best_confidence_sum = evidence.confidence_sum;
            }
        }
        const unsigned int mean = best_hits == 0U ? 0U :
            static_cast<unsigned int>((best_confidence_sum + best_hits / 2U) /
                                      best_hits);
        const bool confirmed = best_speed != 0U && best_hits >= confirmation_hits_ &&
                               mean >= confirmation_confidence_;
        const double confirmation_ms = confirmed ?
            track.evidence.at(best_speed).confirmation_ms : -1.0;
        result.push_back({track.id, track.start_frame, track.end_frame,
                          track.start_ms, track.end_ms, track.observations,
                          best_speed, best_hits, mean, confirmation_ms, confirmed});
    }
    return result;
}
