#Requires -Version 5.1
<#
.SYNOPSIS
  One-click build for moqi-ime backend, moqi-im-windows binaries, and installer package.

.PARAMETER RepoRoot
  Root of moqi-im-windows (defaults to the parent directory of this script).

.PARAMETER MoqiImeRoot
  Root of sibling moqi-ime repository (defaults to RepoRoot\..\moqi-ime).

.PARAMETER Configuration
  Build configuration for moqi-im-windows (default: Release).

.PARAMETER Generator
  CMake generator for moqi-im-windows (default: Visual Studio 17 2022).
#>
param(
    [string] $RepoRoot = "",
    [string] $MoqiImeRoot = "",
    [string] $Configuration = "Release",
    [string] $Generator = "Visual Studio 17 2022"
)

$ErrorActionPreference = "Stop"

function Invoke-Step {
    param(
        [string] $FilePath,
        [string[]] $ArgumentList,
        [string] $WorkingDirectory
    )

    Write-Host ">> $FilePath $($ArgumentList -join ' ')"
    if ([string]::IsNullOrWhiteSpace($WorkingDirectory)) {
        & $FilePath @ArgumentList
    } else {
        Push-Location $WorkingDirectory
        try {
            & $FilePath @ArgumentList
        }
        finally {
            Pop-Location
        }
    }
    if ($LASTEXITCODE -ne 0) {
        throw "Command failed with exit code ${LASTEXITCODE}: $FilePath"
    }
}

$scriptRepoRoot = Join-Path $PSScriptRoot ".."
if (-not $RepoRoot) { $RepoRoot = $scriptRepoRoot }
$RepoRoot = [System.IO.Path]::GetFullPath($RepoRoot)

if (-not $MoqiImeRoot) { $MoqiImeRoot = Join-Path $RepoRoot "..\moqi-ime" }
$MoqiImeRoot = [System.IO.Path]::GetFullPath($MoqiImeRoot)

$moqiImeBuildScript = Join-Path $MoqiImeRoot "scripts\build.ps1"
$windowsBuildScript = Join-Path $RepoRoot "scripts\build.ps1"
$windowsInstallScript = Join-Path $RepoRoot "scripts\install.ps1"
$moqiImeRuntimeDir = Join-Path $MoqiImeRoot "scripts\build\moqi-ime"

foreach ($path in @($moqiImeBuildScript, $windowsBuildScript, $windowsInstallScript)) {
    if (-not (Test-Path -LiteralPath $path)) {
        throw "Required script not found: $path"
    }
}

Write-Host "== Step 1/3: Build moqi-ime runtime =="
Invoke-Step -FilePath "powershell.exe" -ArgumentList @(
    "-NoProfile",
    "-ExecutionPolicy", "Bypass",
    "-File", "`"$moqiImeBuildScript`"",
    "-RepoRoot", "`"$MoqiImeRoot`""
) -WorkingDirectory $MoqiImeRoot

if (-not (Test-Path -LiteralPath (Join-Path $moqiImeRuntimeDir "server.exe"))) {
    throw "moqi-ime runtime was not produced: $moqiImeRuntimeDir"
}

Write-Host "== Step 2/3: Build moqi-im-windows binaries =="
Invoke-Step -FilePath "powershell.exe" -ArgumentList @(
    "-NoProfile",
    "-ExecutionPolicy", "Bypass",
    "-File", "`"$windowsBuildScript`"",
    "-RepoRoot", "`"$RepoRoot`"",
    "-Configuration", $Configuration,
    "-Generator", "`"$Generator`""
) -WorkingDirectory $RepoRoot

Write-Host "== Step 3/3: Build installer package =="
Invoke-Step -FilePath "powershell.exe" -ArgumentList @(
    "-NoProfile",
    "-ExecutionPolicy", "Bypass",
    "-File", "`"$windowsInstallScript`"",
    "-RepoRoot", "`"$RepoRoot`"",
    "-MoqiImeSource", "`"$moqiImeRuntimeDir`""
) -WorkingDirectory $RepoRoot

$installerPath = Join-Path $RepoRoot "installer\dist\moqi-im-windows-setup.exe"
if (Test-Path -LiteralPath $installerPath) {
    Write-Host "OK: $installerPath"
} else {
    throw "Installer was not produced: $installerPath"
}
