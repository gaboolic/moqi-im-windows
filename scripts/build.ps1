#Requires -Version 5.1
<#
.SYNOPSIS
  Build Win32 and x64 Moqi IM for Windows binaries with CMake.

.PARAMETER RepoRoot
  Root of moqi-im-windows (defaults to the parent directory of this script).

.PARAMETER Win32BuildDir
  CMake Win32 build directory (default: RepoRoot\build-vs32).

.PARAMETER X64BuildDir
  CMake x64 build directory (default: RepoRoot\build-vs64).

.PARAMETER Configuration
  Build configuration (default: Release).

.PARAMETER Generator
  CMake generator (default: Visual Studio 17 2022).

.PARAMETER ProtobufRoot
  Optional local protobuf/protoc install root passed to CMake as MOQI_PROTOBUF_ROOT.

.PARAMETER ProtobufSourceDir
  Optional local protobuf source tree passed to CMake as MOQI_PROTOBUF_SOURCE_DIR.
#>
param(
  [string] $RepoRoot = "",
  [string] $Win32BuildDir = "",
  [string] $X64BuildDir = "",
  [string] $Configuration = "Release",
  [string] $Generator = "Visual Studio 17 2022",
  [string] $ProtobufRoot = "",
  [string] $ProtobufSourceDir = ""
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

function Resolve-ProtobufSourceDir {
  param(
    [string] $RequestedPath,
    [string] $RepoRoot
  )

  $candidates = @()
  if (-not [string]::IsNullOrWhiteSpace($RequestedPath)) {
    $candidates += $RequestedPath
  }
  if (-not [string]::IsNullOrWhiteSpace($env:MOQI_PROTOBUF_SOURCE_DIR)) {
    $candidates += $env:MOQI_PROTOBUF_SOURCE_DIR
  }
  if (-not [string]::IsNullOrWhiteSpace($env:USERPROFILE)) {
    $cacheRoot = Join-Path $env:USERPROFILE ".cache\moqi-protobuf"
    $candidates += @(
      (Join-Path $cacheRoot "protobuf-33.5"),
      (Join-Path $cacheRoot "protobuf-34.1"),
      (Join-Path $cacheRoot "protobuf-29.5")
    )
  }

  foreach ($candidate in $candidates) {
    if ([string]::IsNullOrWhiteSpace($candidate)) {
      continue
    }
    $fullPath = [System.IO.Path]::GetFullPath($candidate)
    if (Test-Path -LiteralPath (Join-Path $fullPath "CMakeLists.txt")) {
      return $fullPath
    }
  }

  return ""
}

function Resolve-ProtobufRoot {
  param(
    [string] $RequestedPath
  )

  $candidates = @()
  if (-not [string]::IsNullOrWhiteSpace($RequestedPath)) {
    $candidates += $RequestedPath
  }
  if (-not [string]::IsNullOrWhiteSpace($env:MOQI_PROTOBUF_ROOT)) {
    $candidates += $env:MOQI_PROTOBUF_ROOT
  }
  $defaultRoot = "D:\a_dev\protoc-33.5-win64"
  if (Test-Path -LiteralPath $defaultRoot) {
    $candidates += $defaultRoot
  }

  foreach ($candidate in $candidates) {
    if ([string]::IsNullOrWhiteSpace($candidate)) {
      continue
    }
    $fullPath = [System.IO.Path]::GetFullPath($candidate)
    if ((Test-Path -LiteralPath (Join-Path $fullPath "bin\protoc.exe")) -or
        (Test-Path -LiteralPath (Join-Path $fullPath "include"))) {
      return $fullPath
    }
  }

  return ""
}

$scriptRepoRoot = Join-Path $PSScriptRoot ".."
if (-not $RepoRoot) { $RepoRoot = $scriptRepoRoot }
$RepoRoot = [System.IO.Path]::GetFullPath($RepoRoot)

if (-not $Win32BuildDir) { $Win32BuildDir = Join-Path $RepoRoot "build-vs32" }
if (-not $X64BuildDir) { $X64BuildDir = Join-Path $RepoRoot "build-vs64" }
$Win32BuildDir = [System.IO.Path]::GetFullPath($Win32BuildDir)
$X64BuildDir = [System.IO.Path]::GetFullPath($X64BuildDir)
$ProtobufRoot = Resolve-ProtobufRoot -RequestedPath $ProtobufRoot
$ProtobufSourceDir = Resolve-ProtobufSourceDir -RequestedPath $ProtobufSourceDir -RepoRoot $RepoRoot

$commonConfigureArgs = @("-S", $RepoRoot)
if (-not [string]::IsNullOrWhiteSpace($ProtobufRoot)) {
  Write-Host "[INFO] Using local protobuf root: $ProtobufRoot"
  $commonConfigureArgs += "-DMOQI_PROTOBUF_ROOT=$ProtobufRoot"
  $protocExe = Join-Path $ProtobufRoot "bin\protoc.exe"
  if (Test-Path -LiteralPath $protocExe) {
    $commonConfigureArgs += "-DMOQI_PROTOC_EXECUTABLE=$protocExe"
  }
}
if (-not [string]::IsNullOrWhiteSpace($ProtobufSourceDir)) {
  Write-Host "[INFO] Using local protobuf source: $ProtobufSourceDir"
  $commonConfigureArgs += "-DMOQI_PROTOBUF_SOURCE_DIR=$ProtobufSourceDir"
}
$win32ConfigureArgs = $commonConfigureArgs + @(
  "-B", $Win32BuildDir,
  "-G", $Generator,
  "-A", "Win32"
)
$x64ConfigureArgs = $commonConfigureArgs + @(
  "-B", $X64BuildDir,
  "-G", $Generator,
  "-A", "x64"
)

Invoke-Step -FilePath "cmake" -ArgumentList $win32ConfigureArgs
Invoke-Step -FilePath "cmake" -ArgumentList @(
  "--build", $Win32BuildDir,
  "--config", $Configuration
)

Invoke-Step -FilePath "cmake" -ArgumentList $x64ConfigureArgs
Invoke-Step -FilePath "cmake" -ArgumentList @(
  "--build", $X64BuildDir,
  "--config", $Configuration,
  "--target", "MoqiTextService"
)

Write-Host "OK: Win32 $Configuration (full solution), x64 $Configuration (MoqiTextService)."
