# Project progress

Last updated: 2026-09-15

## Current status

Milestones 1 through 3 are complete. Stage 4 is in progress. The mixed C/C++
boundary, CMake build, BGR-to-RGB bridge, and optional OpenCV decode/perspective
adapter are implemented. A compiler-matched OpenCV 4.13.0 build verifies PNG/JPEG
decoding and perspective normalization locally. Digit recognition, automatic
corner estimation, and confidence calibration remain.

## Milestones

- [x] **1. Load one image frame in C17.** Accept a path, validate a binary PPM
  header, read dimensions and maximum color value, allocate packed RGB pixels
  into a Frame, print metadata and sample pixels, and release resources.
  Includes strict compiler warnings, a sample image, tests, and build instructions.
- [x] **2. Basic image processing in C.** Grayscale conversion, resizing, and
  saving processed images; add real sign images with expected test outputs.
- [x] **3. Find candidate signs in still images.** Identify red regions, group
  connected pixels, filter by shape and size, and output candidate crops.
- [ ] **4. Recognize speed limits.** Select an OCR or recognition library and
  report the speed and confidence, with an unknown result when uncertain.
  In progress: mixed C/C++ architecture and the image/geometry adapter are added.
- [ ] **5. Process video.** Add a decoder, sample frames, and reuse the
  still-image pipeline.
- [ ] **6. Track results over time.** Combine detections across frames to reduce
  flickering and repeated reports.
- [ ] **7. Evaluate and package.** Measure accuracy and processing speed on
  labeled examples, document limitations, and prepare a reproducible demo.

## Milestone 1 verification

- GCC C17 build passes with `-Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror`.
- Loader suite passes 360 checks on the current Windows/MinGW environment.
- Sample-image CLI output passes; missing and extra arguments, a missing file,
  and an invalid image produce failure statuses.
- Tests cover pixel layout, comments, binary raster boundaries, consecutive
  frames, malformed/truncated inputs, numeric overflow, and frame cleanup.
- Supports P6 with maximum color values 1..255; 16-bit samples and P3 are
  rejected. Loads only the first frame. This milestone did not implement detection.

Run `./build.ps1 -Test` in PowerShell to rebuild and verify.
See [README.md](README.md) for input constraints and memory ownership details.

## Stage 2 verification

- Added in-place weighted grayscale, nearest-neighbor resize with checked
  allocation/coordinate arithmetic, and P6 saving.
- CLI supports `--grayscale`, `--resize WIDTH HEIGHT`, and `--output PATH`.
- Strict C17 build passes; combined C suite passes 437 checks, plus 12 CLI
  cases and exact saved-pixel verification.
- Prepared local bright 30 and darker/blurrier 50 sign examples. Both demos
  passed independent checks of all 43,264 output pixels per image.
- Dataset folders, ZIPs, converted inputs, and generated outputs remain ignored.
- Limits: grayscale stays RGB/P6; resizing has no interpolation or automatic
  aspect-ratio preservation; optional JPEG preparation uses Windows System.Drawing.

## Stage 3 verification

- Implemented normalized red thresholding, conservative mask cleanup,
  iterative eight-connected grouping, and configurable area/size/aspect/fill filters.
- Outputs raw/clean masks, annotated image, CSV boxes, and padded color crops.
- Strict C17 build passes: 437 frame/processing checks, 32 detector checks,
  and 18 CLI cases including exact bounding boxes and crop pixels.
- Chose thresholds on 12 training images; checked frozen settings on 12 separate
  validation images. After deduplicating overlapping location labels, training
  matched 7/9 sign locations with 24 extra candidates; validation matched 7/10
  with 21 extra candidates. Matching requires IoU >= 0.5.
- Recorded perspective-related misses, merged adjacent signs, and false
  candidates from other objects. See docs/STAGE3_EVALUATION.md.
- Datasets and all generated artifacts remain local and ignored by Git.
- Next: C++/OpenCV image input and geometric normalization, then digit recognition.

## Stage 4 progress

- Added a C-compatible `image_adapter.h` boundary. Existing C headers now use
  C++ linkage guards, while no OpenCV or C++ type leaks into the C core.
- Added a dependency-free C++ BGR-to-RGB bridge plus 18 checks for exact channel
  order, padded row stride, failure atomicity, and C/C++ allocation ownership.
- Added optional OpenCV JPEG/PNG loading and four-point perspective normalization.
  Exceptions are contained inside the adapter and mapped to static diagnostics.
- Added CMake targets for the C17 core, C++17 bridge, CLI, and all tests. A
  dependency-free CMake/Ninja build passes four test targets; the legacy strict
  C17 build still passes 437 core checks, 18 CLI cases, and 32 detector checks.
- Built OpenCV 4.13.0 locally with the same MinGW compiler. The mandatory-OpenCV
  configuration compiles and links the adapter with strict warnings and passes
  five CTest targets. Its 33 adapter checks cover exact PNG-to-RGB decoding,
  known quadrilateral rectification, invalid geometry, and failure atomicity.
- A real 416x416 validation JPEG also loads through the OpenCV-backed CLI.
- Added `tools/build_opencv.ps1` to reproduce the minimal compiler-compatible
  dependency under ignored `output/deps` from a fresh checkout.
- Next: estimate sign corners automatically, extract normalized inner sign
  regions, then choose and evaluate recognition and confidence thresholds.

## Update convention

Update this file when a stage starts or finishes. Record the implemented
behavior, verification results, and remaining limitations before marking it
complete. Proposed stages may be refined when selected.
