# Stage 4: mixed C/C++ architecture

## Status

Stage 4 is complete. It establishes and tests the language boundary, adds
optional OpenCV image decoding and quadrilateral rectification, and recognizes
12 speed classes with an OpenCV random forest. A measured tree-vote threshold
produces an explicit `unknown` result. CMake supports the mixed build without
removing the original C-only path.

## Why keep both languages

The image representation, basic transforms, red-region detector, and their
tests remain C17. They form a small, dependency-free baseline with explicit
ownership. C++17 is used only where it provides access to OpenCV's C++ API and
where the recognition implementation needs C++ libraries.

The boundary is `image_adapter.h`. Its declarations use only C-compatible
types, and `extern "C"` suppresses C++ name mangling. OpenCV's `cv::Mat` never
appears in a C header. Adapter functions catch exceptions and return the same
boolean/static-error-string contract used by the C core.

## Target graph

```text
traffic_sign_core (C17 static library)
  frame.c + image.c + detect.c
          |
          +----------------------> traffic_sign_assistant (main.c)
          |
traffic_sign_cpp_bridge (C++17 static library)
  cpp/image_bridge.cpp
          |
          +--> test_image_bridge
          |
          +--> traffic_sign_opencv (when OpenCV is found)
                 cpp/opencv_adapter.cpp + OpenCV core/imgcodecs/imgproc
                            |
                            +--> traffic_sign_assistant

traffic_sign_recognizer (C++17 static library, when OpenCV is found)
  cpp/recognizer.cpp + cpp/recognizer_features.cpp + OpenCV ml/imgproc
          |
          +--> traffic_sign_assistant
          +--> test_recognizer
          +--> traffic_sign_train_recognizer
```

Each `.c` file is compiled as C17 and each `.cpp` file as C++17. CMake chooses
the C++ linker whenever the final dependency graph includes C++ so that the C++
runtime and OpenCV dependencies are resolved. This is different from compiling
the C files as C++, which the project deliberately does not do.

## Build modes

The original `build.ps1 -Test` remains the quickest dependency-free regression
build. It invokes GCC directly and produces the PPM-only CLI in the repository
root.

The CMake path configures both languages and runs four tests without OpenCV:

```powershell
cmake -S . -B output/cmake -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build output/cmake
ctest --test-dir output/cmake --output-on-failure
```

Without OpenCV, configuration reports that it is building the PPM-only
executable. `traffic_sign_cpp_bridge` and its test still compile, so the ABI,
stride handling, BGR-to-RGB conversion, and cross-language ownership are tested.

For an OpenCV build, set `OpenCV_DIR` to the package configuration directory and
turn on the required-dependency check:

```powershell
cmake -S . -B output/cmake-opencv -G Ninja `
  -DOpenCV_DIR=C:/path/to/opencv/lib/cmake/opencv4 `
  -DTRAFFIC_SIGN_REQUIRE_OPENCV=ON
cmake --build output/cmake-opencv
ctest --test-dir output/cmake-opencv --output-on-failure
```

For the current MinGW environment, `tools/build_opencv.ps1` clones the official
tagged source and creates the compiler-compatible static install under the
ignored `output/deps` tree. It intentionally builds only `core`, `imgproc`, and
`imgcodecs`, and `ml` plus their packaging dependencies.

The OpenCV installation must have been built for the same compiler ABI,
architecture, and runtime as the application. The current verified configuration
uses MinGW GCC 15.2.0 and a static OpenCV 4.13.0 install whose package directory
is `output/deps/opencv-install/x64/mingw/staticlib`.

The mandatory-OpenCV build passes six CTest targets. The OpenCV-specific suite
contains 33 checks for exact 2-by-2 PNG decoding and RGB channel order, a known
quadrilateral rectification, non-finite input rejection, and failure atomicity.
A separate CLI smoke test loads a real 416-by-416 JPEG from the validation set.

## Adapter behavior

`frame_read_image` uses OpenCV color decoding and transfers the result to an
owned packed RGB `Frame`. OpenCV normally decodes color images as BGR, so the
channel swap is explicit and independently tested.

`frame_normalize_quad` accepts four points ordered top-left, top-right,
bottom-right, bottom-left. It computes a perspective transform and produces a
caller-sized, front-facing RGB frame. The current detector returns an
axis-aligned bounding box, not four reliable sign corners, so a future accuracy
refinement can estimate the ring contour/corners before invoking this function
in the automatic pipeline.

## Recognition behavior

`recognizer.h` is a second plain-C boundary. The caller owns a `Frame` and
`Candidate`; the opaque recognizer owns the loaded OpenCV `RTrees` model. The
implementation removes one eighth of the candidate width/height from each side,
resizes the interior to 20 by 20, equalizes grayscale contrast, and flattens it
to 400 floating-point features. Training and runtime use the same feature code.

The winning class is the forest class with the most tree votes. Confidence is
that vote count divided by all tree votes, stored as per mille at the C boundary.
The default threshold is 600. Results below it are `known=false` and speed 0;
this threshold favors avoiding confident false readings over maximum coverage.

## Deliberate limitations

The automatic path currently uses an axis-aligned candidate interior. The
four-point normalizer is tested and available, but the red-region detector does
not yet return reliable corners to drive it. Automatic corner estimation is a
future accuracy refinement rather than a blocker for the Stage 4 baseline.
Tree-vote share is a useful rejection score but not a probabilistically
calibrated confidence. See [STAGE4_RECOGNITION.md](STAGE4_RECOGNITION.md) for
split metrics, end-to-end results, and dataset limitations.
