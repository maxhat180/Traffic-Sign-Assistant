#ifndef TRAFFIC_SIGN_TRACKER_HPP
#define TRAFFIC_SIGN_TRACKER_HPP

#include "../detect.h"

#include <cstddef>
#include <vector>

struct TrackObservation {
    size_t source_frame;
    double timestamp_ms;
    Candidate box;
    unsigned int predicted_speed;
    unsigned int confidence;
};

struct TrackSummary {
    size_t id;
    size_t start_frame;
    size_t end_frame;
    double start_ms;
    double end_ms;
    size_t observations;
    unsigned int speed;
    size_t speed_observations;
    unsigned int mean_confidence;
    bool confirmed;
};

class TemporalTracker {
public:
    TemporalTracker(unsigned int minimum_iou, double maximum_gap_ms,
                    size_t confirmation_hits,
                    unsigned int confirmation_confidence);
    ~TemporalTracker();

    /* Associate one timestamp's observations and return a track ID per input. */
    std::vector<size_t> update(const std::vector<TrackObservation> &observations);
    std::vector<TrackSummary> summaries() const;

private:
    struct Track;
    static void add_evidence(Track &track, const TrackObservation &observation);
    unsigned int minimum_iou_;
    double maximum_gap_ms_;
    size_t confirmation_hits_;
    unsigned int confirmation_confidence_;
    std::vector<Track> tracks_;
};

#endif
