param(
    [string]$Manifest,
    [ValidateSet('train','valid','test','all')][string]$Split = 'test',
    [string]$Executable,
    [string]$Model,
    [string]$OutputDirectory,
    [ValidateRange(1,2147483647)][int]$SampleMs = 250,
    [ValidateRange(1,1000)][int]$MatchIoU = 500,
    [ValidateRange(0,2147483647)][int]$TimestampToleranceMs = 0,
    [switch]$SkipRun
)

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
if (-not $Manifest) { $Manifest = Join-Path $projectRoot 'evaluation/video-manifest.csv' }
if (-not $Executable) {
    $Executable = Join-Path $projectRoot 'output/cmake-opencv/traffic_sign_video.exe'
}
if (-not $Model) { $Model = Join-Path $projectRoot 'output/speed-recognizer.yml' }
if (-not $OutputDirectory) {
    $OutputDirectory = Join-Path $projectRoot "output/video-evaluation-$Split"
}
$Manifest = [IO.Path]::GetFullPath($Manifest)
$OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)
$manifestDirectory = Split-Path $Manifest -Parent
$culture = [Globalization.CultureInfo]::InvariantCulture
$allowedSpeeds = @(10,20,30,40,50,60,70,80,90,100,110,120)
if ($TimestampToleranceMs -eq 0) { $TimestampToleranceMs = [int][Math]::Ceiling($SampleMs / 2.0) }

function Resolve-ManifestPath([string]$Path) {
    if ([IO.Path]::IsPathRooted($Path)) { return [IO.Path]::GetFullPath($Path) }
    return [IO.Path]::GetFullPath((Join-Path $manifestDirectory $Path))
}

function Parse-Number($Value, [string]$Name) {
    $parsed = 0.0
    if (-not [double]::TryParse([string]$Value,
            [Globalization.NumberStyles]::Float, $culture, [ref]$parsed) -or
        [double]::IsNaN($parsed) -or [double]::IsInfinity($parsed)) {
        throw "Invalid number for ${Name}: $Value"
    }
    return $parsed
}

function Get-IoU($A, $B) {
    $intersectionWidth = [Math]::Max(0.0,
        [Math]::Min($A.x + $A.width, $B.x + $B.width) - [Math]::Max($A.x, $B.x))
    $intersectionHeight = [Math]::Max(0.0,
        [Math]::Min($A.y + $A.height, $B.y + $B.height) - [Math]::Max($A.y, $B.y))
    $intersection = $intersectionWidth * $intersectionHeight
    $union = $A.width * $A.height + $B.width * $B.height - $intersection
    if ($union -le 0.0) { return 0.0 }
    return $intersection / $union
}

function Sum-Field($Rows, [string]$Name) {
    $value = ($Rows | Measure-Object -Property $Name -Sum).Sum
    if ($null -eq $value) { return 0 }
    return $value
}

if (-not (Test-Path -LiteralPath $Manifest -PathType Leaf)) {
    throw "Video manifest not found: $Manifest"
}
$entries = @(Import-Csv -LiteralPath $Manifest)
$entries = @($entries | Where-Object { $Split -eq 'all' -or $_.split -eq $Split })
if ($entries.Count -eq 0) {
    throw "The manifest contains no '$Split' labeled videos. Add annotations before quantitative evaluation."
}
if (-not $SkipRun) {
    if (-not (Test-Path -LiteralPath $Executable -PathType Leaf)) { throw "Video CLI not found: $Executable" }
    if (-not (Test-Path -LiteralPath $Model -PathType Leaf)) { throw "Recognition model not found: $Model" }
    if (Test-Path -LiteralPath $OutputDirectory) {
        throw "Output directory already exists; choose a fresh path: $OutputDirectory"
    }
    [void](New-Item -ItemType Directory -Path $OutputDirectory)
} elseif (-not (Test-Path -LiteralPath $OutputDirectory -PathType Container)) {
    throw "Existing evaluation output not found for -SkipRun: $OutputDirectory"
}

