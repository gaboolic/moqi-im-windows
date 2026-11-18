; Moqi IM for Windows — Inno Setup 6 wizard (x64 only).
; Build: install Inno Setup 6, then run build-installer.ps1 -StageDir <stage root>
; AppId / IME CLSID: keep stable across releases (ARP upgrade path).

#define MyAppName "墨奇输入法"
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
CloseApplications=yes
RestartApplications=no
WizardStyle=modern
OutputDir=dist
OutputBaseFilename=moqi-im-windows-setup
Compression=lzma2/max
SolidCompression=yes
WizardSizePercent=110,100
DisableWelcomePage=no

[Languages]
; Use the vendored translation file so packaging does not depend on local Inno Setup language packs.
Name: "chinesesimplified"; MessagesFile: ".\Inno-Setup-Chinese-Simplified-Translation\ChineseSimplified.isl"

[Files]
Source: "{#StageDir}\win32\MoqiIM\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{autoprograms}\{#MyAppName}\Uninstall"; Filename: "{uninstallexe}"
Name: "{autoprograms}\{#MyAppName}\Logs"; Filename: "{win}\explorer.exe"; Parameters: """{localappdata}\MoqiIM\Log"""

[Registry]
; This runs as the actual interactive user at logon, never the UAC credential
; account or SYSTEM. It retries user enablement if reboot registration raced logon.
Root: HKLM; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; \
  ValueType: string; ValueName: "MoqiUserSetup"; \
  ValueData: """{app}\SetupHelper.exe"" /user /s --appdir ""{app}"""; \
  Flags: uninsdeletevalue
Root: HKLM32; Subkey: "Software\MoqiIM"; Flags: uninsdeletekey
Root: HKLM64; Subkey: "Software\MoqiIM"; Flags: uninsdeletekey

[InstallDelete]
Type: filesandordirs; Name: "{app}\moqi-ime"
Type: filesandordirs; Name: "{app}\x64"

[Code]
const
  SetupHelperExitSuccess = 0;
  SetupHelperExitRestartRequired = 2;

var
  HelperInstallSucceeded: Boolean;
  HelperInstallNeedsRestart: Boolean;
  HelperUninstallNeedsRestart: Boolean;
  HadExistingInstall: Boolean;

function ExistingImeInstallationPresent: Boolean;
begin
  Result :=
    FileExists(ExpandConstant('{app}\MoqiLauncher.exe')) or
    FileExists(ExpandConstant('{syswow64}\MoqiTextService.dll')) or
    FileExists(ExpandConstant('{sys}\MoqiTextService.dll')) or
    RegKeyExists(HKEY_LOCAL_MACHINE, 'SOFTWARE\Microsoft\CTF\TIP\{#ImeClsid}') or
    RegKeyExists(HKEY_CURRENT_USER, 'Software\Microsoft\CTF\TIP\{#ImeClsid}') or
    RegKeyExists(HKEY_CLASSES_ROOT, 'CLSID\{#ImeClsid}') or
    RegKeyExists(HKEY_CURRENT_USER, 'Software\Classes\CLSID\{#ImeClsid}');
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
  TryKillProcessImage('MoqiLauncher.exe');
end;

function GetSetupHelperPath: string;
begin
  Result := ExpandConstant('{app}\SetupHelper.exe');
end;

procedure EnsureSetupHelperExists;
begin
  if not FileExists(GetSetupHelperPath) then
    RaiseException('SetupHelper.exe not found: ' + GetSetupHelperPath);
end;

function RunSetupHelper(const Parameters: string; var ResultCode: Integer): Boolean;
begin
  EnsureSetupHelperExists;
  Result := Exec(GetSetupHelperPath, Parameters, ExpandConstant('{app}'),
    SW_HIDE, ewWaitUntilTerminated, ResultCode);
  if not Result then
    ResultCode := -1;
end;

function BuildInstallSetupHelperParameters(const Action: string): string;
begin
  Result := Action;
  if WizardSilent() then
    Result := Result + ' /s';
  Result := Result + ' --appdir "' + ExpandConstant('{app}') + '"';
