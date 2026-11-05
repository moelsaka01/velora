<#
.SYNOPSIS
  Turns velora-demo-raw.mp4 into the published files: docs/media/velora-demo.mp4 (1280x720, no audio) and docs/media/velora-demo.gif
  (a short looping preview for the README, under 10 MB). Two regions are blurred out: the Wi-Fi name in the Control Center scene and
  the file hits (with their folders) in the Search scene.
.EXAMPLE
  powershell -File tools\demo\make_demo.ps1 -InDir $env:TEMP\velora-demo
#>
param(
    [Parameter(Mandatory)][string]$InDir,
    [string]$OutDir = (Join-Path $PSScriptRoot '..\..\docs\media'),
    [int]$GifWidth = 720,
    [int]$GifFps = 10,
    [double]$GifSeconds = 40
)
$ErrorActionPreference = 'Stop'
$raw = Join-Path $InDir 'velora-demo-raw.mp4'
$segs = Get-Content (Join-Path $InDir 'segments.json') | ConvertFrom-Json
function At($n) { ($segs | Where-Object name -eq $n).t }
New-Item -ItemType Directory -Force $OutDir | Out-Null
$OutDir = (Resolve-Path $OutDir).Path

# Scene windows (seconds in the video; the marks are taken at the END of each scene, with a margin on both sides).
$searchFrom = (At 'apps') + 0.4;     $searchTo = (At 'search') + 1.4
$controlFrom = (At 'overview') + 0.4; $controlTo = (At 'control') + 1.4
# Screen pixels at 1920x1080: the Search results below the first row, and the Wi-Fi name line of Control Center.
$f = "[0:v]split=3[a][b][c];" +
     "[b]crop=780:130:570:450,gblur=sigma=16[bs];" +
     "[c]crop=330:30:1425:116,gblur=sigma=9[bw];" +
     "[a][bs]overlay=570:450:enable='between(t,$searchFrom,$searchTo)'[v1];" +
     "[v1][bw]overlay=1425:116:enable='between(t,$controlFrom,$controlTo)',scale=1280:720:flags=lanczos[out]"
$mp4 = Join-Path $OutDir 'velora-demo.mp4'
& ffmpeg -y -i $raw -filter_complex $f -map '[out]' -c:v libx264 -preset slow -crf 27 -pix_fmt yuv420p -movflags +faststart -an $mp4
$gif = Join-Path $OutDir 'velora-demo.gif'
& ffmpeg -y -i $mp4 -t $GifSeconds -vf "fps=$GifFps,scale=${GifWidth}:-1:flags=lanczos,split[a][b];[a]palettegen=max_colors=96[p];[b][p]paletteuse=dither=bayer:bayer_scale=4" $gif
Get-Item $mp4, $gif | Select-Object Name, @{ n = 'MB'; e = { [math]::Round($_.Length / 1MB, 2) } }
