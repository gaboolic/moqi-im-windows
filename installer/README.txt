Moqi IM for Windows — graphical installer (Inno Setup 6)
========================================================

Produces:
  - moqi-im-windows-setup.exe — wizard that installs Win32/x64 TSF DLLs plus launcher/backend payload
  - After install: unins000.exe in the install directory — uninstaller

Prerequisites:
  1) A stage directory produced by scripts\install.ps1, containing:
     - win32\MoqiIM\MoqLauncher.exe
     - win32\MoqiIM\MoqiTextService.dll
     - win32\MoqiIM\backends.json
     - optionally win32\MoqiIM\moqi-ime\...
     - x64\MoqiIM\MoqiTextService.dll
  2) Inno Setup 6: https://jrsoftware.org/isdl.php

Build (PowerShell):
  .\scripts\install.ps1
  or
  .\installer\build-installer.ps1 -StageDir C:\path\to\stage

Output:
  installer\dist\moqi-im-windows-setup.exe

Uninstall:
  - Settings -> Apps -> Moqi IM for Windows, or
  - Run unins000.exe in the install folder, or
  - Start menu -> Moqi IM for Windows -> Uninstall

Notes:
  - x64 only installer; it deploys Win32 payload to `%ProgramFiles(x86)%\MoqiIM` and x64 DLL to `%ProgramFiles%\MoqiIM`.
  - Installer requests Administrator for COM registration.
  - Fixed AppId inside `MoqiTsf.iss` should stay stable so Windows recognizes upgrades.
  - IME CLSID in the ISS must stay in sync with `MoqiTextService`.
