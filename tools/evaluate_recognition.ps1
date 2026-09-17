param(
    [ValidateSet('train','valid','test')][string]$Split = 'valid',
    [string]$Executable,
    [string]$Model,
    [string]$OutputDirectory,
    [ValidateRange(0,1000)][int]$Confidence = 600,
    [int]$MinArea = 120,
    [int]$MinSide = 12,
    [int]$MinFill = 150
)

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
if (-not $Executable) {
    $Executable = Join-Path $projectRoot 'output/cmake-opencv/traffic_sign_assistant.exe'
}
if (-not $Model) {
    $Model = Join-Path $projectRoot 'output/speed-recognizer.yml'
}
if (-not (Test-Path -LiteralPath $Executable -PathType Leaf)) {
    throw "OpenCV CLI not found: $Executable"
}
if (-not (Test-Path -LiteralPath $Model -PathType Leaf)) {
    throw "Recognition model not found: $Model"
}

$dataset = Join-Path $projectRoot "archive/car/$Split"
$destination = if ($OutputDirectory) {
    [IO.Path]::GetFullPath($OutputDirectory)
} else {
    Join-Path $projectRoot "output/recognition-$Split-c$Confidence"
}
[void](New-Item -ItemType Directory -Force -Path $destination)
$culture = [Globalization.CultureInfo]::InvariantCulture
$speeds = @(10,100,110,120,20,30,40,50,60,70,80,90)

function Get-IoU($a, $b) {
    $intersectionWidth = [Math]::Max(0, [Math]::Min($a.x + $a.width, $b.x + $b.width) - [Math]::Max($a.x, $b.x))
    $intersectionHeight = [Math]::Max(0, [Math]::Min($a.y + $a.height, $b.y + $b.height) - [Math]::Max($a.y, $b.y))
    $intersection = $intersectionWidth * $intersectionHeight
    $union = $a.width * $a.height + $b.width * $b.height - $intersection
    if ($union -le 0) { return 0.0 }
    return $intersection / $union
}

# Freeze a compact, deterministic sample: three images each with small, medium,
# and large speed signs, plus three images without a labeled speed sign.
$groups = @{small=@(); medium=@(); large=@(); negative=@()}
foreach ($path in [IO.Directory]::GetFiles((Join-Path $dataset 'labels'), '*.txt') | Sort-Object) {
    $boxes = @()
    $ambiguous = 0
    foreach ($line in [IO.File]::ReadAllLines($path)) {
        if ([string]::IsNullOrWhiteSpace($line)) { continue }
        $values = $line.Trim() -split '\s+'
        $class = [int]$values[0]
        if ($class -lt 2 -or $class -gt 13) { continue }
        $centerX = [double]::Parse($values[1], $culture)
        $centerY = [double]::Parse($values[2], $culture)
        $width = [double]::Parse($values[3], $culture)
        $height = [double]::Parse($values[4], $culture)
        $box = [pscustomobject]@{
            x=($centerX-$width/2)*416; y=($centerY-$height/2)*416
            width=$width*416; height=$height*416; area=$width*$height
            speed=$speeds[$class-2]
        }
        $duplicate = $false
        foreach ($prior in $boxes) {
            if ((Get-IoU $box $prior) -ge 0.9) {
                $duplicate = $true
                if ($box.speed -ne $prior.speed) {
                    $prior.speed = 0
                    $ambiguous++
                }
                break
            }
        }
        if (-not $duplicate) { $boxes += $box }
    }
    $boxes = @($boxes | Where-Object speed -ne 0)
    $largest = 0.0
    foreach ($box in $boxes) { $largest = [Math]::Max($largest, $box.area) }
    $group = if ($boxes.Count -eq 0) {'negative'} elseif ($largest -lt 0.02) {'small'} elseif ($largest -lt 0.2) {'medium'} else {'large'}
    if ($groups[$group].Count -lt 3) {
        $groups[$group] += [pscustomobject]@{
            name=[IO.Path]::GetFileNameWithoutExtension($path)
            group=$group; boxes=$boxes; ambiguous_labels=$ambiguous
        }
    }
}
$samples = @($groups.small) + @($groups.medium) + @($groups.large) + @($groups.negative)
if ($samples.Count -ne 12) { throw "Need all four strata; found $($samples.Count) samples" }

