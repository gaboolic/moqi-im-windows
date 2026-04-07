; Moqi IM for Windows — Inno Setup 6 wizard (x64 only).
; Build: install Inno Setup 6, then run build-installer.ps1 -StageDir <stage root>
; AppId / IME CLSID: keep stable across releases (ARP upgrade path).

#define MyAppName "Moqi IM for Windows"
#define MyAppPublisher "Moqi"
#define MyAppURL "https://github.com/gaboolic/moqi-im-windows"
#define MyAppId "{{C7A6A2D5-16C7-4BE4-8F52-E96D6D6A9E42}"
#define ImeClsid "{{8F204C91-2D7A-4B3E-9E1F-6A5C0D8B2E7F}}"

#ifndef StageDir
  #define StageDir "..\stage"
#endif

[Setup]
AppId={#MyAppId}
AppName={#MyAppName}
AppVersion=1.0.0
AppPublisher={#MyAppPublisher}
AppPublisherURL={#MyAppURL}
AppSupportURL={#MyAppURL}
AppUpdatesURL={#MyAppURL}
DefaultDirName={autopf32}\MoqiIM
DisableProgramGroupPage=yes
PrivilegesRequired=admin
ArchitecturesAllowed=x64
ArchitecturesInstallIn64BitMode=x64
WizardStyle=modern
OutputDir=dist
OutputBaseFilename=moqi-im-windows-setup
Compression=lzma2/max
SolidCompression=yes
WizardSizePercent=110,100
DisableWelcomePage=no

[Languages]
; English only: chocolatey innosetup omits compiler:Languages\*.isl packs.
Name: "english"; MessagesFile: "compiler:Default.isl"

[Files]
Source: "{#StageDir}\win32\MoqiIM\*"; DestDir: "{autopf32}\MoqiIM"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#StageDir}\x64\MoqiIM\*"; DestDir: "{autopf64}\MoqiIM"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{autoprograms}\{#MyAppName}\Uninstall"; Filename: "{uninstallexe}"
Name: "{autoprograms}\{#MyAppName}\Logs"; Filename: "{win}\explorer.exe"; Parameters: """{localappdata}\MoqiIM\Log"""

[Run]
Filename: "{autopf32}\MoqiIM\MoqLauncher.exe"; Description: "Launch MoqLauncher"; Flags: nowait postinstall skipifsilent

[Registry]
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; \
  ValueType: string; ValueName: "MoqLauncher"; \
  ValueData: """{autopf32}\MoqiIM\MoqLauncher.exe"""; \
  Flags: uninsdeletevalue

[InstallDelete]
Type: filesandordirs; Name: "{autopf32}\MoqiIM\moqi-ime"

[UninstallRun]
Filename: "{syswow64}\regsvr32.exe"; \
  Parameters: "/u /s ""{autopf32}\MoqiIM\MoqiTextService.dll"""; \
  WorkingDir: "{autopf32}\MoqiIM"; \
  RunOnceId: "MoqiTsfUnreg32"; \
  Flags: runhidden waituntilterminated; \
  Check: Win32ImeDllExists
Filename: "{sys}\regsvr32.exe"; \
  Parameters: "/u /s ""{autopf64}\MoqiIM\MoqiTextService.dll"""; \
  WorkingDir: "{autopf64}\MoqiIM"; \
  RunOnceId: "MoqiTsfUnreg64"; \
  Flags: runhidden waituntilterminated; \
  Check: X64ImeDllExists

[Code]

function GetWin32ImeDll: string;
begin
  Result := ExpandConstant('{autopf32}\MoqiIM\MoqiTextService.dll');
end;

function GetX64ImeDll: string;
begin
  Result := ExpandConstant('{autopf64}\MoqiIM\MoqiTextService.dll');
end;

function Win32ImeDllExists: Boolean;
begin
  Result := FileExists(GetWin32ImeDll);
end;

function X64ImeDllExists: Boolean;
begin
  Result := FileExists(GetX64ImeDll);
end;

procedure RegPurgeMoqiResiduals;
var
  ClsidKey: String;
  TipKey: String;
begin
  ClsidKey := 'CLSID\{#ImeClsid}';
  TipKey := 'SOFTWARE\Microsoft\CTF\TIP\{#ImeClsid}';
  if RegKeyExists(HKEY_CLASSES_ROOT, ClsidKey) then
    RegDeleteKeyIncludingSubkeys(HKEY_CLASSES_ROOT, ClsidKey);
  if RegKeyExists(HKEY_LOCAL_MACHINE, TipKey) then
    RegDeleteKeyIncludingSubkeys(HKEY_LOCAL_MACHINE, TipKey);
  if RegKeyExists(HKEY_CURRENT_USER, 'Software\Microsoft\CTF\TIP\{#ImeClsid}') then
    RegDeleteKeyIncludingSubkeys(HKEY_CURRENT_USER, 'Software\Microsoft\CTF\TIP\{#ImeClsid}');
  if RegKeyExists(HKEY_CURRENT_USER, 'Software\Classes\CLSID\{#ImeClsid}') then
    RegDeleteKeyIncludingSubkeys(HKEY_CURRENT_USER, 'Software\Classes\CLSID\{#ImeClsid}');
end;

procedure TryKillProcessImage(const ImageName: String);
var
  R: Integer;
begin
  Exec(ExpandConstant('{sys}\taskkill.exe'), '/F /T /IM "' + ImageName + '"',
    '', SW_HIDE, ewWaitUntilTerminated, R);
end;

procedure StopMoqiProcesses;
begin
  TryKillProcessImage('MoqLauncher.exe');
end;

procedure CurStepChanged(CurStep: TSetupStep);
var
  R: Integer;
begin
  if CurStep = ssInstall then
    StopMoqiProcesses;

  if CurStep = ssPostInstall then
  begin
    if Win32ImeDllExists then
      if (not Exec(ExpandConstant('{syswow64}\regsvr32.exe'), '/s "' + GetWin32ImeDll + '"', ExtractFileDir(GetWin32ImeDll), SW_HIDE, ewWaitUntilTerminated, R)) or (R <> 0) then
        RaiseException('Win32 regsvr32 failed (code ' + IntToStr(R) + ').');
    if X64ImeDllExists then
      if (not Exec(ExpandConstant('{sys}\regsvr32.exe'), '/s "' + GetX64ImeDll + '"', ExtractFileDir(GetX64ImeDll), SW_HIDE, ewWaitUntilTerminated, R)) or (R <> 0) then
        RaiseException('x64 regsvr32 failed (code ' + IntToStr(R) + ').');
  end;
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  if CurUninstallStep = usUninstall then
    StopMoqiProcesses;
  if CurUninstallStep = usPostUninstall then
    RegPurgeMoqiResiduals;
end;
