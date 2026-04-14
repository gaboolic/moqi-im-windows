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
  CMake Win32 build directory (default: RepoRoot\build-vs32).

.PARAMETER X64BuildDir
  CMake x64 build directory (default: RepoRoot\build-vs64).

.PARAMETER MoqiImeSource
  Path to the moqi-ime runtime tree to copy as backend.
  Default detection order:
    1. sibling ..\moqi-ime\scripts\build\moqi-ime
    2. sibling ..\moqi-ime

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

    $existingCandidates = foreach ($candidate in $Candidates) {
        if (Test-Path -LiteralPath $candidate) {
            Get-Item -LiteralPath $candidate
        }
    }

    if (-not $existingCandidates) {
        throw "$Label not found. Checked: $($Candidates -join ', ')"
    }

    $selected = $existingCandidates |
    Sort-Object LastWriteTime -Descending |
    Select-Object -First 1

    Write-Host ("Using {0}: {1} ({2})" -f $Label, $selected.FullName, $selected.LastWriteTime)
    return $selected.FullName
}

function Resolve-MoqiImeSource {
    param(
        [string] $RepoRoot,
        [string] $RequestedSource
    )

    if (-not [string]::IsNullOrWhiteSpace($RequestedSource)) {
        return [System.IO.Path]::GetFullPath($RequestedSource)
    }

    $candidates = @(
        (Join-Path $RepoRoot "..\moqi-ime\scripts\build\moqi-ime"),
        (Join-Path $RepoRoot "..\moqi-ime")
    )

    foreach ($candidate in $candidates) {
        $fullPath = [System.IO.Path]::GetFullPath($candidate)
        if (Test-Path -LiteralPath (Join-Path $fullPath "server.exe")) {
            return $fullPath
        }
    }

    return [System.IO.Path]::GetFullPath((Join-Path $RepoRoot "..\moqi-ime\scripts\build\moqi-ime"))
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

    $directories = Get-ChildItem -Path $SourceRoot -Recurse -Force -Directory |
    Where-Object { $_.FullName -notmatch '[\\/]\.git(?:[\\/]|$)' }
    foreach ($directory in $directories) {
        $relativePath = $directory.FullName.Substring($SourceRoot.Length).TrimStart('\', '/')
        $targetDir = Join-Path $DestinationRoot $relativePath
        New-Item -ItemType Directory -Path $targetDir -Force | Out-Null
    }

    $files = Get-ChildItem -Path $SourceRoot -Recurse -Force -File | Where-Object {
        $_.Extension -ne ".go" -and $_.FullName -notmatch '[\\/]\.git(?:[\\/]|$)'
    }
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

if (-not $Win32BuildDir) { $Win32BuildDir = Join-Path $RepoRoot "build-vs32" }
if (-not $X64BuildDir) { $X64BuildDir = Join-Path $RepoRoot "build-vs64" }
$MoqiImeSource = Resolve-MoqiImeSource -RepoRoot $RepoRoot -RequestedSource $MoqiImeSource
if (-not $StageDir) { $StageDir = Join-Path $RepoRoot "installer\stage" }
if (-not $IssPath) { $IssPath = Join-Path $RepoRoot "installer\MoqiTsf.iss" }
$Win32BuildDir = [System.IO.Path]::GetFullPath($Win32BuildDir)
$X64BuildDir = [System.IO.Path]::GetFullPath($X64BuildDir)
$StageDir = [System.IO.Path]::GetFullPath($StageDir)
$IssPath = [System.IO.Path]::GetFullPath($IssPath)

$stageWin32Root = Join-Path $StageDir "win32\MoqiIM"
$stageX64Root = Join-Path $StageDir "x64\MoqiIM"
$stageWin32X64Root = Join-Path $stageWin32Root "x64"
New-CleanDirectory -Path $StageDir
New-Item -ItemType Directory -Path $stageWin32Root -Force | Out-Null
New-Item -ItemType Directory -Path $stageX64Root -Force | Out-Null
New-Item -ItemType Directory -Path $stageWin32X64Root -Force | Out-Null

$backends = Join-Path $RepoRoot "backends.json"
if (-not (Test-Path -LiteralPath $backends)) {
    throw "Missing backends.json at $backends"
}
Copy-Item -LiteralPath $backends -Destination (Join-Path $stageWin32Root "backends.json") -Force

$launcher = Resolve-ArtifactPath -Label "MoqiLauncher.exe" -Candidates @(
    (Join-Path $Win32BuildDir "MoqiLauncher.exe"),
    (Join-Path $Win32BuildDir "Release\MoqiLauncher.exe"),
    (Join-Path $Win32BuildDir "MoqLauncher\Release\MoqiLauncher.exe")
)
Copy-IfExists -Source $launcher -Destination (Join-Path $stageWin32Root "MoqiLauncher.exe")

$setupHelper = Resolve-ArtifactPath -Label "SetupHelper.exe" -Candidates @(
    (Join-Path $Win32BuildDir "SetupHelper.exe"),
    (Join-Path $Win32BuildDir "Release\SetupHelper.exe"),
    (Join-Path $Win32BuildDir "SetupHelper\Release\SetupHelper.exe")
)
Copy-IfExists -Source $setupHelper -Destination (Join-Path $stageWin32Root "SetupHelper.exe")

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
Copy-IfExists -Source $dll64 -Destination (Join-Path $stageWin32X64Root "MoqiTextService.dll")

if (-not $SkipMoqiImeCopy) {
    if (-not (Test-Path -LiteralPath $MoqiImeSource)) {
        throw "Moqi IME source not found: $MoqiImeSource (use -MoqiImeSource or -SkipMoqiImeCopy)."
    }
    $imeDest = Join-Path $stageWin32Root "moqi-ime"
    Copy-MoqiImeRuntime -SourceRoot $MoqiImeSource -DestinationRoot $imeDest
}
else {
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
