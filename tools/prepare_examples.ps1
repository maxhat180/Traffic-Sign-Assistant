# Optional Windows-only JPEG conversion; the C application needs no image library.
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
$projectRoot = Split-Path $PSScriptRoot -Parent
$exampleDirectory = Join-Path $projectRoot 'examples/local'
[void](New-Item -ItemType Directory -Force -Path $exampleDirectory)
$samples = @(
    @{Name='speed30'; Source='00001_00006_00026_png.rf.49d271a1ad59fd8fc3022b9d27fd55e2.jpg'},
    @{Name='speed50'; Source='00002_00017_00026_png.rf.0e0c876d62e868e7b8bbea45c2854b90.jpg'}
)
foreach ($sample in $samples) {
    $sourcePath = Join-Path $projectRoot ('archive/car/test/images/' + $sample.Source)
    if (-not (Test-Path -LiteralPath $sourcePath)) { throw "Missing local dataset image: $sourcePath" }
    $bitmap = [System.Drawing.Bitmap]::new($sourcePath)
    try {
        $destinationPath = Join-Path $exampleDirectory ($sample.Name + '.ppm')
        $stream = [System.IO.File]::Create($destinationPath)
        try {
            $header = [System.Text.Encoding]::ASCII.GetBytes("P6`n$($bitmap.Width) $($bitmap.Height)`n255`n")
            $stream.Write($header, 0, $header.Length)
            $row = [byte[]]::new($bitmap.Width * 3)
            for ($y = 0; $y -lt $bitmap.Height; $y++) {
                for ($x = 0; $x -lt $bitmap.Width; $x++) {
                    $pixel = $bitmap.GetPixel($x, $y)
                    $row[$x * 3] = $pixel.R
                    $row[$x * 3 + 1] = $pixel.G
                    $row[$x * 3 + 2] = $pixel.B
                }
                $stream.Write($row, 0, $row.Length)
            }
        } finally { $stream.Dispose() }
        Write-Output "Prepared $destinationPath"
    } finally { $bitmap.Dispose() }
}
