# Stage 5: video decoding and frame sampling

## Design

Stage 5 adds video without changing the still-image algorithms. OpenCV
`VideoCapture` stays inside `cpp/video_adapter.cpp`; `video_adapter.h` exposes an
opaque reader and plain C metadata structures. Each decoded frame is copied from
OpenCV's BGR/BGRA/grayscale representation into the existing owned packed RGB
`Frame`. The caller releases it with `frame_destroy` exactly like an image.

`traffic_sign_video` is a C++17 orchestration executable. It creates an output
directory, decodes sequentially, chooses frames by timestamp, and calls the C
detector plus the C-facing Stage 4 recognizer. The C core is still compiled as
C17. This keeps codec and filesystem conveniences in C++ while preserving one
processing implementation for images and video.

## Usage

```powershell
.\output\cmake-opencv\traffic_sign_video.exe `
  <video> <output-directory> `
  [--recognize MODEL] [--confidence N] `
  [--sample-ms N] [--max-samples N]
```

The default interval is 1,000 ms. The first frame is sampled, then the earliest
decoded frame at or after each interval boundary is processed. `--max-samples 0`
or omission means no sample limit. All Stage 3 detector options are also
accepted. The program decodes sequentially instead of repeatedly seeking, which
works predictably with inter-frame compressed formats but still incurs decoding
cost for skipped frames.

The output directory contains:

- `frames.csv`: sample number, source frame, timestamp, candidate count, known
  count, and unknown count;
- `recognition.csv`: one row per candidate when a model is supplied, including
  track ID, accepted and raw predicted speed, vote-share confidence, and
  known/unknown status;
- Stage 6 `tracks.csv` and deduplicated confirmed `events.csv`; and
- normal Stage 3 masks, annotated PPM, boxes CSV, and candidate crops under a
  `frame-NNNNNN` prefix for every sampled frame.

Use a new output directory for each run. Existing matching files are overwritten,
but old candidate crops whose IDs no longer appear are not deleted.

## Dependency and runtime

The OpenCV build now includes `videoio`. On Windows, the helper enables OpenCV's
pinned prebuilt FFmpeg wrapper, whose license files are installed with OpenCV.
The dependency and DLL remain below ignored `output/`; CMake copies the wrapper
beside `traffic_sign_video.exe` so the backend is discoverable at runtime. Other
platforms may use the FFmpeg or GStreamer backend supplied by their OpenCV build.

## Verification

`tests/test_video_adapter.cpp` generates a lossless three-frame PNG sequence and
checks reported dimensions, frame indexes, exact RGB pixels, EOF behavior, and
invalid inputs without relying on repository datasets or a lossy codec. The
video adapter remains one of the current mandatory-OpenCV configuration's eight
CTest targets.

The end-to-end smoke run used the included 416x416 MP4:

```powershell
.\output\cmake-opencv\traffic_sign_video.exe `
  .\archive\video.mp4 .\output\video-full `
  --recognize .\output\speed-recognizer.yml --sample-ms 1000
```

OpenCV reported 508 frames at 30 FPS. The program decoded all 508 and processed
17 samples from 0 through 16 seconds. It found one candidate at six seconds;
the conservative recognizer rejected it as unknown. This proves decoding and
pipeline reuse, but the clip does not provide labeled video accuracy evidence.

## Limitations

- A failed decode is reported by OpenCV through the same read result as normal
  end-of-stream, so late corruption may shorten a run without a distinct error.
- Short sampling intervals generate many PPM masks, crops, and annotated frames.
- Sampling does not seek, parallelize, or preserve audio.
- Decoding and sampling remain independent of Stage 6 association; tracker
  limitations and defaults are documented in [STAGE6_TRACKING.md](STAGE6_TRACKING.md).