$seenIds = @{}
$videoRows = @()
foreach ($entry in $entries) {
    $id = [string]$entry.id
    if ($id -notmatch '^[A-Za-z0-9._-]+$' -or $seenIds.ContainsKey($id)) {
        throw "Manifest IDs must be unique and filesystem-safe: '$id'"
    }
    $seenIds[$id] = $true
    $video = Resolve-ManifestPath ([string]$entry.video)
    $annotationPath = Resolve-ManifestPath ([string]$entry.annotations)
    if (-not (Test-Path -LiteralPath $annotationPath -PathType Leaf)) {
        throw "Annotations not found for ${id}: $annotationPath"
    }
    if (-not $SkipRun -and -not (Test-Path -LiteralPath $video -PathType Leaf)) {
        throw "Video not found for ${id}: $video"
    }
    $annotation = Get-Content -LiteralPath $annotationPath -Raw | ConvertFrom-Json
    if ([int]$annotation.schema_version -ne 1 -or [string]$annotation.video_id -ne $id) {
        throw "Annotation schema version or video_id mismatch: $annotationPath"
    }
    $truthEvents = @($annotation.events)
    $truthIds = @{}
    foreach ($truth in $truthEvents) {
        if ([string]::IsNullOrWhiteSpace([string]$truth.id) -or
            $truthIds.ContainsKey([string]$truth.id)) { throw "Duplicate/empty truth event ID in $id" }
        $truthIds[[string]$truth.id] = $true
        $speed = [int]$truth.speed
        $first = Parse-Number $truth.first_visible_ms 'first_visible_ms'
        $last = Parse-Number $truth.last_visible_ms 'last_visible_ms'
        if ($allowedSpeeds -notcontains $speed -or $first -lt 0.0 -or $last -lt $first) {
            throw "Invalid truth event '$($truth.id)' in $id"
        }
        $observations = @($truth.observations)
        if ($observations.Count -eq 0) { throw "Truth event '$($truth.id)' has no box observations" }
        foreach ($box in $observations) {
            $time = Parse-Number $box.timestamp_ms 'timestamp_ms'
            $x = Parse-Number $box.x 'x'
            $y = Parse-Number $box.y 'y'
            $width = Parse-Number $box.width 'width'
            $height = Parse-Number $box.height 'height'
            if ($time -lt $first -or $time -gt $last -or $x -lt 0.0 -or $y -lt 0.0 -or
                $width -le 0.0 -or $height -le 0.0) {
                throw "Invalid box observation in truth event '$($truth.id)'"
            }
        }
    }

    $videoOutput = Join-Path $OutputDirectory $id
    if (-not $SkipRun) {
        & $Executable $video $videoOutput --recognize $Model --sample-ms $SampleMs
        if ($LASTEXITCODE -ne 0) { throw "Video pipeline failed for $id" }
    }
    foreach ($name in @('recognition.csv','tracks.csv','events.csv','run-summary.csv')) {
        if (-not (Test-Path -LiteralPath (Join-Path $videoOutput $name) -PathType Leaf)) {
            throw "Missing $name for $id in $videoOutput"
        }
    }
    $recognition = @(Import-Csv -LiteralPath (Join-Path $videoOutput 'recognition.csv'))
    $tracks = @(Import-Csv -LiteralPath (Join-Path $videoOutput 'tracks.csv'))
    $events = @(Import-Csv -LiteralPath (Join-Path $videoOutput 'events.csv'))
    $run = @(Import-Csv -LiteralPath (Join-Path $videoOutput 'run-summary.csv'))
    if ($run.Count -ne 1) { throw "Expected one run-summary row for $id" }

    $trackRows = @{}
    foreach ($track in $tracks) { $trackRows[[string]$track.track_id] = $track }
    $pairBest = @{}
    foreach ($prediction in $recognition) {
        $predictionBox = [pscustomobject]@{
            x=Parse-Number $prediction.x 'x'; y=Parse-Number $prediction.y 'y'
            width=Parse-Number $prediction.width 'width'; height=Parse-Number $prediction.height 'height'
        }
        $predictionTime = Parse-Number $prediction.timestamp_ms 'timestamp_ms'
        foreach ($truth in $truthEvents) {
            $best = 0.0
            foreach ($box in @($truth.observations)) {
                $truthTime = Parse-Number $box.timestamp_ms 'timestamp_ms'
                if ([Math]::Abs($predictionTime - $truthTime) -gt $TimestampToleranceMs) { continue }
                $truthBox = [pscustomobject]@{
                    x=Parse-Number $box.x 'x'; y=Parse-Number $box.y 'y'
                    width=Parse-Number $box.width 'width'; height=Parse-Number $box.height 'height'
                }
                $best = [Math]::Max($best, (Get-IoU $predictionBox $truthBox))
            }
            $key = "$($prediction.track_id)|$($truth.id)"
            if ($best -gt 0.0 -and (-not $pairBest.ContainsKey($key) -or $best -gt $pairBest[$key])) {
                $pairBest[$key] = $best
            }
        }
    }
    $pairs = @()
    foreach ($key in $pairBest.Keys) {
        if ($pairBest[$key] -lt ($MatchIoU / 1000.0)) { continue }
        $parts = $key -split '\|', 2
        $pairs += [pscustomobject]@{ track=$parts[0]; truth=$parts[1]; score=$pairBest[$key] }
    }
    $usedTracks = @{}; $usedTruth = @{}; $matches = @()
    foreach ($pair in $pairs | Sort-Object -Property @{Expression='score';Descending=$true},track,truth) {
        if ($usedTracks.ContainsKey($pair.track) -or $usedTruth.ContainsKey($pair.truth)) { continue }
        $usedTracks[$pair.track] = $true; $usedTruth[$pair.truth] = $true; $matches += $pair
    }

    $correct = 0; $wrong = 0; $unconfirmed = 0
    $unknownObservations = 0; $localizedObservations = 0
    $detectionLatencies = @(); $confirmationLatencies = @()
    foreach ($match in $matches) {
        $truth = @($truthEvents | Where-Object { [string]$_.id -eq $match.truth })[0]
        $track = $trackRows[$match.track]
        if ($null -eq $track) { throw "Recognition referenced missing track $($match.track)" }
        $firstVisible = Parse-Number $truth.first_visible_ms 'first_visible_ms'
        $detectionLatencies += (Parse-Number $track.start_ms 'start_ms') - $firstVisible
        $confirmed = [string]$track.confirmed -eq 'true'
        if (-not $confirmed) { $unconfirmed++ }
        elseif ([int]$track.speed -eq [int]$truth.speed) { $correct++ }
        else { $wrong++ }
        if ($confirmed) {
            $confirmationLatencies += (Parse-Number $track.confirmation_ms 'confirmation_ms') - $firstVisible
        }
        foreach ($prediction in @($recognition | Where-Object { [string]$_.track_id -eq $match.track })) {
            $time = Parse-Number $prediction.timestamp_ms 'timestamp_ms'
            if ($time -ge (Parse-Number $truth.first_visible_ms 'first_visible_ms') -and
                $time -le (Parse-Number $truth.last_visible_ms 'last_visible_ms')) {
                $localizedObservations++
                if ([string]$prediction.known -ne 'true') { $unknownObservations++ }
            }
        }
    }
    $confirmedTracks = @($tracks | Where-Object { [string]$_.confirmed -eq 'true' }).Count
    if ($events.Count -ne $confirmedTracks) {
        throw "Confirmed track/event count mismatch for $id"
    }
    $truthCount = $truthEvents.Count
    $tp = $correct
    $fp = $confirmedTracks - $correct
    $fn = $truthCount - $correct
    $missed = $truthCount - $matches.Count
    $elapsed = Parse-Number $run[0].elapsed_ms 'elapsed_ms'
    $videoRows += [pscustomobject][ordered]@{
        video_id=$id; split=[string]$entry.split; truth_events=$truthCount
        localized_events=$matches.Count; missed_events=$missed
        true_positives=$tp; false_positives=$fp; false_negatives=$fn
        recognized_correct=$correct; recognized_wrong=$wrong; unconfirmed_events=$unconfirmed
        localized_observations=$localizedObservations; unknown_observations=$unknownObservations
        mean_detection_latency_ms=$(if ($detectionLatencies.Count) { ($detectionLatencies | Measure-Object -Average).Average } else { $null })
        mean_confirmation_latency_ms=$(if ($confirmationLatencies.Count) { ($confirmationLatencies | Measure-Object -Average).Average } else { $null })
        decoded_frames=[int]$run[0].decoded_frames; elapsed_ms=$elapsed
        decoded_fps=Parse-Number $run[0].decoded_fps 'decoded_fps'
        realtime_factor=Parse-Number $run[0].realtime_factor 'realtime_factor'
    }
}

