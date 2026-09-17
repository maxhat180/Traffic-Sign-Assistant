param(
    [ValidatePattern('^[0-9]+\.[0-9]+\.[0-9]+$')]
    [string]$Version = '4.13.0',
    [ValidateRange(1, 64)]
    [int]$Jobs = 4
)

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
$dependencyRoot = Join-Path $projectRoot 'output/deps'
$source = Join-Path $dependencyRoot 'opencv'
$build = Join-Path $dependencyRoot 'opencv-build'
$install = Join-Path $dependencyRoot 'opencv-install'

[void](New-Item -ItemType Directory -Path $dependencyRoot -Force)
if (-not (Test-Path (Join-Path $source 'CMakeLists.txt'))) {
    if (Test-Path $source) {
        throw "OpenCV source path exists but is incomplete: $source"
    }
    & git clone --branch $Version --depth 1 https://github.com/opencv/opencv.git $source
    if ($LASTEXITCODE -ne 0) { throw 'OpenCV clone failed' }
}

$configure = @(
    '-S', $source,
    '-B', $build,
    '-G', 'Ninja',
    '-DCMAKE_BUILD_TYPE=Release',
    ('-DCMAKE_INSTALL_PREFIX=' + $install.Replace('\', '/')),
    '-DBUILD_LIST=core,imgproc,imgcodecs,ml,videoio',
    '-DBUILD_SHARED_LIBS=OFF',
    '-DBUILD_TESTS=OFF',
    '-DBUILD_PERF_TESTS=OFF',
    '-DBUILD_EXAMPLES=OFF',
    '-DBUILD_opencv_apps=OFF',
    '-DBUILD_opencv_python_bindings_generator=OFF',
    '-DBUILD_JAVA=OFF',
    '-DWITH_IPP=OFF',
    '-DWITH_ITT=OFF',
    '-DWITH_OPENCL=OFF',
    '-DWITH_FFMPEG=ON',
    '-DWITH_MSMF=OFF',
    '-DWITH_DSHOW=OFF',
    '-DWITH_ADE=OFF',
    '-DWITH_WEBP=OFF',
    '-DWITH_TIFF=OFF',
    '-DWITH_OPENJPEG=OFF',
    '-DWITH_OPENEXR=OFF',
    '-DOPENCV_FORCE_3RDPARTY_BUILD=ON'
)

$python = $null
if (Get-Command py -ErrorAction SilentlyContinue) {
    $python = (& py -3 -c 'import sys; print(sys.executable)' 2>$null | Select-Object -First 1)
}
if ($python) {
    $pythonPath = ([string]$python).Trim().Replace('\', '/')
    $configure += '-DPYTHON_EXECUTABLE=' + $pythonPath
    $configure += '-DPYTHON3_EXECUTABLE=' + $pythonPath
    $configure += '-DPYTHON_DEFAULT_EXECUTABLE=' + $pythonPath
}

& cmake @configure
if ($LASTEXITCODE -ne 0) { throw 'OpenCV configuration failed' }
$variables = Get-Content (Join-Path $build 'CMakeVars.txt') -Raw
if ($variables -notmatch '(?m)^HAVE_FFMPEG=TRUE\r?$') {
    throw 'OpenCV configuration did not enable FFmpeg; inspect CMakeDownloadLog.txt'
}

# OpenCV 4.13 exports its internal static Protobuf target even with DNN excluded.
# Build it before installation so the generated package is internally complete.
& cmake --build $build --target opencv_imgcodecs opencv_ml opencv_videoio libprotobuf --parallel $Jobs
if ($LASTEXITCODE -ne 0) { throw 'OpenCV build failed' }
& cmake --build $build --target install --parallel $Jobs
if ($LASTEXITCODE -ne 0) { throw 'OpenCV installation failed' }
$ffmpegWrapper = Get-ChildItem (Join-Path $install 'x64/mingw/bin') `
    -Filter 'opencv_videoio_ffmpeg*_64.dll' -File -ErrorAction SilentlyContinue
if (-not $ffmpegWrapper) { throw 'OpenCV FFmpeg runtime wrapper was not installed' }

$package = Join-Path $install 'x64/mingw/staticlib'
Write-Output "OpenCV $Version is ready. Configure this project with:"
Write-Output "  cmake -S . -B output/cmake-opencv -G Ninja -DOpenCV_DIR=$($package.Replace('\', '/')) -DTRAFFIC_SIGN_REQUIRE_OPENCV=ON"
