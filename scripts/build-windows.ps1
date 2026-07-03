#Requires -Version 5.1
<#
.SYNOPSIS
  Configure and build CutWire Prism on Windows (MSVC + vcpkg + Qt).

.PARAMETER QtPath
  Qt MSVC kit prefix, e.g. C:\Qt\6.8.0\msvc2022_64

.PARAMETER VcpkgRoot
  vcpkg root directory. Defaults to ..\vcpkg relative to the repo.

.PARAMETER Config
  CMake build configuration (Release or Debug). Default: Release

.PARAMETER Deploy
  Run windeployqt after a successful build.

.EXAMPLE
  .\scripts\build-windows.ps1 -QtPath C:\Qt\6.8.0\msvc2022_64
#>
param(
    [Parameter(Mandatory = $true)]
    [string]$QtPath,

    [string]$VcpkgRoot = "",

    [ValidateSet("Release", "Debug")]
    [string]$Config = "Release",

    [switch]$Deploy,

    [switch]$WithLua,
    [switch]$WithNdi,
    [switch]$WithWebRtc
)

$ErrorActionPreference = "Stop"
$RepoRoot = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)

if (-not $VcpkgRoot) {
    $VcpkgRoot = Join-Path (Split-Path -Parent $RepoRoot) "vcpkg"
}
$VcpkgExe = Join-Path $VcpkgRoot "vcpkg.exe"
$Toolchain = Join-Path $VcpkgRoot "scripts\buildsystems\vcpkg.cmake"

if (-not (Test-Path $Toolchain)) {
    Write-Error "vcpkg toolchain not found at $Toolchain. Clone vcpkg and bootstrap first."
}
if (-not (Test-Path (Join-Path $QtPath "lib\cmake\Qt6\Qt6Config.cmake"))) {
    Write-Error "Qt6 not found at $QtPath"
}

$cmake = "cmake"
if (Test-Path "C:\Program Files\CMake\bin\cmake.exe") {
    $cmake = "C:\Program Files\CMake\bin\cmake.exe"
}

$vcpkgPackages = @("re2", "ffmpeg", "libzip")
if ($WithLua) { $vcpkgPackages += "lua" }
if ($WithWebRtc) { $vcpkgPackages += "openssl" }

Write-Host "==> Installing vcpkg packages: $($vcpkgPackages -join ', ')"
& $VcpkgExe install @vcpkgPackages --triplet x64-windows
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

$buildDir = Join-Path $RepoRoot "build"
$cmakeArgs = @(
    "-B", $buildDir,
    "-G", "Visual Studio 17 2022",
    "-A", "x64",
    "-DCMAKE_TOOLCHAIN_FILE=$Toolchain",
    "-DCMAKE_PREFIX_PATH=$QtPath",
    "-DPRISM_WITH_NDI=$(if ($WithNdi) { 'ON' } else { 'OFF' })",
    "-DPRISM_WITH_WEBRTC=$(if ($WithWebRtc) { 'ON' } else { 'OFF' })",
    "-DPRISM_WITH_LUA=$(if ($WithLua) { 'ON' } else { 'OFF' })"
)

Write-Host "==> Configuring CutWire Prism"
& $cmake @cmakeArgs $RepoRoot
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Write-Host "==> Building ($Config)"
& $cmake --build $buildDir --config $Config --parallel
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

$exe = Join-Path $buildDir "$Config\Prism.exe"
Write-Host "==> Build complete: $exe"

if ($Deploy) {
    $windeployqt = Join-Path $QtPath "bin\windeployqt.exe"
    if (-not (Test-Path $windeployqt)) {
        Write-Error "windeployqt not found at $windeployqt"
    }
    Write-Host "==> Deploying Qt runtime DLLs"
    & $windeployqt --no-translations --no-compiler-runtime $exe
}

Write-Host "Run: $exe"
