$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
$testRoot = Join-Path $projectRoot ('output/video-eval-test-' + [guid]::NewGuid().ToString('N'))
$runRoot = Join-Path $testRoot 'run'
$videoRoot = Join-Path $runRoot 'fixture'
[void](New-Item -ItemType Directory -Path $videoRoot -Force)
try {
    @'
{
  "schema_version": 1,
  "video_id": "fixture",
  "events": [
    {"id":"sign-a","speed":30,"first_visible_ms":0,"last_visible_ms":600,
     "observations":[
       {"timestamp_ms":0,"x":10,"y":10,"width":20,"height":20},
       {"timestamp_ms":250,"x":12,"y":10,"width":20,"height":20}]},
    {"id":"sign-b","speed":70,"first_visible_ms":1000,"last_visible_ms":1500,
     "observations":[{"timestamp_ms":1250,"x":100,"y":100,"width":20,"height":20}]}
  ]
}
'@ | Set-Content -LiteralPath (Join-Path $testRoot 'annotations.json')
    @'
id,split,video,annotations
fixture,test,unused.mp4,annotations.json
'@ | Set-Content -LiteralPath (Join-Path $testRoot 'manifest.csv')
    @'
sample,source_frame,timestamp_ms,candidate,track_id,x,y,width,height,speed,predicted_speed,confidence,known
0,0,0,0,0,10,10,20,20,0,30,0.55,false
1,1,250,0,0,12,10,20,20,30,30,0.75,true
2,2,500,0,1,200,200,20,20,50,50,0.80,true
3,3,750,0,1,201,200,20,20,50,50,0.80,true
'@ | Set-Content -LiteralPath (Join-Path $videoRoot 'recognition.csv')
    @'
track_id,start_frame,end_frame,start_ms,end_ms,observations,speed,speed_observations,mean_confidence,confirmation_ms,confirmed
0,0,1,0,250,2,30,2,0.650,250,true
1,2,3,500,750,2,50,2,0.800,750,true
'@ | Set-Content -LiteralPath (Join-Path $videoRoot 'tracks.csv')
    @'
event_id,track_id,start_frame,end_frame,start_ms,end_ms,observations,speed,speed_observations,mean_confidence,confirmation_ms,confirmed
0,0,0,1,0,250,2,30,2,0.650,250,true
1,1,2,3,500,750,2,50,2,0.800,750,true
'@ | Set-Content -LiteralPath (Join-Path $videoRoot 'events.csv')
    @'
decoded_frames,samples,candidates,accepted_predictions,confirmed_events,video_duration_ms,elapsed_ms,decoded_fps,samples_per_second,realtime_factor
60,4,4,3,2,2000,1000,60,4,2
'@ | Set-Content -LiteralPath (Join-Path $videoRoot 'run-summary.csv')

    & (Join-Path $projectRoot 'tools/evaluate_video.ps1') `
        -Manifest (Join-Path $testRoot 'manifest.csv') -Split test `
        -OutputDirectory $runRoot -SampleMs 250 -SkipRun | Out-Null
    $summary = Get-Content -LiteralPath (Join-Path $runRoot 'summary.json') -Raw | ConvertFrom-Json
    if ($summary.true_positives -ne 1 -or $summary.false_positives -ne 1 -or
        $summary.false_negatives -ne 1 -or $summary.recognized_correct -ne 1 -or
        $summary.recognized_wrong -ne 0 -or $summary.unknown_observations -ne 1 -or
        [Math]::Abs([double]$summary.mean_video_confirmation_latency_ms - 250.0) -gt 0.001 -or
        [Math]::Abs([double]$summary.decoded_fps - 60.0) -gt 0.001) {
        throw "Unexpected evaluation summary: $($summary | ConvertTo-Json -Compress)"
    }
    Write-Output 'PASS: video evaluation matching, metrics, unknowns, latency, and throughput'
} finally {
    if (Test-Path -LiteralPath $testRoot) {
        Remove-Item -LiteralPath $testRoot -Recurse -Force
    }
}
