# Stage 7: video evaluation and demo packaging

## Reproducible workflow

Stage 7 separates three activities that answer different questions:

1. `tools/evaluate_recognition.ps1` keeps the frozen still-image localization and
   recognition regression check.
2. `tools/evaluate_video.ps1` runs labeled videos and scores final temporal
   events against timestamped boxes.
3. `tools/run_video_demo.ps1` is a convenient qualitative demo. It is not an
   accuracy benchmark unless its input has been independently annotated and
   added to the evaluation manifest.

The tracked `evaluation/video-manifest.csv` intentionally contains only its
header. In particular, `archive/video.mp4` has no ground-truth row. It remains a
negative/qualitative pipeline smoke input and contributes no accuracy numbers.

Run a labeled test split into a fresh directory:

```powershell
.\tools\evaluate_video.ps1 -Split test `
  -OutputDirectory .\output\video-evaluation-test-20260917
```

The evaluator invokes the OpenCV video executable at 250 ms sampling by
default, loads `recognition.csv`, `tracks.csv`, `events.csv`, and
`run-summary.csv`, and writes `metrics.csv` plus aggregate `summary.json`.
`-SkipRun` rescoring is available for existing outputs and is covered by a
synthetic regression test.

For a complete build-to-smoke-test checklist, see [TESTING.md](TESTING.md).

## Annotation format

`evaluation/video-annotations.schema.json` defines schema version 1. Each
manifest row has `id,split,video,annotations`; relative paths are resolved from
the manifest directory. Splits must be assigned before tuning. Use `train` and
`valid` for detector, recognition, sampling, association, and confirmation
choices. Keep `test` frozen for final reporting.

Each JSON event records a unique ID, the true speed, its first and last visible
timestamps, and one or more timestamped `x,y,width,height` boxes in decoded
video pixel coordinates. Annotate enough frames to cover movement and scale
changes, especially near the first visible and expected confirmation times.
The evaluator allows a box timestamp difference of half the sampling interval
by default and greedily matches tracks to truth events at maximum IoU, requiring
IoU 0.50. Timestamped boxes distinguish simultaneous signs that a time-only
annotation could not.

Example shape (illustrative only, not a label for a repository video):

```json
{
  "schema_version": 1,
  "video_id": "drive-001",
  "events": [{
    "id": "sign-1",
    "speed": 50,
    "first_visible_ms": 1200,
    "last_visible_ms": 3100,
    "observations": [
      {"timestamp_ms": 1250, "x": 280, "y": 90, "width": 24, "height": 27},
      {"timestamp_ms": 2500, "x": 250, "y": 70, "width": 43, "height": 47}
    ]
  }]
}
```

## Metrics

A ground-truth event is a true positive only when it is spatially matched to a
confirmed track and the final speed is correct. A wrong-speed confirmed match
counts as both a false positive and a false negative for multiclass event
scoring. Every other confirmed track is a false positive; every truth event
without a correct confirmed track is a false negative.

- **Event precision, recall, F1:** computed from those TP/FP/FN counts.
- **Recognition accuracy:** correct / (correct + wrong) among confirmed tracks
  spatially matched to truth. Unconfirmed events are reported separately.
- **Unknown observations:** matched-track candidate rows for which Stage 4 kept
  `known=false`; the raw winning speed is still available to temporal evidence.
- **Detection latency:** track start time minus the annotated first-visible time.
- **Confirmation latency:** the first time the final winning speed met the hit
  and mean-confidence policy, minus first-visible time.
- **Throughput:** decoded frames per wall-clock processing second. The run also
  reports samples per second and video-time/wall-time ratio. This includes
  detection, recognition, artifact serialization, and decoding on that machine.

Latency may be slightly negative when sparse annotation places first visibility
after a nearby sample; denser annotations reduce this quantization error.
Aggregate latency is currently a mean of per-video means so long videos do not
silently dominate.

## Demo outputs

```powershell
.\tools\run_video_demo.ps1 -Video .\archive\video.mp4
```

The runner requires a fresh output directory (or creates a timestamped one),
prints a compact event table, and lists the artifact paths. The video executable
also writes `confirmed-events.txt` for people, while retaining `frames.csv`,
`recognition.csv`, `tracks.csv`, and `events.csv` for diagnosis. `run-summary.csv`
captures counts and throughput.

## Adding labeled videos

1. Obtain permission to use the video and keep large/local media outside Git if
   redistribution is not appropriate.
2. Assign its `train`, `valid`, or frozen `test` split before looking at test
   results. Do not reuse near-duplicate drives across splits.
3. Record every in-scope speed sign as one event. Mark first/last visibility and
   timestamped boxes in original decoded coordinates; have a second person
   review ambiguous, occluded, and simultaneous signs when possible.
4. Validate paths and annotations by running the evaluator. Inspect the matched
   frame artifacts and `metrics.csv`, not only the aggregate score.
5. Tune only on training/validation material. Run the test split after the
   policy is frozen and record hardware, build type, model provenance, sampling
   interval, and command.

## Benchmark limitations

- No labeled video is distributed or locally declared in the manifest yet, so
  Stage 7 establishes the workflow but makes no video-level accuracy claim.
- The still-image dataset and recognizer remain imbalanced and region-specific.
- Detection recall remains the likely end-to-end bottleneck.
- Greedy axis-aligned IoU can split fast-moving or rapidly growing signs and can
  confuse nearby overlapping candidates. The evaluator exposes these failures;
  its default is not evidence that the tracker threshold is optimal.
- Sparse box annotations introduce matching and latency uncertainty. They do
  not measure how a system should behave after a sign leaves view.
- Throughput depends on CPU, codec backend, build type, storage, output volume,
  and sampling interval; report the environment with results.
