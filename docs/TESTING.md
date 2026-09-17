# Testing the current project

Run these commands from the repository root in PowerShell. Generated programs,
models, dependencies, datasets, and reports remain below ignored paths and must
not be committed.

## 1. Dependency-free regression suite

This verifies that the C17 core and C++ tracker still build without OpenCV:

```powershell
cmake -S . -B output/test-cmake -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build output/test-cmake
ctest --test-dir output/test-cmake --output-on-failure
```

Expected current result: six CTest targets pass (`frame`, `detect`,
`image_bridge`, `tracker`, `cli`, and `video_evaluation`).

The original strict GCC scripts remain an independent compatibility check:

```powershell
.\build.ps1 -Test
```

Expected current result: 437 core checks, 23 CLI cases, and 32 detector checks.

## 2. OpenCV regression suite

Build the local compiler-compatible dependency once if it is not already under
`output/deps`:

```powershell
.\tools\build_opencv.ps1
```

Then configure and test the complete build:

```powershell
cmake -S . -B output/test-cmake-opencv -G Ninja `
  -DCMAKE_BUILD_TYPE=Release `
  -DOpenCV_DIR="$PWD/output/deps/opencv-install/x64/mingw/staticlib" `
  -DTRAFFIC_SIGN_REQUIRE_OPENCV=ON
cmake --build output/test-cmake-opencv
ctest --test-dir output/test-cmake-opencv --output-on-failure
```

Expected current result: nine CTest targets pass. This adds the OpenCV adapter,
recognizer, and video adapter tests to the six dependency-free targets.

## 3. Train and evaluate recognition

The local `archive/car` dataset is required. Train a fresh model:

```powershell
.\output\test-cmake-opencv\traffic_sign_train_recognizer.exe `
  .\archive\car .\output\speed-recognizer.yml
```

Run the frozen validation evaluation into a fresh directory:

```powershell
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
.\tools\evaluate_recognition.ps1 -Split valid `
  -Executable .\output\test-cmake-opencv\traffic_sign_assistant.exe `
  -Model .\output\speed-recognizer.yml `
  -OutputDirectory ".\output\recognition-valid-$stamp"
```

The current baseline localizes 7 of 10 labeled signs in the frozen 12-image
validation sample. Three localized predictions are accepted and all three are
correct; four localized signs remain unknown. Treat this as a regression sample,
not a statistically strong benchmark.

## 4. Negative video smoke test

`archive/video.mp4` is unlabeled. Use it only to verify decoding, sampling,
tracking, artifact generation, and conservative event suppression:

```powershell
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
.\tools\run_video_demo.ps1 -Video .\archive\video.mp4 `
  -Executable .\output\test-cmake-opencv\traffic_sign_video.exe `
  -Model .\output\speed-recognizer.yml `
  -OutputDirectory ".\output\video-negative-$stamp" `
  -SampleMs 250
```

The Stage 7 smoke baseline decodes 508 frames, samples 68, finds five isolated
candidates, and confirms no event. These counts may expose regressions but are
not accuracy measurements.

## 5. Positive temporal-confirmation smoke test

Create a local three-frame sequence from the known validation fixture and pass
its numbered filename pattern to OpenCV:

```powershell
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$source = '.\archive\car\valid\images\000062_jpg.rf.cc95cfe9c2ec51e7e600b32dad431408.jpg'
0..2 | ForEach-Object {
  Copy-Item -LiteralPath $source `
    -Destination (".\output\positive-$stamp-{0:D2}.jpg" -f $_)
}
$pattern = ".\output\positive-$stamp-%02d.jpg"
$destination = ".\output\video-positive-$stamp"
.\output\test-cmake-opencv\traffic_sign_video.exe `
  $pattern $destination `
  --recognize .\output\speed-recognizer.yml --sample-ms 40
Import-Csv "$destination\events.csv" | Format-Table -AutoSize
Get-Content "$destination\confirmed-events.txt"
```

Expected result: one speed-30 event with three observations, mean confidence
0.76, and confirmation at 40 ms.

## 6. Quantitative labeled-video evaluation

Do not add `archive/video.mp4` to the manifest without independent labels. Add
new videos and timestamped boxes according to
`evaluation/video-annotations.schema.json`, assign drive-level splits in
`evaluation/video-manifest.csv`, and tune only with training/validation videos.

After the test policy is frozen:

```powershell
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
.\tools\evaluate_video.ps1 -Split test `
  -Executable .\output\test-cmake-opencv\traffic_sign_video.exe `
  -Model .\output\speed-recognizer.yml `
  -OutputDirectory ".\output\video-evaluation-test-$stamp"
```

Inspect `summary.json` and `metrics.csv` for event TP/FP/FN,
precision/recall/F1, recognition accuracy, unknown observations,
detection/confirmation latency, and throughput. Also inspect matched frame and
track artifacts before drawing conclusions from the aggregate values.

## 7. Final repository checks

```powershell
git diff --check
git status --short
```

Only source, tests, schemas, and documentation should appear. Files under
`output/`, local datasets, recognizer models, OpenCV dependencies, and FFmpeg
binaries are ignored.
