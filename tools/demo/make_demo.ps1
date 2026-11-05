<#
.SYNOPSIS
  Turns velora-demo-raw.mp4 into the published files: docs/media/velora-demo.mp4 (1280x720, no audio, the Wi-Fi name blurred out
  of the Control Center scene) and docs/media/velora-demo.gif (a short looping preview for the README, under 10 MB).
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
New-Item -ItemType Directory -Force $OutDir | Out-Null
$OutDir = (Resolve-Path $OutDir).Path

# The Control Center scene runs from the 'overview' mark to the 'control' mark: hide the Wi-Fi name (screen pixels at 1920x1080).
$a = ($segs | Where-Object name -eq 'overview').t
$b = ($segs | Where-Object name -eq 'control').t
$box = "drawbox=x=1425:y=118:w=330:h=28:color=0x3c3c42:t=fill:enable='between(t,$($a - 1.2),$($b + 0.5))'"
$mp4 = Join-Path $OutDir 'velora-demo.mp4'
& ffmpeg -y -i $raw -vf "$box,scale=1280:720:flags=lanczos" -c:v libx264 -preset slow -crf 27 -pix_fmt yuv420p -movflags +faststart -an $mp4
$gif = Join-Path $OutDir 'velora-demo.gif'
& ffmpeg -y -i $mp4 -t $GifSeconds -vf "fps=$GifFps,scale=${GifWidth}:-1:flags=lanczos,split[a][b];[a]palettegen=max_colors=96[p];[b][p]paletteuse=dither=bayer:bayer_scale=4" $gif
Get-Item $mp4, $gif | Select-Object Name, @{ n = 'MB'; e = { [math]::Round($_.Length / 1MB, 2) } }
