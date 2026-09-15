# Stage 4: mixed C/C++ architecture

## Status

Stage 4 is in progress. The first slice establishes and tests the language
boundary, adds optional OpenCV image decoding and quadrilateral rectification,
and introduces CMake without removing the original C-only build. OpenCV 4.13.0
was built locally with the same MinGW compiler under the ignored `output/deps`
tree. Both dependency-free and mandatory-OpenCV configurations are verified.

Digit recognition, confidence calibration, and an `unknown` decision remain to
be implemented and evaluated before the milestone is complete.

## Why keep both languages

The image representation, basic transforms, red-region detector, and their
tests remain C17. They form a small, dependency-free baseline with explicit
ownership. C++17 is used only where it provides access to OpenCV's C++ API and
where future recognition code is likely to need C++ libraries.

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
```

Each `.c` file is compiled as C17 and each `.cpp` file as C++17. CMake chooses
the C++ linker whenever the final dependency graph includes C++ so that the C++
runtime and OpenCV dependencies are resolved. This is different from compiling
the C files as C++, which the project deliberately does not do.

## Build modes

The original `build.ps1 -Test` remains the quickest dependency-free regression
build. It invokes GCC directly and produces the PPM-only CLI in the repository
root.

The CMake path configures both languages and runs four tests:

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
`imgcodecs` plus their packaging dependencies.

The OpenCV installation must have been built for the same compiler ABI,
architecture, and runtime as the application. The current verified configuration
uses MinGW GCC 15.2.0 and a static OpenCV 4.13.0 install whose package directory
is `output/deps/opencv-install/x64/mingw/staticlib`.

The mandatory-OpenCV build passes five CTest targets. The OpenCV-specific suite
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
axis-aligned bounding box, not four reliable sign corners, so a later Stage 4
slice must estimate the ring contour/corners before invoking this function in
the automatic pipeline.

## Remaining milestone work

1. Estimate sign geometry from each candidate and normalize its inner digit
   region.
2. Select and integrate a digit-recognition method.
3. Calibrate confidence and return `unknown` below a measured threshold.
4. Evaluate recognition on a frozen train/validation split and document errors.
