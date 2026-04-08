#Requires -Version 5.1
<#
.SYNOPSIS
  Build Win32 and x64 Moqi IM for Windows binaries with CMake.

.PARAMETER RepoRoot
  Root of moqi-im-windows (defaults to the parent directory of this script).

.PARAMETER Win32BuildDir
  CMake Win32 build directory (default: RepoRoot\build).

.PARAMETER X64BuildDir
  CMake x64 build directory (default: RepoRoot\build64).

.PARAMETER Configuration
  Build configuration (default: Release).

.PARAMETER Generator
  CMake generator (default: Visual Studio 17 2022).
#>
param(
  [string] $RepoRoot = "",
  [string] $Win32BuildDir = "",
  [string] $X64BuildDir = "",
  [string] $Configuration = "Release",
  [string] $Generator = "Visual Studio 17 2022"
)

$ErrorActionPreference = "Stop"

function Invoke-Step {
  param(
    [string] $FilePath,
    [string[]] $ArgumentList
  )

  Write-Host ">> $FilePath $($ArgumentList -join ' ')"
  & $FilePath @ArgumentList
  if ($LASTEXITCODE -ne 0) {
    throw "Command failed with exit code ${LASTEXITCODE}: $FilePath"
  }
}

$scriptRepoRoot = Join-Path $PSScriptRoot ".."
if (-not $RepoRoot) { $RepoRoot = $scriptRepoRoot }
$RepoRoot = [System.IO.Path]::GetFullPath($RepoRoot)

if (-not $Win32BuildDir) { $Win32BuildDir = Join-Path $RepoRoot "build" }
if (-not $X64BuildDir) { $X64BuildDir = Join-Path $RepoRoot "build64" }
$Win32BuildDir = [System.IO.Path]::GetFullPath($Win32BuildDir)
$X64BuildDir = [System.IO.Path]::GetFullPath($X64BuildDir)

Invoke-Step -FilePath "cmake" -ArgumentList @(
  "-S", $RepoRoot,
  "-B", $Win32BuildDir,
  "-G", $Generator,
  "-A", "Win32"
)
Invoke-Step -FilePath "cmake" -ArgumentList @(
  "--build", $Win32BuildDir,
  "--config", $Configuration
)

Invoke-Step -FilePath "cmake" -ArgumentList @(
  "-S", $RepoRoot,
  "-B", $X64BuildDir,
  "-G", $Generator,
  "-A", "x64"
)
Invoke-Step -FilePath "cmake" -ArgumentList @(
  "--build", $X64BuildDir,
  "--config", $Configuration,
  "--target", "MoqiTextService"
)

Write-Host "OK: Win32 $Configuration (full solution), x64 $Configuration (MoqiTextService)."
