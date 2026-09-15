param(
    [ValidateSet('train','valid')][string]$Split = 'train',
    [int]$MinArea = 120,
    [int]$MinSide = 12,
    [int]$MinFill = 150
)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
$projectRoot = Split-Path $PSScriptRoot -Parent
$dataset = Join-Path $projectRoot "archive/car/$Split"
$destination = Join-Path $projectRoot "output/detection-$Split-a$MinArea-s$MinSide-f$MinFill"
[void](New-Item -ItemType Directory -Force -Path $destination)
$inputs = Join-Path $projectRoot "examples/local/detection-$Split"
[void](New-Item -ItemType Directory -Force -Path $inputs)

# Stable selection: three examples each of small, medium, large speed signs,
# and three negatives (no labeled speed sign). Labels select strata, not thresholds.
$groups = @{small=@(); medium=@(); large=@(); negative=@()}
foreach ($path in [IO.Directory]::GetFiles((Join-Path $dataset 'labels'), '*.txt') | Sort-Object) {
    $boxes = @()
    foreach ($line in [IO.File]::ReadAllLines($path)) {
        if ([string]::IsNullOrWhiteSpace($line)) { continue }
        $v = $line.Trim() -split '\s+'
        $class = [int]$v[0]
        if ($class -ge 2 -and $class -le 13) {
            $culture = [Globalization.CultureInfo]::InvariantCulture
            $cx = [double]::Parse($v[1],$culture); $cy = [double]::Parse($v[2],$culture)
            $w = [double]::Parse($v[3],$culture); $h = [double]::Parse($v[4],$culture)
            $boxes += [pscustomobject]@{x=($cx-$w/2)*416; y=($cy-$h/2)*416; width=$w*416; height=$h*416; area=$w*$h}
        }
    }
    $largest = 0.0
    $rawCount = $boxes.Count
    # Localization ignores speed class: collapse near-identical region labels
    # (including conflicting class labels) at IoU >= 0.9.
    $uniqueBoxes = @()
    foreach ($box in $boxes) {
        $duplicate = $false
        foreach ($prior in $uniqueBoxes) {
            $iw = [Math]::Max(0,[Math]::Min($box.x+$box.width,$prior.x+$prior.width)-[Math]::Max($box.x,$prior.x))
            $ih = [Math]::Max(0,[Math]::Min($box.y+$box.height,$prior.y+$prior.height)-[Math]::Max($box.y,$prior.y))
            $intersection = $iw*$ih
            if ($intersection/($box.width*$box.height+$prior.width*$prior.height-$intersection) -ge 0.9) { $duplicate=$true; break }
        }
        if (-not $duplicate) { $uniqueBoxes += $box }
    }
    $boxes = $uniqueBoxes
    foreach ($b in $boxes) { $largest = [Math]::Max($largest,$b.area) }
    $group = if ($boxes.Count -eq 0) {'negative'} elseif ($largest -lt 0.02) {'small'} elseif ($largest -lt 0.2) {'medium'} else {'large'}
    if ($groups[$group].Count -lt 3) {
        $groups[$group] += [pscustomobject]@{name=[IO.Path]::GetFileNameWithoutExtension($path); group=$group; boxes=$boxes; duplicate_labels=$rawCount-$boxes.Count}
    }
}
$samples = @($groups.small) + @($groups.medium) + @($groups.large) + @($groups.negative)
if ($samples.Count -ne 12) { throw "Need all four strata; found $($samples.Count) samples" }
function IoU($a,$b) {
    $iw = [Math]::Max(0, [Math]::Min($a.x+$a.width,$b.x+$b.width)-[Math]::Max($a.x,$b.x))
    $ih = [Math]::Max(0, [Math]::Min($a.y+$a.height,$b.y+$b.height)-[Math]::Max($a.y,$b.y))
    $intersection = $iw*$ih
    return $intersection/($a.width*$a.height+$b.width*$b.height-$intersection)
}
$rows = @()
foreach ($sample in $samples) {
    $ppm = Join-Path $inputs ($sample.name + '.ppm')
    if (-not (Test-Path -LiteralPath $ppm)) {
        $bitmap = [Drawing.Bitmap]::new((Join-Path $dataset "images/$($sample.name).jpg"))
        try {
            if ($bitmap.Width -ne 416 -or $bitmap.Height -ne 416) { throw 'Expected 416x416 dataset' }
            $stream = [IO.File]::Create($ppm)
            try {
                $header = [Text.Encoding]::ASCII.GetBytes("P6`n416 416`n255`n")
                $stream.Write($header,0,$header.Length)
                $pixels = [byte[]]::new(416*416*3)
                for ($y=0; $y -lt 416; $y++) {
                    for ($x=0; $x -lt 416; $x++) {
                        $p=$bitmap.GetPixel($x,$y); $i=($y*416+$x)*3
                        $pixels[$i]=$p.R; $pixels[$i+1]=$p.G; $pixels[$i+2]=$p.B
                    }
                }
                $stream.Write($pixels,0,$pixels.Length)
            } finally { $stream.Dispose() }
        } finally { $bitmap.Dispose() }
    }
    $prefix = Join-Path $destination $sample.name
    $timer = [Diagnostics.Stopwatch]::StartNew()
    & (Join-Path $projectRoot 'traffic_sign_assistant.exe') $ppm --detect $prefix --min-area $MinArea --min-side $MinSide --min-fill $MinFill | Out-Null
    if ($LASTEXITCODE -ne 0) { throw "Detector failed: $($sample.name)" }
    $timer.Stop()
    $predictions = @(Import-Csv "$prefix-boxes.csv" | ForEach-Object {
        [pscustomobject]@{x=[double]$_.x; y=[double]$_.y; width=[double]$_.width; height=[double]$_.height}
    })
    # Descending-IoU greedy one-to-one matching at IoU >= 0.5.
    $pairs = @()
    for ($p=0; $p -lt $predictions.Count; $p++) {
        for ($g=0; $g -lt $sample.boxes.Count; $g++) {
            $score = IoU $predictions[$p] $sample.boxes[$g]
            if ($score -ge 0.5) { $pairs += [pscustomobject]@{p=$p; g=$g; score=$score} }
        }
    }
    $usedP=@{}; $usedG=@{}
    foreach ($pair in $pairs | Sort-Object score -Descending) {
        if (-not $usedP.ContainsKey($pair.p) -and -not $usedG.ContainsKey($pair.g)) {
            $usedP[$pair.p]=$true; $usedG[$pair.g]=$true
        }
    }
    $rows += [pscustomobject]@{image=$sample.name; stratum=$sample.group; signs=$sample.boxes.Count;
        matched=$usedG.Count; missed=$sample.boxes.Count-$usedG.Count;
        candidates=$predictions.Count; false_candidates=$predictions.Count-$usedP.Count;
        duplicate_labels=$sample.duplicate_labels; elapsed_ms=$timer.ElapsedMilliseconds}
}
$rows | Export-Csv (Join-Path $destination 'metrics.csv') -NoTypeInformation
$samples | ConvertTo-Json -Depth 5 | Set-Content (Join-Path $destination 'manifest.json')
$totalSigns = ($rows | Measure-Object signs -Sum).Sum
$matched = ($rows | Measure-Object matched -Sum).Sum
$falseCount = ($rows | Measure-Object false_candidates -Sum).Sum
$summary = [pscustomobject]@{split=$Split; images=$rows.Count; signs=$totalSigns; matched=$matched;
    recall=$matched/$totalSigns; false_candidates=$falseCount; false_per_image=$falseCount/$rows.Count;
    min_area=$MinArea; min_side=$MinSide; min_fill=$MinFill; iou_threshold=0.5}
$summary | ConvertTo-Json | Set-Content (Join-Path $destination 'summary.json')
$summary | Format-List
Write-Output "Reports: $destination"
