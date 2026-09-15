# Project progress

Last updated: 2026-09-15

## Current status

Milestone 1 is complete. Later stages are proposed and have not started;
the next implementation stage awaits selection.

## Milestones

- [x] **1. Load one image frame in C17.** Accept a path, validate a binary PPM
  header, read dimensions and maximum color value, allocate packed RGB pixels
  into a Frame, print metadata and sample pixels, and release resources.
  Includes strict compiler warnings, a sample image, tests, and build instructions.
- [ ] **2. Basic image processing in C.** Grayscale conversion, resizing, and
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

## Update convention

Update this file when a stage starts or finishes. Record the implemented
behavior, verification results, and remaining limitations before marking it
complete. Proposed stages may be refined when selected.