$rows = @()
foreach ($sample in $samples) {
    $image = Join-Path $dataset "images/$($sample.name).jpg"
    if (-not (Test-Path -LiteralPath $image -PathType Leaf)) { throw "Image not found: $image" }
    $prefix = Join-Path $destination $sample.name
    $timer = [Diagnostics.Stopwatch]::StartNew()
    & $Executable $image --detect $prefix --min-area $MinArea --min-side $MinSide `
        --min-fill $MinFill --recognize $Model --confidence $Confidence | Out-Null
    if ($LASTEXITCODE -ne 0) { throw "Recognition pipeline failed: $($sample.name)" }
    $timer.Stop()

    $boxRows = @(Import-Csv "$prefix-boxes.csv")
    $recognitionRows = @(Import-Csv "$prefix-recognition.csv")
    if ($boxRows.Count -ne $recognitionRows.Count) {
        throw "Candidate and recognition CSV counts differ: $($sample.name)"
    }
    $predictions = @()
    for ($index = 0; $index -lt $boxRows.Count; $index++) {
        if ([int]$recognitionRows[$index].id -ne $index) {
            throw "Recognition IDs are not contiguous: $($sample.name)"
        }
        $predictions += [pscustomobject]@{
            x=[double]$boxRows[$index].x; y=[double]$boxRows[$index].y
            width=[double]$boxRows[$index].width; height=[double]$boxRows[$index].height
            speed=[int]$recognitionRows[$index].speed
            confidence=[double]::Parse($recognitionRows[$index].confidence, $culture)
            known=$recognitionRows[$index].known -eq 'true'
        }
    }

    $pairs = @()
    for ($prediction = 0; $prediction -lt $predictions.Count; $prediction++) {
        for ($truth = 0; $truth -lt $sample.boxes.Count; $truth++) {
            $score = Get-IoU $predictions[$prediction] $sample.boxes[$truth]
            if ($score -ge 0.5) {
                $pairs += [pscustomobject]@{prediction=$prediction; truth=$truth; score=$score}
            }
        }
    }
    $usedPredictions = @{}
    $usedTruth = @{}
    $matchedPairs = @()
    foreach ($pair in $pairs | Sort-Object score -Descending) {
        if (-not $usedPredictions.ContainsKey($pair.prediction) -and -not $usedTruth.ContainsKey($pair.truth)) {
            $usedPredictions[$pair.prediction] = $true
            $usedTruth[$pair.truth] = $true
            $matchedPairs += $pair
        }
    }

    $correct = 0; $wrong = 0; $unknown = 0
    foreach ($pair in $matchedPairs) {
        $prediction = $predictions[$pair.prediction]
        $truth = $sample.boxes[$pair.truth]
        if (-not $prediction.known) { $unknown++ }
        elseif ($prediction.speed -eq $truth.speed) { $correct++ }
        else { $wrong++ }
    }
    $falseKnown = 0; $falseUnknown = 0
    for ($index = 0; $index -lt $predictions.Count; $index++) {
        if ($usedPredictions.ContainsKey($index)) { continue }
        if ($predictions[$index].known) { $falseKnown++ } else { $falseUnknown++ }
    }
    $rows += [pscustomobject]@{
        image=$sample.name; stratum=$sample.group; signs=$sample.boxes.Count
        localized=$matchedPairs.Count; missed=$sample.boxes.Count-$matchedPairs.Count
        correct=$correct; wrong=$wrong; unknown=$unknown
        candidates=$predictions.Count; false_known=$falseKnown; false_unknown=$falseUnknown
        ambiguous_labels=$sample.ambiguous_labels; elapsed_ms=$timer.ElapsedMilliseconds
    }
}

$rows | Export-Csv (Join-Path $destination 'metrics.csv') -NoTypeInformation
$samples | ConvertTo-Json -Depth 6 | Set-Content (Join-Path $destination 'manifest.json')
function Sum-Column([string]$Name) {
    $value = ($rows | Measure-Object $Name -Sum).Sum
    return $(if ($null -eq $value) { 0 } else { $value })
}
$signs = Sum-Column 'signs'
$localized = Sum-Column 'localized'
$correct = Sum-Column 'correct'
$wrong = Sum-Column 'wrong'
$unknown = Sum-Column 'unknown'
$falseKnown = Sum-Column 'false_known'
$falseUnknown = Sum-Column 'false_unknown'
$acceptedMatched = $correct + $wrong
$summary = [ordered]@{
    split=$Split; images=$rows.Count; signs=$signs; localized=$localized; missed=$signs-$localized
    localization_recall=$(if ($signs) {$localized/$signs} else {0})
    recognized_correct=$correct; recognized_wrong=$wrong; localized_unknown=$unknown
    recognition_coverage=$(if ($localized) {$acceptedMatched/$localized} else {0})
    accepted_accuracy=$(if ($acceptedMatched) {$correct/$acceptedMatched} else {0})
    end_to_end_correct_rate=$(if ($signs) {$correct/$signs} else {0})
    false_known=$falseKnown; false_unknown=$falseUnknown
    false_known_per_image=$falseKnown/$rows.Count
    confidence_per_mille=$Confidence; min_area=$MinArea; min_side=$MinSide
    min_fill=$MinFill; iou_threshold=0.5
}
$summary | ConvertTo-Json | Set-Content (Join-Path $destination 'summary.json')
[pscustomobject]$summary | Format-List
Write-Output "Reports: $destination"
