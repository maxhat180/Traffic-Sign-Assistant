$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
$testDirectory = Join-Path $projectRoot ('output/test-' + [guid]::NewGuid().ToString('N'))
[void](New-Item -ItemType Directory -Path $testDirectory -Force)
$executable = Join-Path $projectRoot 'traffic_sign_assistant.exe'
$inputImage = Join-Path $projectRoot 'examples/tiny.ppm'
$resultImage = Join-Path $testDirectory 'gray resized.ppm'
$script:caseCount = 0
function Check-Run([string[]]$CommandArguments, [bool]$ShouldPass) {
    $savedPreference = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        $messages = & $executable @CommandArguments 2>&1
        $code = $LASTEXITCODE
    } finally { $ErrorActionPreference = $savedPreference }
    if (($code -eq 0) -ne $ShouldPass) { throw "Unexpected exit $code : $messages" }
    $script:caseCount++
}
Check-Run @($inputImage, '--grayscale', '--resize', '3', '1', '--output', $resultImage) $true
$expected = [Text.Encoding]::ASCII.GetBytes("P6`n3 1`n255`n") + [byte[]]@(74,74,74,74,74,74,50,50,50)
$actual = [IO.File]::ReadAllBytes($resultImage)
if ([Convert]::ToBase64String($actual) -ne [Convert]::ToBase64String($expected)) {
    throw 'Saved grayscale/resize pixels differ from the hand-calculated fixture'
}
Check-Run @($resultImage) $true
Check-Run @() $false
Check-Run @($inputImage, '--grayscale') $false
Check-Run @($inputImage, '--resize', '0', '2', '--output', $resultImage) $false
Check-Run @($inputImage, '--resize', '-1', '2', '--output', $resultImage) $false
Check-Run @($inputImage, '--resize', '99999999999999999999999999', '2', '--output', $resultImage) $false
Check-Run @($inputImage, '--resize', '2') $false
Check-Run @($inputImage, '--grayscale', '--grayscale', '--output', $resultImage) $false
Check-Run @($inputImage, '--unknown') $false
Check-Run @($inputImage, '--output', (Join-Path $testDirectory 'missing/result.ppm')) $false
Check-Run @((Join-Path $testDirectory 'missing.ppm')) $false
# Synthetic ring: check actual detector artifacts, coordinates, and crop margin.
$ringImage = Join-Path $testDirectory 'ring.ppm'
$ringBytes = [byte[]]::new(24*24*3)
for ($i=0; $i -lt $ringBytes.Length; $i++) { $ringBytes[$i]=255 }
for ($y=4; $y -lt 16; $y++) {
    for ($x=3; $x -lt 15; $x++) {
        if ($y -eq 4 -or $y -eq 15 -or $x -eq 3 -or $x -eq 14) {
            $i=($y*24+$x)*3; $ringBytes[$i+1]=0; $ringBytes[$i+2]=0
        }
    }
}
[IO.File]::WriteAllBytes($ringImage,([Text.Encoding]::ASCII.GetBytes("P6`n24 24`n255`n")+$ringBytes))
$prefix=Join-Path $testDirectory 'ring'
Check-Run @($ringImage,'--detect',$prefix) $true
if (@(Import-Csv "$prefix-boxes.csv").Count -ne 0) { throw 'Default area filter should reject the compact test ring' }
Check-Run @($ringImage,'--detect',$prefix,'--min-area','12') $true
$boxes=@(Import-Csv "$prefix-boxes.csv")
if ($boxes.Count -ne 1 -or $boxes[0].x -ne '3' -or $boxes[0].y -ne '4' -or
    $boxes[0].width -ne '12' -or $boxes[0].height -ne '12') { throw 'Incorrect detection box' }
$crop=[IO.File]::ReadAllBytes("$prefix-crop-0.ppm")
$cropHeader=[Text.Encoding]::ASCII.GetBytes("P6`n16 16`n255`n")
if ($crop.Length -ne $cropHeader.Length+16*16*3) { throw 'Wrong crop size/margin' }
for ($y=0; $y -lt 16; $y++) {
    for ($x=0; $x -lt 16; $x++) {
        for ($channel=0; $channel -lt 3; $channel++) {
            if ($crop[$cropHeader.Length+($y*16+$x)*3+$channel] -ne
                $ringBytes[(($y+2)*24+$x+1)*3+$channel]) { throw 'Incorrect crop pixel' }
        }
    }
}
Check-Run @($ringImage,'--detect',(Join-Path $testDirectory 'missing/box')) $false
Check-Run @($ringImage,'--detect',$prefix,'--red-ratio','100') $false
Check-Run @($ringImage,'--min-area','12') $false
Check-Run @($ringImage,'--detect',$prefix,'--resize','12','12','--min-area','12','--min-side','5') $true
# All children below were generated in this unique test directory; no recursion.
Get-ChildItem -LiteralPath $testDirectory -File | ForEach-Object { Remove-Item -LiteralPath $_.FullName }
Remove-Item -LiteralPath $testDirectory
Write-Output "PASS: $script:caseCount CLI cases and exact saved pixels"
