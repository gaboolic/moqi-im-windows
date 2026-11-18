# Non-installing regression: the rejected switch must not touch an existing stage
# or invoke artifact resolution/the installer builder. No real payload is needed.
$ErrorActionPreference = 'Stop'
$installScript = Join-Path $PSScriptRoot '..\scripts\install.ps1'
$fixture = Join-Path $PSScriptRoot ('.skip-backend-test-' + [guid]::NewGuid().ToString('N'))
try {
    $stage = Join-Path $fixture 'stage'
    $sentinel = Join-Path $stage 'nested\keep.txt'
    New-Item -ItemType Directory -Path (Split-Path -Parent $sentinel) -Force | Out-Null
    Set-Content -LiteralPath $sentinel -Value 'An existing valid stage must survive.' -Encoding UTF8
    $before = (Get-FileHash -LiteralPath $sentinel).Hash
    $failure = ''
    try {
        & $installScript -SkipMoqiImeCopy -StageDir $stage -RepoRoot (Join-Path $fixture 'nonexistent-repo')
    } catch { $failure = $_.Exception.Message }
    if ($failure -notmatch 'complete installer must include the moqi-ime backend' -or
        $failure -notmatch 'existing stage has not been modified') {
        throw "Expected explicit early backend rejection, got: $failure"
    }
    if ((Get-FileHash -LiteralPath $sentinel).Hash -ne $before -or
        @(Get-ChildItem -LiteralPath $stage -Recurse -File).Count -ne 1) {
        throw 'Existing stage was modified by the rejected switch.'
    }
    Write-Host 'PASS -SkipMoqiImeCopy explicitly rejects before touching an existing stage/artifacts'

    $newStage = Join-Path $fixture 'not-created'
    try { & $installScript -SkipMoqiImeCopy -StageDir $newStage } catch { }
    if (Test-Path -LiteralPath $newStage) { throw 'Rejected switch created a stage.' }
    Write-Host 'PASS rejected switch does not create a new stage'
} finally {
    $resolved = [IO.Path]::GetFullPath($fixture)
    if ((Split-Path -Parent $resolved) -ne $PSScriptRoot -or
        (Split-Path -Leaf $resolved) -notlike '.skip-backend-test-*') {
        throw "Refusing unexpected cleanup target: $resolved"
    }
    if (Test-Path -LiteralPath $resolved) { Remove-Item -LiteralPath $resolved -Recurse -Force }
}
