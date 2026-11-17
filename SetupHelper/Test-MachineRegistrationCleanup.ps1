# Standalone MSVC test. Does not use CMake, load the IME or touch the registry.
param([string] $VcVarsPath = 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars32.bat')
$ErrorActionPreference = 'Stop'
if (-not (Test-Path -LiteralPath $VcVarsPath)) { throw "Specify -VcVarsPath for MSVC: $VcVarsPath" }
$fixture = Join-Path $PSScriptRoot ('.cleanup-test-' + [guid]::NewGuid().ToString('N'))
try {
    New-Item -ItemType Directory -Path $fixture | Out-Null
    $source = Join-Path $PSScriptRoot 'Test-MachineRegistrationCleanup.cpp'
    $helperSource = Join-Path $PSScriptRoot 'SetupHelper.cpp'
    $exe = Join-Path $fixture 'cleanup-test.exe'
    $obj = Join-Path $fixture 'cleanup-test.obj'
    $batch = Join-Path $fixture 'run.cmd'
    @"
@echo off
call "$VcVarsPath" >nul
if errorlevel 1 exit /b 1
cl /nologo /std:c++17 /EHsc /DUNICODE /D_UNICODE /Zs "$helperSource"
if errorlevel 1 exit /b 1
cl /nologo /std:c++17 /EHsc /W4 "$source" /Fe:"$exe" /Fo:"$obj"
if errorlevel 1 exit /b 1
"$exe"
"@ | Set-Content -LiteralPath $batch -Encoding ASCII
    & $env:ComSpec /d /c $batch
    if ($LASTEXITCODE -ne 0) { throw "Standalone C++ test failed: $LASTEXITCODE" }
} finally {
    $resolved = [IO.Path]::GetFullPath($fixture)
    if ((Split-Path -Parent $resolved) -ne $PSScriptRoot -or
        (Split-Path -Leaf $resolved) -notlike '.cleanup-test-*') {
        throw "Refusing unexpected cleanup target: $resolved"
    }
    if (Test-Path -LiteralPath $resolved) { Remove-Item -LiteralPath $resolved -Recurse -Force }
}
