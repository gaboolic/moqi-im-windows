#Requires -Version 5.1
<#
.SYNOPSIS
  Stage Moqi IM for Windows binaries and invoke the installer builder.

  Does not install files into Program Files directly. Instead it prepares an
  installer stage tree and calls installer\build-installer.ps1 to produce the
  setup executable.

.PARAMETER RepoRoot
  Root of moqi-im-windows (defaults to the parent directory of this script).

.PARAMETER Win32BuildDir
  CMake Win32 Release output directory (default: RepoRoot\build\Release).

.PARAMETER X64BuildDir
  CMake x64 Release output directory (default: RepoRoot\build64\Release).

.PARAMETER MoqiImeSource
  Path to the moqi-ime tree to copy as backend (default: sibling ..\moqi-ime next to RepoRoot).

.PARAMETER SkipMoqiImeCopy
  If set, do not include the backend tree in the staged installer payload.

.PARAMETER StageDir
  Installer staging directory (default: RepoRoot\installer\stage).

.PARAMETER IssPath
  Optional path to the Inno Setup script (default: RepoRoot\installer\MoqiTsf.iss).
#>
param(
    [string] $RepoRoot = "",
    [string] $Win32BuildDir = "",
    [string] $X64BuildDir = "",
    [string] $MoqiImeSource = "",
    [switch] $SkipMoqiImeCopy,
    [string] $StageDir = "",
    [string] $IssPath = ""
)

$ErrorActionPreference = "Stop"

function New-CleanDirectory {
    param([string] $Path)

    if (Test-Path -LiteralPath $Path) {
        Remove-Item -LiteralPath $Path -Recurse -Force
    }
    New-Item -ItemType Directory -Path $Path -Force | Out-Null
}

function Copy-IfExists {
    param(
        [string] $Source,
        [string] $Destination
    )

    if (-not (Test-Path -LiteralPath $Source)) {
        throw "Required file not found: $Source"
    }
    Copy-Item -LiteralPath $Source -Destination $Destination -Force
}

function Resolve-ArtifactPath {
    param(
        [string[]] $Candidates,
        [string] $Label
    )

    foreach ($candidate in $Candidates) {
        if (Test-Path -LiteralPath $candidate) {
            return [System.IO.Path]::GetFullPath($candidate)
        }
    }

    throw "$Label not found. Checked: $($Candidates -join ', ')"
}

