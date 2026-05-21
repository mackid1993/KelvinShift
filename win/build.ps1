# build.ps1 — Build KelvinShift Windows release and package as Inno Setup
# installer.
#
# Usage:
#   .\build.ps1                  # full build: publish + installer
#   .\build.ps1 -SkipInstaller   # just publish (skip iscc)
#   .\build.ps1 -Clean           # rm bin/obj first

param(
    [switch]$SkipInstaller,
    [switch]$Clean
)

$ErrorActionPreference = 'Stop'
Set-Location $PSScriptRoot

$proj      = 'src\KelvinShift\KelvinShift.csproj'
$publishOut = 'src\KelvinShift\bin\Release\net8.0-windows10.0.19041.0\win-x64\publish'
$iss       = 'installer\setup.iss'

if ($Clean) {
    Write-Host "Cleaning bin/obj..." -ForegroundColor Cyan
    Get-ChildItem -Recurse -Directory -Include bin,obj -ErrorAction SilentlyContinue |
        Remove-Item -Recurse -Force
}

Write-Host "[1/2] Publishing self-contained single-file (win-x64)..." -ForegroundColor Cyan
& dotnet publish $proj `
    -c Release `
    -r win-x64 `
    --self-contained true `
    -p:PublishSingleFile=true `
    -p:IncludeNativeLibrariesForSelfExtract=true `
    -p:EnableCompressionInSingleFile=true `
    -p:DebugType=embedded
if ($LASTEXITCODE -ne 0) { throw "dotnet publish failed (exit $LASTEXITCODE)" }

$exe = Join-Path $publishOut 'KelvinShift.exe'
if (-not (Test-Path $exe)) { throw "Published exe not found at $exe" }
$exeSize = '{0:N1} MB' -f ((Get-Item $exe).Length / 1MB)
Write-Host "    -> $exe ($exeSize)" -ForegroundColor Green

if ($SkipInstaller) {
    Write-Host "Skipping installer (-SkipInstaller)." -ForegroundColor Yellow
    return
}

Write-Host "[2/2] Building Inno Setup installer..." -ForegroundColor Cyan
$iscc = (Get-Command iscc -ErrorAction SilentlyContinue).Source
if (-not $iscc) {
    foreach ($p in @(
        "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe",
        "$env:ProgramFiles\Inno Setup 6\ISCC.exe",
        "${env:ProgramFiles(x86)}\Inno Setup 5\ISCC.exe"
    )) { if (Test-Path $p) { $iscc = $p; break } }
}
if (-not $iscc) {
    Write-Host "  ISCC.exe not found. Install Inno Setup 6 from https://jrsoftware.org/isdl.php" -ForegroundColor Yellow
    Write-Host "  Then re-run this script. The published binary is ready at:" -ForegroundColor Yellow
    Write-Host "    $publishOut" -ForegroundColor Yellow
    return
}

& $iscc $iss
if ($LASTEXITCODE -ne 0) { throw "ISCC failed (exit $LASTEXITCODE)" }

$installer = Get-ChildItem 'KelvinShift-*-Setup.exe' | Sort-Object LastWriteTime -Descending | Select-Object -First 1
if ($installer) {
    $insSize = '{0:N1} MB' -f ($installer.Length / 1MB)
    Write-Host ""
    Write-Host "    Installer: $($installer.FullName) ($insSize)" -ForegroundColor Green
    Write-Host ""
    Write-Host "    To install: run the installer (UAC prompt — needs admin for the gamma-range registry key)." -ForegroundColor Cyan
    Write-Host "    To uninstall: Settings -> Apps -> KelvinShift." -ForegroundColor Cyan
}
