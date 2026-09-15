# Project progress

Last updated: 2026-09-15

## Current status

Milestones 1 and 2 are complete. Stage 3 (candidate sign regions in still
images) is the proposed next stage and has not started.

## Milestones

- [x] **1. Load one image frame in C17.** Accept a path, validate a binary PPM
  header, read dimensions and maximum color value, allocate packed RGB pixels
  into a Frame, print metadata and sample pixels, and release resources.
  Includes strict compiler warnings, a sample image, tests, and build instructions.
- [x] **2. Basic image processing in C.** Grayscale conversion, resizing, and
  saving processed images; add real sign images with expected test outputs.
- [ ] **3. Find candidate signs in still images.** Identify red regions, group
  connected pixels, filter by shape and size, and output candidate crops.
- [ ] **4. Recognize speed limits.** Select an OCR or recognition library and
  report the speed and confidence, with an unknown result when uncertain.
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
  rejected. Loads only the first frame. Video and detection are not implemented.

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

## Update convention

Update this file when a stage starts or finishes. Record the implemented
behavior, verification results, and remaining limitations before marking it
complete. Proposed stages may be refined when selected.
