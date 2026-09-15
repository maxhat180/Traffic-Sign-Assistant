param([switch]$Test)

$ErrorActionPreference = 'Stop'
Push-Location $PSScriptRoot
try {
    $compilerFlags = @('-std=c17', '-Wall', '-Wextra', '-Wpedantic', '-Wconversion', '-Wshadow', '-Werror')
    & gcc @compilerFlags main.c frame.c image.c detect.c -o traffic_sign_assistant.exe
    if ($LASTEXITCODE -ne 0) { throw 'Application build failed' }
    if ($Test) {
        & gcc @compilerFlags tests/test_frame.c frame.c image.c -o test_frame.exe
        if ($LASTEXITCODE -ne 0) { throw 'Test build failed' }
        & .\test_frame.exe
        if ($LASTEXITCODE -ne 0) { throw 'Loader tests failed' }
        $output = & .\traffic_sign_assistant.exe examples/tiny.ppm
        if ($LASTEXITCODE -ne 0) { throw 'CLI sample failed' }
        if ($output -notcontains 'Pixel (0, 1): R=97 G=98 B=99') {
            throw 'CLI pixel output is incorrect'
        }
        Write-Output 'PASS: CLI sample'
        & .\tests\test_cli.ps1
        & gcc @compilerFlags tests/test_detect.c detect.c frame.c image.c -o test_detect.exe
        if ($LASTEXITCODE -ne 0) { throw 'Detector test build failed' }
        & .\test_detect.exe
        if ($LASTEXITCODE -ne 0) { throw 'Detector tests failed' }
    }
} finally {
    Pop-Location
}
