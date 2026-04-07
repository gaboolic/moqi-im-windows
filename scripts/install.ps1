#Requires -Version 5.1
<#
.SYNOPSIS
  Deploy Moqi IM TSF binaries and backend into Program Files and register the DLLs.

  Does not build. Run from an elevated PowerShell if copying under Program Files fails.

.PARAMETER RepoRoot
  Root of moqi-im-windows (defaults to this script's directory).

.PARAMETER Win32BuildDir
  CMake Win32 Release output directory (default: RepoRoot\build\Release).

.PARAMETER X64BuildDir
  CMake x64 Release output directory (default: RepoRoot\build64\Release).

.PARAMETER MoqiImeSource
  Path to the moqi-ime tree to copy as backend (default: sibling ..\moqi-ime next to RepoRoot).

.PARAMETER SkipMoqiImeCopy
  If set, do not copy the backend tree (you must supply moqi-ime\server.exe yourself).
#>
param(
    [string] $RepoRoot = $PSScriptRoot,
    [string] $Win32BuildDir = "",
    [string] $X64BuildDir = "",
    [string] $MoqiImeSource = "",
    [switch] $SkipMoqiImeCopy
)

$ErrorActionPreference = "Stop"

if (-not $Win32BuildDir) { $Win32BuildDir = Join-Path $RepoRoot "build\Release" }
if (-not $X64BuildDir) { $X64BuildDir = Join-Path $RepoRoot "build64\Release" }
if (-not $MoqiImeSource) { $MoqiImeSource = Join-Path $RepoRoot "..\moqi-ime" }
$MoqiImeSource = [System.IO.Path]::GetFullPath($MoqiImeSource)

$pf32 = ${env:ProgramFiles(x86)}
if (-not $pf32) { $pf32 = $env:ProgramFiles }
$dest32 = Join-Path $pf32 "MoqiIM"
$dest64 = Join-Path $env:ProgramFiles "MoqiIM"

if (-not (Test-Path -LiteralPath $dest32)) {
    New-Item -ItemType Directory -Path $dest32 -Force | Out-Null
}
if ([Environment]::Is64BitOperatingSystem -and -not (Test-Path -LiteralPath $dest64)) {
    New-Item -ItemType Directory -Path $dest64 -Force | Out-Null
}

$backends = Join-Path $RepoRoot "backends.json"
if (-not (Test-Path -LiteralPath $backends)) {
    throw "Missing backends.json at $backends"
}
Copy-Item -LiteralPath $backends -Destination (Join-Path $dest32 "backends.json") -Force

$launcher = Join-Path $Win32BuildDir "MoqLauncher.exe"
if (Test-Path -LiteralPath $launcher) {
    Copy-Item -LiteralPath $launcher -Destination (Join-Path $dest32 "MoqLauncher.exe") -Force
} else {
    Write-Warning "MoqLauncher.exe not found at $launcher (Win32 Release build required)."
}

$dll32 = Join-Path $Win32BuildDir "MoqiTextService.dll"
if (Test-Path -LiteralPath $dll32) {
    Copy-Item -LiteralPath $dll32 -Destination (Join-Path $dest32 "MoqiTextService.dll") -Force
} else {
    Write-Warning "Win32 MoqiTextService.dll not found at $dll32."
}

$dll64 = Join-Path $X64BuildDir "MoqiTextService.dll"
if ([Environment]::Is64BitOperatingSystem -and (Test-Path -LiteralPath $dll64)) {
    Copy-Item -LiteralPath $dll64 -Destination (Join-Path $dest64 "MoqiTextService.dll") -Force
} elseif ([Environment]::Is64BitOperatingSystem) {
    Write-Warning "x64 MoqiTextService.dll not found at $dll64."
}

if (-not $SkipMoqiImeCopy) {
    if (-not (Test-Path -LiteralPath $MoqiImeSource)) {
        throw "Moqi IME source not found: $MoqiImeSource (use -MoqiImeSource or -SkipMoqiImeCopy)."
    }
    $imeDest = Join-Path $dest32 "moqi-ime"
    if (Test-Path -LiteralPath $imeDest) {
        Remove-Item -LiteralPath $imeDest -Recurse -Force
    }
    Copy-Item -LiteralPath $MoqiImeSource -Destination $imeDest -Recurse -Force
} else {
    Write-Warning "Skipped copying moqi-ime backend; ensure $dest32\moqi-ime matches backends.json."
}

# Register TSF DLLs (WOW64 vs native regsvr32 on 64-bit Windows)
$regsvr32Native = Join-Path $env:windir "System32\regsvr32.exe"
$regsvr32Wow = Join-Path $env:windir "SysWOW64\regsvr32.exe"

if ([Environment]::Is64BitOperatingSystem) {
    if (Test-Path -LiteralPath (Join-Path $dest32 "MoqiTextService.dll")) {
        if (Test-Path -LiteralPath $regsvr32Wow) {
            & $regsvr32Wow /s (Join-Path $dest32 "MoqiTextService.dll")
        }
    }
    if (Test-Path -LiteralPath (Join-Path $dest64 "MoqiTextService.dll")) {
        & $regsvr32Native /s (Join-Path $dest64 "MoqiTextService.dll")
    }
} else {
    if (Test-Path -LiteralPath (Join-Path $dest32 "MoqiTextService.dll")) {
        & $regsvr32Native /s (Join-Path $dest32 "MoqiTextService.dll")
    }
}

$runKey = "HKCU:\Software\Microsoft\Windows\CurrentVersion\Run"
$launcherDest = Join-Path $dest32 "MoqLauncher.exe"
if (Test-Path -LiteralPath $launcherDest) {
    Set-ItemProperty -Path $runKey -Name "MoqLauncher" -Value "`"$launcherDest`"" -Type String
} else {
    Write-Warning "Run key not set: MoqLauncher.exe missing at $launcherDest"
}

Write-Host "Install layout: $dest32 (launcher, Win32 DLL, backends, moqi-ime); x64 DLL: $dest64"
