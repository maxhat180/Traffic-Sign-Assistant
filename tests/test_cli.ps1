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
Remove-Item -LiteralPath $resultImage
Remove-Item -LiteralPath $testDirectory
Write-Output "PASS: $script:caseCount CLI cases and exact saved pixels"
