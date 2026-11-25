# ZEBU Slicer - builds the Windows installer from an already compiled build tree.
#
#   .\packaging\windows\build_installer.ps1                      # build dir "build", Release
#   .\packaging\windows\build_installer.ps1 -BuildDir build -Config Release
#
# Needs NSIS 3 (makensis on PATH or in its default install folder).
param(
    [string]$BuildDir = "build",
    [string]$Config   = "Release"
)
$ErrorActionPreference = "Stop"

$Root     = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$BuildDir = (Resolve-Path (Join-Path $Root $BuildDir)).Path
$Stage    = Join-Path $BuildDir "zebu_stage"

# Version from version.inc (ZEBU_VERSION "0.2.0-beta")
$inc = Get-Content (Join-Path $Root "version.inc") -Raw
if ($inc -notmatch 'set\(ZEBU_VERSION "([^"]+)"\)') { throw "ZEBU_VERSION not found in version.inc" }
$Version = $Matches[1]
if ($Version -notmatch '^(\d+)\.(\d+)\.(\d+)') { throw "Bad ZEBU_VERSION '$Version'" }
$VersionNum = "$($Matches[1]).$($Matches[2]).$($Matches[3]).0"

Write-Host "== Staging ZEBU Slicer $Version ($Config) into $Stage"
if (Test-Path $Stage) { Remove-Item -Recurse -Force $Stage }
cmake --install $BuildDir --config $Config --prefix $Stage
if ($LASTEXITCODE -ne 0) { throw "cmake --install failed" }
if (-not (Test-Path (Join-Path $Stage "zebu-slicer.exe"))) { throw "zebu-slicer.exe missing from the staged tree" }

$makensis = (Get-Command makensis.exe -ErrorAction SilentlyContinue).Source
if (-not $makensis) {
    foreach ($p in @("${env:ProgramFiles(x86)}\NSIS\makensis.exe", "$env:ProgramFiles\NSIS\makensis.exe")) {
        if (Test-Path $p) { $makensis = $p; break }
    }
}
if (-not $makensis) { throw "makensis not found: install NSIS 3 (https://nsis.sourceforge.io)" }

$Out = Join-Path $BuildDir "ZEBU_Slicer_Windows_Installer_V${Version}_x64.exe"
& $makensis /V3 "/DSTAGE_DIR=$Stage" "/DVERSION=$Version" "/DVERSION_NUM=$VersionNum" "/DOUT_FILE=$Out" (Join-Path $PSScriptRoot "zebu_installer.nsi")
if ($LASTEXITCODE -ne 0) { throw "makensis failed" }

$hash = (Get-FileHash $Out -Algorithm SHA256).Hash.ToLower()
Set-Content -Path "$Out.sha256" -Value "$hash  $(Split-Path $Out -Leaf)" -Encoding ascii
Write-Host "== Installer: $Out"
Write-Host "== SHA256:    $hash"
