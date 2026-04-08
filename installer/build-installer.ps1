#Requires -Version 5.1
<#
.SYNOPSIS
  Compile MoqiTsf.iss with Inno Setup 6 (requires ISCC.exe on PATH or default path).

.PARAMETER StageDir
  Root of the staged installer tree. Expected layout:
    win32\MoqiIM\...
    x64\MoqiIM\...

.PARAMETER IssPath
  Optional path to MoqiTsf.iss (default: installer dir next to this script).

.EXAMPLE
  .\installer\build-installer.ps1 -StageDir D:\moqi-im-windows\installer\stage
#>
param(
    [Parameter(Mandatory = $true)]
    [string] $StageDir,
    [string] $IssPath = ''
)

$ErrorActionPreference = 'Stop'
if (-not (Test-Path -LiteralPath $StageDir)) {
    Write-Error "StageDir not found: $StageDir"
}
$StageDir = (Resolve-Path -LiteralPath $StageDir).Path

$win32Root = Join-Path $StageDir 'win32\MoqiIM'
$x64Root = Join-Path $StageDir 'x64\MoqiIM'
if (-not (Test-Path -LiteralPath $win32Root)) {
    Write-Error "Stage win32 payload not found: $win32Root"
}
if (-not (Test-Path -LiteralPath $x64Root)) {
    Write-Error "Stage x64 payload not found: $x64Root"
}

$requiredPaths = @(
    (Join-Path $win32Root 'MoqiLauncher.exe'),
    (Join-Path $win32Root 'MoqiTextService.dll'),
    (Join-Path $win32Root 'backends.json'),
    (Join-Path $x64Root 'MoqiTextService.dll')
)
foreach ($path in $requiredPaths) {
    if (-not (Test-Path -LiteralPath $path)) {
        Write-Error "Required staged file not found: $path"
    }
}

if ([string]::IsNullOrWhiteSpace($IssPath)) {
    $IssPath = Join-Path $PSScriptRoot 'MoqiTsf.iss'
}
if (-not (Test-Path -LiteralPath $IssPath)) {
    Write-Error "ISS not found: $IssPath"
}

$candidates = @(
    (Join-Path ${env:ProgramFiles(x86)} 'Inno Setup 6\ISCC.exe'),
    (Join-Path $env:ProgramFiles 'Inno Setup 6\ISCC.exe'),
    'ISCC.exe'
)
$iscc = $null
foreach ($c in $candidates) {
    if ($c -eq 'ISCC.exe') {
        $cmd = Get-Command ISCC.exe -ErrorAction SilentlyContinue
        if ($cmd) { $iscc = $cmd.Path; break }
    }
    elseif (Test-Path -LiteralPath $c) {
        $iscc = $c
        break
    }
}
if (-not $iscc) {
    Write-Error @"
Inno Setup 6 compiler (ISCC.exe) not found.
Install: https://jrsoftware.org/isdl.php
Then re-run this script.
"@
}

$argStage = '/DStageDir=' + $StageDir
Write-Host "ISCC: $iscc"
Write-Host "Args: `"$IssPath`" $argStage"
$p = Start-Process -FilePath $iscc -ArgumentList @("`"$IssPath`"", $argStage) -Wait -PassThru -NoNewWindow
if ($p.ExitCode -ne 0) {
    Write-Error "ISCC failed with exit code $($p.ExitCode)"
}

$dist = Join-Path $PSScriptRoot 'dist'
Write-Host "Output: $(Join-Path $dist 'moqi-im-windows-setup.exe')"
exit 0