$videoRows | Export-Csv -LiteralPath (Join-Path $OutputDirectory 'metrics.csv') -NoTypeInformation
$tp = Sum-Field $videoRows 'true_positives'; $fp = Sum-Field $videoRows 'false_positives'
$fn = Sum-Field $videoRows 'false_negatives'; $correct = Sum-Field $videoRows 'recognized_correct'
$wrong = Sum-Field $videoRows 'recognized_wrong'; $localizedObs = Sum-Field $videoRows 'localized_observations'
$unknownObs = Sum-Field $videoRows 'unknown_observations'; $totalElapsed = Sum-Field $videoRows 'elapsed_ms'
$totalDecoded = Sum-Field $videoRows 'decoded_frames'
$allDetectionLatency = @($videoRows | Where-Object { $null -ne $_.mean_detection_latency_ms } | ForEach-Object { $_.mean_detection_latency_ms })
$allConfirmationLatency = @($videoRows | Where-Object { $null -ne $_.mean_confirmation_latency_ms } | ForEach-Object { $_.mean_confirmation_latency_ms })
$summary = [ordered]@{
    split=$Split; videos=$videoRows.Count; truth_events=(Sum-Field $videoRows 'truth_events')
    true_positives=$tp; false_positives=$fp; false_negatives=$fn
    event_precision=$(if ($tp+$fp) {$tp/($tp+$fp)} else {0})
    event_recall=$(if ($tp+$fn) {$tp/($tp+$fn)} else {0})
    event_f1=$(if (2*$tp+$fp+$fn) {2*$tp/(2*$tp+$fp+$fn)} else {0})
    recognized_correct=$correct; recognized_wrong=$wrong
    recognition_accuracy=$(if ($correct+$wrong) {$correct/($correct+$wrong)} else {0})
    unknown_observations=$unknownObs
    unknown_observation_rate=$(if ($localizedObs) {$unknownObs/$localizedObs} else {0})
    mean_video_detection_latency_ms=$(if ($allDetectionLatency.Count) {($allDetectionLatency | Measure-Object -Average).Average} else {$null})
    mean_video_confirmation_latency_ms=$(if ($allConfirmationLatency.Count) {($allConfirmationLatency | Measure-Object -Average).Average} else {$null})
    decoded_fps=$(if ($totalElapsed) {$totalDecoded/($totalElapsed/1000.0)} else {0})
    sample_ms=$SampleMs; match_iou=$MatchIoU/1000.0; timestamp_tolerance_ms=$TimestampToleranceMs
}
$summary | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $OutputDirectory 'summary.json')
[pscustomobject]$summary | Format-List
Write-Output "Reports: $OutputDirectory"
