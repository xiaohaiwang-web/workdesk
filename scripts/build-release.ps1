[CmdletBinding()]
param(
    [string]$Version = '0.2.0',
    [string]$QtRoot = 'C:\Qt\6.11.2\msvc2022_64',
    [string]$TesseractRoot = '',
    [switch]$SkipInstaller,
    [switch]$SkipVCRedistDownload
)

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$packageScript = Join-Path $PSScriptRoot 'package-windows.ps1'

Write-Host '[WorkDesk release] Building Release configuration...' -ForegroundColor Cyan
& cmake --build (Join-Path $projectRoot 'build') --config Release --parallel
if ($LASTEXITCODE -ne 0) {
    throw "Release build failed with exit code $LASTEXITCODE. Packaging was not started."
}

$packageArgs = @(
    '-NoProfile',
    '-ExecutionPolicy', 'Bypass',
    '-File', $packageScript,
    '-Configuration', 'Release',
    '-Version', $Version,
    '-QtRoot', $QtRoot
)
if ($TesseractRoot) { $packageArgs += @('-TesseractRoot', $TesseractRoot) }
if ($SkipInstaller) { $packageArgs += '-SkipInstaller' }
if ($SkipVCRedistDownload) { $packageArgs += '-SkipVCRedistDownload' }

Write-Host '[WorkDesk release] Build passed. Collecting runtimes and creating release artifacts...' -ForegroundColor Cyan
& powershell.exe @packageArgs
if ($LASTEXITCODE -ne 0) {
    throw "Packaging failed with exit code $LASTEXITCODE. Check the release log above."
}

Write-Host '[WorkDesk release] Release artifacts are ready. Run the clean-machine test plan before publishing.' -ForegroundColor Green
