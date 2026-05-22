# build.ps1 - Build the KelvinShift Windows port (pure Win32 / C++) and
# optionally package it as an Inno Setup installer.
#
# Usage:
#   .\build.ps1                  # full build: compile + installer
#   .\build.ps1 -SkipInstaller   # just compile
#   .\build.ps1 -Clean           # wipe the build dir first
#   .\build.ps1 -Debug           # unoptimized build with symbols
#
# Produces a single statically-linked KelvinShift.exe (no .NET runtime, no
# VC redist) — that is the whole point of the C++ port: a few-MB working set
# instead of the WPF runtime's ~80 MB.

param(
    [switch]$SkipInstaller,
    [switch]$Clean,
    [switch]$Debug
)

$ErrorActionPreference = 'Stop'
Set-Location $PSScriptRoot

$srcDir   = Join-Path $PSScriptRoot 'src'
$buildDir = Join-Path $PSScriptRoot 'build'
$iss      = Join-Path $PSScriptRoot 'installer\setup.iss'

if ($Clean -and (Test-Path $buildDir)) {
    Write-Host 'Cleaning build dir...' -ForegroundColor Cyan
    Remove-Item $buildDir -Recurse -Force
}
New-Item -ItemType Directory -Force -Path $buildDir | Out-Null

# ── Locate the MSVC environment ───────────────────────────────────────────
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path $vswhere)) { throw 'vswhere.exe not found - install Visual Studio Build Tools.' }
$vsRoot = & $vswhere -latest -products * `
    -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
    -property installationPath
if (-not $vsRoot) { throw 'No MSVC C++ toolset found. Install the "Desktop development with C++" workload.' }
$vcvars = Join-Path $vsRoot 'VC\Auxiliary\Build\vcvars64.bat'
if (-not (Test-Path $vcvars)) { throw "vcvars64.bat not found at $vcvars" }

# ── Compose the compiler invocation ───────────────────────────────────────
# /MT  : static CRT  -> self-contained exe
# /utf-8: source + execution charset UTF-8 so the emoji glyph literals and
#         L"..." wide strings encode correctly.
# /O2 /GL + /LTCG: optimized release; /Od /Zi for -Debug.
$opt = if ($Debug) { '/Od /Zi /MTd /D_DEBUG' } else { '/O2 /GL /MT /DNDEBUG' }
$ltcg = if ($Debug) { '' } else { '/LTCG' }

$libs = @(
    'user32.lib','gdi32.lib','gdiplus.lib','shell32.lib','advapi32.lib',
    'ole32.lib','shlwapi.lib','dwmapi.lib','comctl32.lib','wtsapi32.lib',
    'uxtheme.lib','windowscodecs.lib','msimg32.lib','uuid.lib','winhttp.lib','WindowsApp.lib'
) -join ' '

$res = Join-Path $buildDir 'app.res'
$exe = Join-Path $buildDir 'KelvinShift.exe'

# Single cmd session: import vcvars, compile the .rc, then compile+link.
$cl = @(
    "cl /nologo /std:c++20 /EHsc /utf-8 $opt",
    '/DUNICODE /D_UNICODE /DWIN32_LEAN_AND_MEAN /DNOMINMAX /D_WIN32_WINNT=0x0A00',
    "/Fo`"$buildDir\\`" /Fd`"$buildDir\\`"",
    "`"$srcDir\\*.cpp`"",
    "`"$res`"",
    "/Fe`"$exe`"",
    "/link /SUBSYSTEM:WINDOWS $ltcg $libs"
) -join ' '

$installerDir = Split-Path $vswhere
$script = @"
@echo off
rem vcvars' inner VsDevCmd expects vswhere.exe on PATH.
set "PATH=%PATH%;$installerDir"
call "$vcvars" >nul || exit /b 1
echo [1/2] Compiling resources...
rc /nologo /fo "$res" "$srcDir\app.rc" || exit /b 1
echo [2/2] Compiling + linking...
$cl || exit /b 1
"@
$bat = Join-Path $buildDir '_build.bat'
Set-Content -Path $bat -Value $script -Encoding ascii

& cmd /c $bat
if ($LASTEXITCODE -ne 0) { throw "Build failed (exit $LASTEXITCODE)" }

if (-not (Test-Path $exe)) { throw "Expected output $exe was not produced." }
$sizeMB = '{0:N2} MB' -f ((Get-Item $exe).Length / 1MB)
Write-Host "    -> $exe ($sizeMB)" -ForegroundColor Green

if ($SkipInstaller) { Write-Host 'Skipping installer (-SkipInstaller).' -ForegroundColor Yellow; return }

# ── Inno Setup installer ──────────────────────────────────────────────────
$iscc = (Get-Command iscc -ErrorAction SilentlyContinue).Source
if (-not $iscc) {
    foreach ($p in @(
        "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe",
        "$env:ProgramFiles\Inno Setup 6\ISCC.exe")) {
        if (Test-Path $p) { $iscc = $p; break }
    }
}
if (-not $iscc) {
    Write-Host '  ISCC.exe not found. Install Inno Setup 6 from https://jrsoftware.org/isdl.php' -ForegroundColor Yellow
    Write-Host "  The compiled binary is ready at: $exe" -ForegroundColor Yellow
    return
}
& $iscc $iss
if ($LASTEXITCODE -ne 0) { throw "ISCC failed (exit $LASTEXITCODE)" }

$installer = Get-ChildItem 'KelvinShift-*-Setup.exe' -ErrorAction SilentlyContinue |
    Sort-Object LastWriteTime -Descending | Select-Object -First 1
if ($installer) {
    Write-Host ''
    Write-Host "    Installer: $($installer.FullName)" -ForegroundColor Green
}
