#include "../cpp/tracker.hpp"

#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {

unsigned int checks = 0;

void check(bool condition, const char *message)
{
    ++checks;
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

TrackObservation observation(size_t frame, double timestamp, size_t x,
                             unsigned int speed, unsigned int confidence)
{
    return {frame, timestamp, {x, 10U, 20U, 20U, 100U}, speed, confidence};
}

} // namespace

int main()
{
    TemporalTracker tracker(100U, 600.0, 2U, 500U);
    std::vector<size_t> ids = tracker.update({observation(0U, 0.0, 10U, 20U, 700U)});
    check(ids.size() == 1U && ids[0] == 0U, "first observation starts track zero");
    TrackSummary summary = tracker.summaries()[0];
    check(!summary.confirmed && summary.observations == 1U,
          "one strong observation is not confirmed");

    ids = tracker.update({observation(1U, 250.0, 12U, 20U, 500U)});
    check(ids[0] == 0U, "overlapping observation joins existing track");
    summary = tracker.summaries()[0];
    check(summary.confirmed && summary.speed == 20U &&
          summary.speed_observations == 2U && summary.mean_confidence == 600U,
          "consistent evidence confirms mean confidence");

    ids = tracker.update({observation(2U, 500.0, 100U, 30U, 900U)});
    check(ids[0] == 1U && tracker.summaries().size() == 2U,
          "spatially separate candidate starts another track");
    ids = tracker.update({observation(3U, 1250.0, 101U, 30U, 900U)});
    check(ids[0] == 2U, "expired spatial track is not revived");

    TemporalTracker flicker(100U, 600.0, 2U, 500U);
    check(flicker.update({observation(0U, 0.0, 10U, 30U, 800U)})[0] == 0U,
          "flicker track starts");
    check(flicker.update({observation(1U, 250.0, 11U, 70U, 800U)})[0] == 0U,
          "different reading still associates spatially");
    check(!flicker.summaries()[0].confirmed,
          "two conflicting speeds do not confirm an event");

    TemporalTracker unknown_votes(100U, 600.0, 2U, 500U);
    unknown_votes.update({observation(0U, 0.0, 10U, 50U, 550U)});
    unknown_votes.update({observation(1U, 250.0, 11U, 50U, 550U)});
    check(unknown_votes.summaries()[0].confirmed,
          "repeated sub-frame-threshold predictions can confirm temporally");

    bool rejected = false;
    try {
        TemporalTracker invalid(0U, 100.0, 1U, 0U);
    } catch (const std::invalid_argument &) {
        rejected = true;
    }
    check(rejected, "zero IoU threshold is rejected");
    rejected = false;
    try {
        tracker.update({observation(4U, 100.0, 10U, 20U, 500U)});
    } catch (const std::invalid_argument &) {
        rejected = true;
    }
    check(rejected, "decreasing timestamps are rejected");

    std::cout << "PASS: " << checks << " temporal tracker checks\n";
    return EXIT_SUCCESS;
}
