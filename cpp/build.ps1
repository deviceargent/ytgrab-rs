# Builds the C++ port with the portable toolchain in the temp folder.
#
# Usage:  .\build.ps1            # configure + build
#         .\build.ps1 -Test      # ... and run the unit tests
#         .\build.ps1 -Clean     # wipe the build directory first
param(
    [switch]$Test,
    [switch]$Clean,
    [string]$BuildDir = "$PSScriptRoot\build"
)

$ErrorActionPreference = 'Stop'
$toolchain = Join-Path $env:LOCALAPPDATA 'Temp\opencode\toolchain'

$paths = @(
    (Join-Path $toolchain 'mingw64\bin'),
    (Join-Path $toolchain 'cmake-3.31.6-windows-x86_64\bin'),
    (Join-Path $toolchain 'ninja')
) | Where-Object { Test-Path $_ }

foreach ($path in $paths) { $env:PATH = "$path;$env:PATH" }

$cmake = Get-Command cmake.exe -ErrorAction SilentlyContinue
if (-not $cmake) { throw 'cmake not found; install CMake or fix $toolchain in build.ps1' }

if ($Clean -and (Test-Path $BuildDir)) { Remove-Item -Recurse -Force $BuildDir }

cmake -S $PSScriptRoot -B $BuildDir -G Ninja `
    -DCMAKE_BUILD_TYPE=Release `
    -DCMAKE_C_COMPILER=gcc `
    -DCMAKE_CXX_COMPILER=g++
if ($LASTEXITCODE -ne 0) { throw 'cmake configure failed' }

cmake --build $BuildDir
if ($LASTEXITCODE -ne 0) { throw 'build failed' }

if ($Test) {
    & (Join-Path $BuildDir 'ytgrab_tests.exe')
    if ($LASTEXITCODE -ne 0) { throw 'tests failed' }
}

Write-Host "`nBinary: $(Join-Path $BuildDir 'ytgrab.exe')" -ForegroundColor Green
