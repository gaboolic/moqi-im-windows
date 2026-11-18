# Pure filesystem fixtures. Never loads the IME DLL, installs, or registers TSF.
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot '..\installer\PayloadValidation.ps1')
$root = Join-Path $PSScriptRoot ('.payload-test-' + [guid]::NewGuid().ToString('N'))
function New-TestPE([string] $Relative, [uint16] $Machine) {
    $path = Join-Path $root $Relative
    New-Item -ItemType Directory -Path (Split-Path -Parent $path) -Force | Out-Null
    $stream = [IO.File]::Create($path)
    $writer = New-Object IO.BinaryWriter($stream)
    try {
        $writer.Write([uint16]0x5a4d)
        $stream.Position = 0x3c
        $writer.Write([int]64)
        $stream.Position = 64
        $writer.Write([uint32]0x4550)
        $writer.Write($Machine)
        $writer.Write((New-Object byte[] 22))
    } finally { $writer.Dispose() }
}
function Expect-Rejection([string] $Name) {
    $failed = $false
    try { Assert-MoqiPayload $root } catch { $failed = $true; Write-Host "PASS rejection: $Name ($($_.Exception.Message))" }
    if (-not $failed) { throw "Expected rejection: $Name" }
}
try {
    New-TestPE 'MoqiTextService.dll' 0x14c
    New-TestPE 'x64\MoqiTextService.dll' 0x8664
    New-TestPE 'SetupHelper.exe' 0x14c
    New-TestPE 'MoqiLauncher.exe' 0x14c
    New-TestPE 'moqi-ime\server.exe' 0x8664
    Set-Content -LiteralPath (Join-Path $root 'backends.json') -Encoding UTF8 -Value '[{"name":"moqi-ime"}]'
    $profileDir = Join-Path $root 'moqi-ime\input_methods\moqi'
    New-Item -ItemType Directory -Path $profileDir -Force | Out-Null
    Expect-Rejection 'zero profiles'
    $profile = '{"name":"Moqi","guid":"{3F6B5A12-8D44-4E71-9A2E-6B4F9C1D2A30}","locale":"zh-Hans-CN","fallbackLocale":"zh-CN","icon":"mo.ico"}'
    $profilePath = Join-Path $profileDir 'ime.json'
    Set-Content -LiteralPath $profilePath -Encoding UTF8 -Value $profile
    Expect-Rejection 'missing icon'
    Set-Content -LiteralPath (Join-Path $profileDir 'mo.ico') -Value 'fixture'
    Assert-MoqiPayload $root
    Write-Host 'PASS complete single-backend payload'
    New-TestPE 'x64\MoqiTextService.dll' 0x14c
    Expect-Rejection 'wrong x64 architecture'
    New-TestPE 'x64\MoqiTextService.dll' 0x8664
    Set-Content -LiteralPath $profilePath -Encoding UTF8 -Value ($profile.Replace('3F6B5A12-8D44-4E71-9A2E-6B4F9C1D2A30', 'not-a-guid'))
    Expect-Rejection 'invalid GUID'
    Set-Content -LiteralPath $profilePath -Encoding UTF8 -Value '{broken'
    Expect-Rejection 'malformed JSON'
    Set-Content -LiteralPath $profilePath -Encoding UTF8 -Value $profile
    $duplicate = Join-Path $root 'moqi-ime\input_methods\duplicate'
    New-Item -ItemType Directory -Path $duplicate -Force | Out-Null
    Copy-Item -LiteralPath $profilePath -Destination (Join-Path $duplicate 'ime.json')
    Copy-Item -LiteralPath (Join-Path $profileDir 'mo.ico') -Destination (Join-Path $duplicate 'mo.ico')
    Expect-Rejection 'duplicate GUID'
    Set-Content -LiteralPath (Join-Path $root 'backends.json') -Encoding UTF8 -Value '[{"name":".."}]'
    Expect-Rejection 'backend traversal'
    Write-Host 'All payload validation tests passed.'
} finally {
    # Only remove the fixture directory created by this invocation.
    $resolved = [IO.Path]::GetFullPath($root)
    if ((Split-Path -Parent $resolved) -ne $PSScriptRoot -or (Split-Path -Leaf $resolved) -notlike '.payload-test-*') {
        throw "Refusing unexpected cleanup target: $resolved"
    }
    if (Test-Path -LiteralPath $resolved) { Remove-Item -LiteralPath $resolved -Recurse -Force }
}
