# Stage 6: temporal tracking and deduplicated events

## Goal and data flow

A single frame can miss a sign, fluctuate between speeds, or confidently classify
a false detector region. Stage 6 keeps the per-frame outputs for diagnosis but
adds a precision-oriented event layer:

```text
decoded sample -> detector boxes -> raw recognizer votes -> spatial tracks
                -> per-speed evidence within each track -> confirmed event
```

The tracker is dependency-free C++17. It consumes the C `Candidate` structure and
does not depend on OpenCV. `traffic_sign_video` supplies each sampled timestamp's
boxes and recognition results.

## Association

Before processing a timestamp, tracks older than the maximum gap expire. Every
active-track/current-box pair at or above the IoU threshold becomes a candidate
match. Pairs are processed by descending IoU, with each track and box used at
most once. Unmatched boxes start new tracks.

Defaults:

| Option | Default | Meaning |
| --- | ---: | --- |
| `--track-iou` | 100 | Minimum intersection-over-union, 0.10 in per mille |
| `--track-gap-ms` | 2 x sample interval | Maximum time between observations |
| `--confirm-hits` | 2 | Matching raw speed observations required |
| `--track-confidence` | 500 | Required mean vote share, 0.50 in per mille |

The gap can bridge one missing sampled detection. Lower IoU tolerates motion but
increases the chance of joining nearby objects.

## Evidence and confirmation

Stage 4 still emits accepted `speed=0` when a prediction is below its per-frame
threshold. It now also exposes `predicted_speed`, the forest's winning class
before rejection. Each track accumulates observation count and confidence sum
separately for every raw speed. The speed with the highest summed confidence is
the track result. It becomes a confirmed event only when that speed has enough
observations and its mean confidence meets the temporal threshold.

This means two consistent 0.55 readings may confirm even though each is unknown
under the Stage 4 0.60 threshold, while a single 0.70 reading does not. Two
different 0.80 readings do not confirm either because neither speed has two hits.

## Outputs

- `recognition.csv` adds `track_id` and `predicted_speed` to every candidate.
- `tracks.csv` records every track's frame/time span, total observations, winning
  speed evidence, mean confidence, first confirmation time, and final decision.
- `events.csv` contains only confirmed tracks, with one row per physical track.

These files are finalized at end-of-stream. Stage 6 does not yet stream events
to an external consumer.

## Verification

`tests/test_tracker.cpp` deterministically checks association, spatial separation,
expiry, conflicting speeds, repeated sub-threshold evidence, invalid thresholds,
and timestamp ordering. It runs in both dependency-free and OpenCV builds.

On the included MP4 at 250 ms sampling, detection produced five isolated
candidates. One frame independently accepted speed 20 at 0.70, but every track
had only one observation, so `events.csv` remained header-only. This is the
intended suppression of the single-frame result observed in Stage 5.

A positive end-to-end smoke test repeated the known validation 30-sign image for
three frames. All three detections joined track 0 and produced exactly one event:
speed 30, three supporting observations, and 0.76 mean confidence.

## Limitations

- Axis-aligned IoU alone can split a rapidly moving/expanding sign whose boxes no
  longer overlap. Motion prediction or center-distance gating would improve this.
- Two nearby signs can be joined if their boxes overlap and cross.
- The defaults intentionally favor precision and may reject a real sign visible
  in only one sampled frame.
- Smoke sequences are not a labeled video benchmark. Stage 7 must measure event
  precision/recall and latency before the tracker is considered road-ready.
