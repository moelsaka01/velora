<#
.SYNOPSIS
  Stages the release binaries and builds the portable zip; with -Checksums writes SHA256SUMS.txt for everything in dist\.
.EXAMPLE
  pwsh tools\package.ps1 -BuildDir build\Release -Version 1.0.0
  pwsh tools\package.ps1 -Checksums
#>
param(
  [string]$BuildDir = "build\Release",
  [string]$Version = "",
  [string]$Out = "dist",
  [switch]$Checksums
)
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

if ($Checksums) {
  $lines = Get-ChildItem $Out -File | Where-Object { $_.Name -match '(setup\.exe|\.msi|portable\.zip)$' } | Sort-Object Name | ForEach-Object {
    $h = (Get-FileHash $_.FullName -Algorithm SHA256).Hash.ToLower()
    "$h  $($_.Name)"
  }
  $lines | Set-Content -Encoding ascii (Join-Path $Out "SHA256SUMS.txt")
  $lines
  return
}

if (-not $Version) { $Version = (Get-Content vcpkg.json -Raw | ConvertFrom-Json).version }
$stage = Join-Path $Out "stage"
if (Test-Path $stage) { Remove-Item $stage -Recurse -Force }
New-Item -ItemType Directory -Force $stage | Out-Null

foreach ($exe in "velora-shell.exe", "velora-watchdog.exe", "velora-files.exe", "velora-settings.exe") {
  $src = Join-Path $BuildDir $exe
  if (-not (Test-Path $src)) { throw "missing $src (build the Release configuration first)" }
  Copy-Item $src $stage
}
foreach ($doc in "LICENSE", "NOTICE", "THIRD_PARTY_NOTICES.md", "README.md") { Copy-Item $doc $stage }

$zip = Join-Path $Out "Velora-$Version-portable.zip"
if (Test-Path $zip) { Remove-Item $zip }
Compress-Archive -Path (Join-Path $stage "*") -DestinationPath $zip
Write-Host "staged $stage; wrote $zip"
Get-ChildItem $stage | Select-Object Name, Length | Format-Table -AutoSize
