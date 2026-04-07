@echo off
setlocal
cd /d "%~dp0"

cmake -S . -B build -G "Visual Studio 17 2022" -A Win32
if errorlevel 1 exit /b 1
cmake --build build --config Release
if errorlevel 1 exit /b 1

cmake -S . -B build64 -G "Visual Studio 17 2022" -A x64
if errorlevel 1 exit /b 1
cmake --build build64 --config Release --target MoqiTextService
if errorlevel 1 exit /b 1

echo OK: Win32 Release ^(full solution^), x64 Release ^(MoqiTextService^).
