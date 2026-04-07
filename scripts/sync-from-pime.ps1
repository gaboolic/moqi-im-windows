#Requires -Version 5.1
<#
.SYNOPSIS
  Copy PIME launcher/text service sources into moqi-im-windows.

.DESCRIPTION
  Mirrors files from:
    - <PimeRoot>\PIMELauncher    -> <RepoRoot>\MoqLauncher
    - <PimeRoot>\PIMETextService -> <RepoRoot>\MoqiTextService

  During copy:
    - file names replace "PIME" with "Moqi"
    - file contents replace "PIME" with "Moqi"

  The script overwrites mapped target files, but does not delete extra files
  that already exist in the target directories.

.PARAMETER RepoRoot
  Root of moqi-im-windows. Defaults to the parent directory of this script.

.PARAMETER PimeRoot
  Root of the PIME repository. Defaults to RepoRoot\..\PIME.

.PARAMETER DryRun
  Print planned writes without modifying files.
#>
param(
    [string] $RepoRoot = "",
    [string] $PimeRoot = "",
    [switch] $DryRun
)

$ErrorActionPreference = "Stop"

function Resolve-FullPath {
    param([string] $Path)
    return [System.IO.Path]::GetFullPath($Path)
}

function Copy-TextTreeWithReplacement {
    param(
        [string] $SourceDir,
        [string] $TargetDir
    )

    if (-not (Test-Path -LiteralPath $SourceDir)) {
        throw "Source directory not found: $SourceDir"
    }

    if (-not (Test-Path -LiteralPath $TargetDir)) {
        New-Item -ItemType Directory -Path $TargetDir | Out-Null
    }

    $sourceRoot = Resolve-FullPath $SourceDir
    $targetRoot = Resolve-FullPath $TargetDir

    $files = Get-ChildItem -LiteralPath $sourceRoot -Recurse -File | Sort-Object FullName
    foreach ($file in $files) {
        $relativePath = $file.FullName.Substring($sourceRoot.Length).TrimStart('\', '/')
        $mappedRelativePath = $relativePath.Replace("PIME", "Moqi")
        $targetPath = Join-Path $targetRoot $mappedRelativePath
        $targetParent = Split-Path -Parent $targetPath

        if (-not (Test-Path -LiteralPath $targetParent)) {
            if ($DryRun) {
                Write-Host "[DRYRUN] mkdir $targetParent"
            } else {
                New-Item -ItemType Directory -Path $targetParent -Force | Out-Null
            }
        }

        $content = [System.IO.File]::ReadAllText($file.FullName)
        $mappedContent = $content.Replace("PIME", "Moqi")

        if ($DryRun) {
            Write-Host "[DRYRUN] write $targetPath"
        } else {
            [System.IO.File]::WriteAllText($targetPath, $mappedContent, [System.Text.UTF8Encoding]::new($false))
            Write-Host "Wrote $targetPath"
        }
    }
}

$scriptRepoRoot = Resolve-FullPath (Join-Path $PSScriptRoot "..")
if (-not $RepoRoot) {
    $RepoRoot = $scriptRepoRoot
}
if (-not $PimeRoot) {
    $PimeRoot = Join-Path $RepoRoot "..\PIME"
}

$RepoRoot = Resolve-FullPath $RepoRoot
$PimeRoot = Resolve-FullPath $PimeRoot

$jobs = @(
    @{
        Source = Join-Path $PimeRoot "PIMELauncher"
        Target = Join-Path $RepoRoot "MoqLauncher"
    },
    @{
        Source = Join-Path $PimeRoot "PIMETextService"
        Target = Join-Path $RepoRoot "MoqiTextService"
    }
)

foreach ($job in $jobs) {
    Write-Host "== Sync $($job.Source) -> $($job.Target) =="
    Copy-TextTreeWithReplacement -SourceDir $job.Source -TargetDir $job.Target
}

Write-Host "Done."