end;

function BuildUninstallSetupHelperParameters(const Action: string): string;
begin
  Result := Action;
  if UninstallSilent() then
    Result := Result + ' /s';
  Result := Result + ' --appdir "' + ExpandConstant('{app}') + '"';
end;

procedure HandleSetupHelperResult(const Operation: string; const ResultCode: Integer);
begin
  RaiseException(Operation + ' failed (exit code ' + IntToStr(ResultCode) + ').');
end;

procedure CurStepChanged(CurStep: TSetupStep);
var
  ResultCode: Integer;
begin
  if CurStep = ssInstall then
  begin
    HadExistingInstall := ExistingImeInstallationPresent;
    StopMoqiProcesses;
  end;

  if CurStep = ssPostInstall then
  begin
    if not RunSetupHelper(BuildInstallSetupHelperParameters('/i'), ResultCode) then
      HandleSetupHelperResult('SetupHelper install', ResultCode);

    if ResultCode = SetupHelperExitSuccess then
    begin
      HelperInstallSucceeded := True;
      if HadExistingInstall then
        SuppressibleMsgBox(
          '检测到这是一次覆盖安装。若当前会话里仍有旧的 TSF 实例，墨奇可能要在注销或重启 Windows 后才能立即恢复正常输入。',
          mbInformation, MB_OK, IDOK);
    end
    else if ResultCode = SetupHelperExitRestartRequired then
    begin
      HelperInstallSucceeded := True;
      HelperInstallNeedsRestart := True;
      SuppressibleMsgBox(
        '安装程序已更新应用文件，但 TSF DLL 当前仍被系统占用。' + #13#10#13#10 +
        '请在安装完成后尽快重启 Windows。安装器已安排在系统重启后自动完成 TSF 注册。',
        mbInformation, MB_OK, IDOK);
    end;
    if (ResultCode <> SetupHelperExitSuccess) and
       (ResultCode <> SetupHelperExitRestartRequired) then
      HandleSetupHelperResult('SetupHelper install', ResultCode);
    if not HelperInstallNeedsRestart then
    begin
      if not ExecAsOriginalUser(GetSetupHelperPath,
        BuildInstallSetupHelperParameters('/user'), ExpandConstant('{app}'),
        SW_HIDE, ewWaitUntilTerminated, ResultCode) then
        HandleSetupHelperResult('Interactive-user setup', -1);
      if ResultCode = 4 then
        SuppressibleMsgBox(
          '机器注册已完成。安装程序由提升身份直接启动，无法安全启用其他用户的输入法。请注销并重新登录目标用户；登录时将自动完成用户启用。',
          mbInformation, MB_OK, IDOK)
      else if ResultCode <> SetupHelperExitSuccess then
        HandleSetupHelperResult('Interactive-user profile enable/verification', ResultCode);
    end;
  end;
end;

function NeedRestart(): Boolean;
begin
  Result := HelperInstallNeedsRestart;
end;

function ShouldLaunchLauncher(): Boolean;
begin
  Result := HelperInstallSucceeded and (not HelperInstallNeedsRestart);
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var
  ResultCode: Integer;
begin
  if CurUninstallStep = usUninstall then
  begin
    StopMoqiProcesses;
    if not RunSetupHelper(BuildUninstallSetupHelperParameters('/u'), ResultCode) then
      HandleSetupHelperResult('SetupHelper uninstall', ResultCode);
    if ResultCode = SetupHelperExitRestartRequired then
      HelperUninstallNeedsRestart := True
    else if ResultCode <> SetupHelperExitSuccess then
      HandleSetupHelperResult('SetupHelper uninstall', ResultCode);
  end;
  if CurUninstallStep = usPostUninstall then
  begin
    { The helper now reports checked machine-unregistration results. Do not
      silently purge HKCR/HKCU belonging to the elevated credential account. }
    if HelperUninstallNeedsRestart then
      SuppressibleMsgBox(
        '部分 TSF DLL 已安排在系统重启后删除。请尽快重启 Windows，以完成卸载清理。',
        mbInformation, MB_OK, IDOK);
  end;
end;
