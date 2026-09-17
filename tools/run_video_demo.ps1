param(
    [Parameter(Mandatory=$true)][string]$Video,
    [string]$Executable,
    [string]$Model,
    [string]$OutputDirectory,
    [ValidateRange(1,2147483647)][int]$SampleMs = 250,
    [ValidateRange(1,1000)][int]$TrackIoU = 100,
    [ValidateRange(1,2147483647)][int]$ConfirmHits = 2,
    [ValidateRange(0,1000)][int]$TrackConfidence = 500
)

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
if (-not $Executable) {
    $Executable = Join-Path $projectRoot 'output/cmake-opencv/traffic_sign_video.exe'
}
if (-not $Model) { $Model = Join-Path $projectRoot 'output/speed-recognizer.yml' }
if (-not $OutputDirectory) {
    $stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
    $OutputDirectory = Join-Path $projectRoot "output/video-demo-$stamp"
}
$Video = [IO.Path]::GetFullPath($Video)
$OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)
if (-not (Test-Path -LiteralPath $Video -PathType Leaf)) { throw "Video not found: $Video" }
if (-not (Test-Path -LiteralPath $Executable -PathType Leaf)) { throw "Video CLI not found: $Executable" }
if (-not (Test-Path -LiteralPath $Model -PathType Leaf)) { throw "Recognition model not found: $Model" }
if (Test-Path -LiteralPath $OutputDirectory) {
    throw "Choose a fresh demo output directory: $OutputDirectory"
}

& $Executable $Video $OutputDirectory --recognize $Model --sample-ms $SampleMs `
    --track-iou $TrackIoU --confirm-hits $ConfirmHits `
    --track-confidence $TrackConfidence
if ($LASTEXITCODE -ne 0) { throw "Video demo failed with exit code $LASTEXITCODE" }

$events = @(Import-Csv -LiteralPath (Join-Path $OutputDirectory 'events.csv'))
Write-Output ''
if ($events.Count -eq 0) {
    Write-Output 'Confirmed events: none'
} else {
    Write-Output 'Confirmed events:'
    $events | Select-Object speed,
        @{Name='first_seen_s';Expression={[Math]::Round([double]$_.start_ms / 1000.0, 3)}},
        @{Name='confirmed_s';Expression={[Math]::Round([double]$_.confirmation_ms / 1000.0, 3)}},
        @{Name='last_seen_s';Expression={[Math]::Round([double]$_.end_ms / 1000.0, 3)}},
        observations,mean_confidence | Format-Table -AutoSize
}
Write-Output "`nArtifacts:"
foreach ($name in @('confirmed-events.txt','events.csv','tracks.csv','recognition.csv',
        'frames.csv','run-summary.csv')) {
    $path = Join-Path $OutputDirectory $name
    if (Test-Path -LiteralPath $path -PathType Leaf) { Write-Output "  $name : $path" }
}
Write-Output "  frame artifacts : $OutputDirectory\frame-*-{boxes.csv,boxes.ppm,crop-*.ppm}"
