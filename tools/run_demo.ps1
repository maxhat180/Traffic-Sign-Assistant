$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
& (Join-Path $PSScriptRoot 'prepare_examples.ps1')
$outputDirectory = Join-Path $projectRoot 'output'
[void](New-Item -ItemType Directory -Force -Path $outputDirectory)
foreach ($name in @('speed30', 'speed50')) {
    $inputImage = Join-Path $projectRoot "examples/local/$name.ppm"
    $resultImage = Join-Path $outputDirectory "$name-gray.ppm"
    & (Join-Path $projectRoot 'traffic_sign_assistant.exe') $inputImage --resize 208 208 --grayscale --output $resultImage
    if ($LASTEXITCODE -ne 0) { throw "Demo failed for $name" }

    # Independently verify every output sample against the input's 2:1 mapping.
    $original = [IO.File]::ReadAllBytes($inputImage)
    $processed = [IO.File]::ReadAllBytes($resultImage)
    $sourceHeader = [Text.Encoding]::ASCII.GetBytes("P6`n416 416`n255`n")
    $targetHeader = [Text.Encoding]::ASCII.GetBytes("P6`n208 208`n255`n")
    if ($original.Length -ne $sourceHeader.Length + 416 * 416 * 3 -or
        $processed.Length -ne $targetHeader.Length + 208 * 208 * 3) {
        throw 'Unexpected demo image size'
    }
    $preview = [Drawing.Bitmap]::new(208,208)
    try {
        for ($y = 0; $y -lt 208; $y++) {
            for ($x = 0; $x -lt 208; $x++) {
                $sourceOffset = $sourceHeader.Length + (($y * 2) * 416 + $x * 2) * 3
                $expected = [int][Math]::Floor((299 * $original[$sourceOffset] +
                    587 * $original[$sourceOffset + 1] + 114 * $original[$sourceOffset + 2] + 500) / 1000)
                $targetOffset = $targetHeader.Length + ($y * 208 + $x) * 3
                if ($processed[$targetOffset] -ne $expected -or
                    $processed[$targetOffset + 1] -ne $expected -or
                    $processed[$targetOffset + 2] -ne $expected) { throw "Pixel mismatch at $x,$y" }
                $preview.SetPixel($x, $y, [Drawing.Color]::FromArgb($expected,$expected,$expected))
            }
        }
        $preview.Save((Join-Path $outputDirectory "$name-gray.png"), [Drawing.Imaging.ImageFormat]::Png)
    } finally { $preview.Dispose() }
    Write-Output "PASS: $name demo pixel verification"
}
