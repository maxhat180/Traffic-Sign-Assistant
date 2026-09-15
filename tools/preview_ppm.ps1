param([Parameter(Mandatory)][string]$InputPath, [Parameter(Mandatory)][string]$OutputPath)
# Preview canonical P6/255 output from this project's writer, not arbitrary PPM.
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
$bytes = [IO.File]::ReadAllBytes((Resolve-Path -LiteralPath $InputPath))
$position = 0
$lines = @()
for ($line = 0; $line -lt 3; $line++) {
    $start = $position
    while ($position -lt $bytes.Length -and $bytes[$position] -ne 10) { $position++ }
    if ($position -eq $bytes.Length) { throw 'Missing canonical PPM header' }
    $lines += [Text.Encoding]::ASCII.GetString($bytes,$start,$position-$start)
    $position++
}
if ($lines[0] -ne 'P6' -or $lines[2] -ne '255') { throw 'Expected canonical P6 with Maxval 255' }
$dimensions = $lines[1] -split ' '
$w = [int]$dimensions[0]; $h = [int]$dimensions[1]
if ($w -le 0 -or $h -le 0 -or $bytes.Length - $position -ne [long]$w*$h*3) { throw 'Invalid raster size' }
$bitmap = [Drawing.Bitmap]::new($w,$h)
try {
    for ($y=0; $y -lt $h; $y++) {
        for ($x=0; $x -lt $w; $x++) {
            $i=$position+($y*$w+$x)*3
            $bitmap.SetPixel($x,$y,[Drawing.Color]::FromArgb($bytes[$i],$bytes[$i+1],$bytes[$i+2]))
        }
    }
    $bitmap.Save([IO.Path]::GetFullPath($OutputPath),[Drawing.Imaging.ImageFormat]::Png)
} finally { $bitmap.Dispose() }