function Copy-MoqiImeRuntime {
    param(
        [string] $SourceRoot,
        [string] $DestinationRoot
    )

    $serverExe = Join-Path $SourceRoot "server.exe"
    if (-not (Test-Path -LiteralPath $serverExe)) {
        throw "moqi-ime server.exe not found: $serverExe"
    }

    New-Item -ItemType Directory -Path $DestinationRoot -Force | Out-Null

    $directories = Get-ChildItem -Path $SourceRoot -Recurse -Force -Directory
    foreach ($directory in $directories) {
        $relativePath = $directory.FullName.Substring($SourceRoot.Length).TrimStart('\', '/')
        $targetDir = Join-Path $DestinationRoot $relativePath
        New-Item -ItemType Directory -Path $targetDir -Force | Out-Null
    }

    $files = Get-ChildItem -Path $SourceRoot -Recurse -Force -File | Where-Object { $_.Extension -ne ".go" }
    foreach ($file in $files) {
        $relativePath = $file.FullName.Substring($SourceRoot.Length).TrimStart('\', '/')
        $targetPath = Join-Path $DestinationRoot $relativePath
        $targetDir = Split-Path -Parent $targetPath
        if (-not (Test-Path -LiteralPath $targetDir)) {
            New-Item -ItemType Directory -Path $targetDir -Force | Out-Null
        }
        Copy-Item -LiteralPath $file.FullName -Destination $targetPath -Force
    }
}

$scriptRepoRoot = Join-Path $PSScriptRoot ".."
if (-not $RepoRoot) { $RepoRoot = $scriptRepoRoot }
$RepoRoot = [System.IO.Path]::GetFullPath($RepoRoot)

if (-not $Win32BuildDir) { $Win32BuildDir = Join-Path $RepoRoot "build\Release" }
if (-not $X64BuildDir) { $X64BuildDir = Join-Path $RepoRoot "build64\Release" }
if (-not $MoqiImeSource) { $MoqiImeSource = Join-Path $RepoRoot "..\moqi-ime" }
if (-not $StageDir) { $StageDir = Join-Path $RepoRoot "installer\stage" }
if (-not $IssPath) { $IssPath = Join-Path $RepoRoot "installer\MoqiTsf.iss" }
$Win32BuildDir = [System.IO.Path]::GetFullPath($Win32BuildDir)
$X64BuildDir = [System.IO.Path]::GetFullPath($X64BuildDir)
$MoqiImeSource = [System.IO.Path]::GetFullPath($MoqiImeSource)
$StageDir = [System.IO.Path]::GetFullPath($StageDir)
$IssPath = [System.IO.Path]::GetFullPath($IssPath)

$stageWin32Root = Join-Path $StageDir "win32\MoqiIM"
$stageX64Root = Join-Path $StageDir "x64\MoqiIM"
New-CleanDirectory -Path $StageDir
New-Item -ItemType Directory -Path $stageWin32Root -Force | Out-Null
New-Item -ItemType Directory -Path $stageX64Root -Force | Out-Null

$backends = Join-Path $RepoRoot "backends.json"
if (-not (Test-Path -LiteralPath $backends)) {
    throw "Missing backends.json at $backends"
}
Copy-Item -LiteralPath $backends -Destination (Join-Path $stageWin32Root "backends.json") -Force

$launcher = Resolve-ArtifactPath -Label "MoqLauncher.exe" -Candidates @(
    (Join-Path $Win32BuildDir "MoqLauncher.exe"),
    (Join-Path $Win32BuildDir "Release\MoqLauncher.exe"),
    (Join-Path $Win32BuildDir "MoqLauncher\Release\MoqLauncher.exe")
)
Copy-IfExists -Source $launcher -Destination (Join-Path $stageWin32Root "MoqLauncher.exe")

$dll32 = Resolve-ArtifactPath -Label "Win32 MoqiTextService.dll" -Candidates @(
    (Join-Path $Win32BuildDir "MoqiTextService.dll"),
    (Join-Path $Win32BuildDir "Release\MoqiTextService.dll"),
    (Join-Path $Win32BuildDir "MoqiTextService\Release\MoqiTextService.dll")
)
Copy-IfExists -Source $dll32 -Destination (Join-Path $stageWin32Root "MoqiTextService.dll")

$dll64 = Resolve-ArtifactPath -Label "x64 MoqiTextService.dll" -Candidates @(
    (Join-Path $X64BuildDir "MoqiTextService.dll"),
    (Join-Path $X64BuildDir "Release\MoqiTextService.dll"),
    (Join-Path $X64BuildDir "MoqiTextService\Release\MoqiTextService.dll")
)
Copy-IfExists -Source $dll64 -Destination (Join-Path $stageX64Root "MoqiTextService.dll")

if (-not $SkipMoqiImeCopy) {
    if (-not (Test-Path -LiteralPath $MoqiImeSource)) {
        throw "Moqi IME source not found: $MoqiImeSource (use -MoqiImeSource or -SkipMoqiImeCopy)."
    }
    $imeDest = Join-Path $stageWin32Root "moqi-ime"
    Copy-MoqiImeRuntime -SourceRoot $MoqiImeSource -DestinationRoot $imeDest
} else {
    Write-Warning "Skipped copying moqi-ime backend; ensure the final installer payload is sufficient for your deployment."
}

$installerScript = Join-Path $RepoRoot "installer\build-installer.ps1"
if (-not (Test-Path -LiteralPath $installerScript)) {
    throw "Installer builder script not found: $installerScript"
}
if (-not (Test-Path -LiteralPath $IssPath)) {
    throw "Installer ISS file not found: $IssPath"
}

Write-Host "Stage prepared at: $StageDir"
Write-Host "Win32 payload: $stageWin32Root"
Write-Host "x64 payload: $stageX64Root"

& $installerScript -StageDir $StageDir -IssPath $IssPath

Write-Host "Installer build finished."
