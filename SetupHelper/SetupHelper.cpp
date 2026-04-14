#include <windows.h>

#include <shellapi.h>

#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

constexpr int kExitSuccess = 0;
constexpr int kExitFailure = 1;
constexpr int kExitRestartRequired = 2;
constexpr int kExitInvalidArgs = 3;
constexpr wchar_t kProgramDirEnvVar[] = L"MOQI_PROGRAM_DIR";
constexpr wchar_t kReregisterTaskName[] = L"MoqiIM-ReRegisterTSF";

enum class Action {
  kHelp,
  kInstall,
  kReregister,
  kUninstall,
};

struct Options {
  Action action = Action::kHelp;
  bool silent = false;
  std::wstring app_dir;
};

std::wstring Quote(const std::wstring& value) {
  return L"\"" + value + L"\"";
}

std::wstring GetModulePath() {
  std::wstring path(MAX_PATH, L'\0');
  while (true) {
    const DWORD written = GetModuleFileNameW(nullptr, path.data(),
                                             static_cast<DWORD>(path.size()));
    if (written == 0) {
      return L"";
    }
    if (written < path.size() - 1) {
      path.resize(written);
      return path;
    }
    path.resize(path.size() * 2);
  }
}

std::wstring GetModuleDirectory() {
  const fs::path module_path(GetModulePath());
  return module_path.parent_path().wstring();
}

std::wstring JoinArguments(const std::vector<std::wstring>& args,
                           const size_t start_index) {
  std::wstring result;
  for (size_t i = start_index; i < args.size(); ++i) {
    if (!result.empty()) {
      result += L' ';
    }
    result += Quote(args[i]);
  }
  return result;
}

void ShowMessage(const std::wstring& text,
                 const std::wstring& caption,
                 const UINT flags,
                 const bool silent) {
  if (!silent) {
    MessageBoxW(nullptr, text.c_str(), caption.c_str(), flags);
  }
}

std::vector<std::wstring> GetCommandLineArguments() {
  int argc = 0;
  LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
  if (argv == nullptr) {
    return {};
  }

  std::vector<std::wstring> args;
  args.reserve(argc);
  for (int i = 0; i < argc; ++i) {
    args.emplace_back(argv[i]);
  }
  LocalFree(argv);
  return args;
}

bool IsRunningAsAdmin() {
  BOOL is_admin = FALSE;
  SID_IDENTIFIER_AUTHORITY authority = SECURITY_NT_AUTHORITY;
  PSID admin_group = nullptr;
  if (!AllocateAndInitializeSid(&authority, 2, SECURITY_BUILTIN_DOMAIN_RID,
                                DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0, 0,
                                &admin_group)) {
    return false;
  }

  if (!CheckTokenMembership(nullptr, admin_group, &is_admin)) {
    is_admin = FALSE;
  }
  FreeSid(admin_group);
  return is_admin == TRUE;
}

int RestartElevated(const std::vector<std::wstring>& args, const bool silent) {
  SHELLEXECUTEINFOW exec_info = {};
  exec_info.cbSize = sizeof(exec_info);
  exec_info.fMask = SEE_MASK_NOCLOSEPROCESS;
  exec_info.lpVerb = L"runas";
  const std::wstring module_path = GetModulePath();
  const std::wstring parameters = JoinArguments(args, 1);
  exec_info.lpFile = module_path.c_str();
  exec_info.lpParameters = parameters.empty() ? nullptr : parameters.c_str();
  exec_info.nShow = silent ? SW_HIDE : SW_SHOWNORMAL;

  if (!ShellExecuteExW(&exec_info)) {
    return kExitFailure;
  }

  WaitForSingleObject(exec_info.hProcess, INFINITE);
  DWORD exit_code = kExitFailure;
  if (!GetExitCodeProcess(exec_info.hProcess, &exit_code)) {
    exit_code = kExitFailure;
  }
  CloseHandle(exec_info.hProcess);
  return static_cast<int>(exit_code);
}

std::wstring GetWindowsDirectoryPath() {
  std::wstring path(MAX_PATH, L'\0');
  while (true) {
    const UINT written =
        GetWindowsDirectoryW(path.data(), static_cast<UINT>(path.size()));
    if (written == 0) {
      return L"";
    }
    if (written < path.size()) {
      path.resize(written);
      return path;
    }
    path.resize(written + 1);
  }
}

std::wstring GetSyswow64DirectoryPath() {
  std::wstring path(MAX_PATH, L'\0');
  const UINT written =
      GetSystemWow64DirectoryW(path.data(), static_cast<UINT>(path.size()));
  if (written > 0 && written < path.size()) {
    path.resize(written);
    return path;
  }

  const UINT fallback =
      GetSystemDirectoryW(path.data(), static_cast<UINT>(path.size()));
  if (fallback == 0) {
    return L"";
  }
  path.resize(fallback);
  return path;
}

std::wstring GetNativeSystemDirectoryPath() {
  const fs::path sysnative =
      fs::path(GetWindowsDirectoryPath()) / L"Sysnative";
  if (fs::exists(sysnative)) {
    return sysnative.wstring();
  }

  std::wstring path(MAX_PATH, L'\0');
  const UINT written =
      GetSystemDirectoryW(path.data(), static_cast<UINT>(path.size()));
  if (written == 0) {
    return L"";
  }
  path.resize(written);
  return path;
}

std::wstring GetNativeSystemDirectoryForChildProcess() {
  return (fs::path(GetWindowsDirectoryPath()) / L"System32").wstring();
}

std::wstring NormalizePathForPendingOperation(const std::wstring& path) {
  const std::wstring sysnative_prefix =
      fs::path(GetWindowsDirectoryPath() + L"\\Sysnative").wstring() + L"\\";
  if (_wcsnicmp(path.c_str(), sysnative_prefix.c_str(),
                sysnative_prefix.length()) == 0) {
    return (fs::path(GetWindowsDirectoryPath()) / L"System32" /
            path.substr(sysnative_prefix.length()))
        .wstring();
  }
  return path;
}

bool RunProcess(const std::wstring& application_path,
                std::wstring command,
                const std::wstring& working_dir,
                DWORD* exit_code) {
  STARTUPINFOW startup_info = {};
  startup_info.cb = sizeof(startup_info);
  startup_info.dwFlags = STARTF_USESHOWWINDOW;
  startup_info.wShowWindow = SW_HIDE;
  PROCESS_INFORMATION process_info = {};

  const BOOL created =
      CreateProcessW(application_path.c_str(), command.data(), nullptr, nullptr,
                     FALSE, 0, nullptr,
                     working_dir.empty() ? nullptr : working_dir.c_str(),
                     &startup_info, &process_info);
  if (!created) {
    return false;
  }

  WaitForSingleObject(process_info.hProcess, INFINITE);
  DWORD process_exit_code = 0;
  const BOOL got_exit_code =
      GetExitCodeProcess(process_info.hProcess, &process_exit_code);
  CloseHandle(process_info.hThread);
  CloseHandle(process_info.hProcess);
  if (exit_code != nullptr) {
    *exit_code = got_exit_code ? process_exit_code : static_cast<DWORD>(-1);
  }
  return got_exit_code == TRUE;
}

bool RunRegsvr(const fs::path& regsvr_path,
               const fs::path& dll_path_for_process,
               const fs::path& program_dir,
               const bool unregister) {
  if (!fs::exists(dll_path_for_process)) {
    return true;
  }

  std::wstring command = Quote(regsvr_path.wstring());
  if (unregister) {
    command += L" /u";
  }
  command += L" /s " + Quote(dll_path_for_process.wstring());

  std::wstring mutable_command = command;
  std::wstring working_dir = dll_path_for_process.parent_path().wstring();
  std::wstring previous_program_dir;
  const DWORD previous_len = GetEnvironmentVariableW(kProgramDirEnvVar, nullptr, 0);
  if (previous_len > 0) {
    previous_program_dir.resize(previous_len - 1);
    GetEnvironmentVariableW(kProgramDirEnvVar, previous_program_dir.data(), previous_len);
  }
  SetEnvironmentVariableW(kProgramDirEnvVar, program_dir.c_str());

  DWORD exit_code = 0;
  const bool ran =
      RunProcess(regsvr_path.wstring(), mutable_command, working_dir, &exit_code);
  if (previous_len > 0) {
    SetEnvironmentVariableW(kProgramDirEnvVar, previous_program_dir.c_str());
  } else {
    SetEnvironmentVariableW(kProgramDirEnvVar, nullptr);
  }
  if (!ran) {
    return false;
  }
  return exit_code == 0;
}

fs::path BuildOldPath(const fs::path& destination) {
  for (int i = 0; i < 16; ++i) {
    fs::path old_path = destination;
    old_path += L".old." + std::to_wstring(i);
    if (!fs::exists(old_path)) {
      return old_path;
    }
  }
  fs::path old_path = destination;
  old_path += L".old";
  return old_path;
}

void CleanupStalePendingFiles(const fs::path& destination) {
  std::error_code ec;
  const fs::path directory = destination.parent_path();
  if (!fs::exists(directory, ec)) {
    return;
  }

  const std::wstring prefix = destination.filename().wstring() + L".pending.";
  for (const auto& entry : fs::directory_iterator(directory, ec)) {
    if (ec || !entry.is_regular_file(ec)) {
      continue;
    }
    const std::wstring name = entry.path().filename().wstring();
    if (name.rfind(prefix, 0) == 0) {
      fs::remove(entry.path(), ec);
      ec.clear();
    }
  }
}

bool RenameFileForDeleteOnReboot(const fs::path& path, bool& reboot_required) {
  if (!fs::exists(path)) {
    return true;
  }

  const fs::path old_path = BuildOldPath(path);
  if (MoveFileExW(path.c_str(), old_path.c_str(), MOVEFILE_REPLACE_EXISTING) ==
      TRUE) {
    const std::wstring pending_delete_path =
        NormalizePathForPendingOperation(old_path.wstring());
    if (MoveFileExW(pending_delete_path.c_str(), nullptr,
                    MOVEFILE_DELAY_UNTIL_REBOOT) ==
        TRUE) {
      reboot_required = true;
      return true;
    }
    MoveFileExW(old_path.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING);
    return false;
  }
  return false;
}

void CleanupStaleOldFiles(const fs::path& destination) {
  std::error_code ec;
  const fs::path directory = destination.parent_path();
  if (!fs::exists(directory, ec)) {
    return;
  }

  const std::wstring prefix = destination.filename().wstring() + L".old";
  for (const auto& entry : fs::directory_iterator(directory, ec)) {
    if (ec || !entry.is_regular_file(ec)) {
      continue;
    }
    const std::wstring name = entry.path().filename().wstring();
    if (name.rfind(prefix, 0) == 0) {
      fs::remove(entry.path(), ec);
      ec.clear();
    }
  }
}

bool DeleteReregisterTask() {
  const fs::path schtasks =
      fs::path(GetNativeSystemDirectoryForChildProcess()) / L"schtasks.exe";
  std::wstring command = Quote(schtasks.wstring()) + L" /Delete /TN " +
                         Quote(kReregisterTaskName) + L" /F";
  DWORD exit_code = 0;
  if (!RunProcess(schtasks.wstring(), command, GetModuleDirectory(), &exit_code)) {
    return false;
  }
  return exit_code == 0 || exit_code == 1;
}

bool ScheduleReregisterTask(const Options& options) {
  const fs::path schtasks =
      fs::path(GetNativeSystemDirectoryForChildProcess()) / L"schtasks.exe";
  const std::wstring task_command =
      Quote(GetModulePath()) + L" /r /s --appdir " + Quote(options.app_dir);
  std::wstring command = Quote(schtasks.wstring()) + L" /Create /TN " +
                         Quote(kReregisterTaskName) +
                         L" /SC ONSTART /RU SYSTEM /RL HIGHEST /TR " +
                         Quote(task_command) + L" /F";
  DWORD exit_code = 0;
  return RunProcess(schtasks.wstring(), command, GetModuleDirectory(),
                    &exit_code) &&
         exit_code == 0;
}

bool DeleteFileWithFallback(const fs::path& path, bool& reboot_required) {
  if (!fs::exists(path)) {
    return true;
  }
  if (DeleteFileW(path.c_str()) == TRUE) {
    return true;
  }
  return RenameFileForDeleteOnReboot(path, reboot_required);
}

bool CopyFileWithFallback(const fs::path& source,
                          const fs::path& destination,
                          bool& reboot_required) {
  if (!fs::exists(source)) {
    return false;
  }
  CleanupStalePendingFiles(destination);
  if (CopyFileW(source.c_str(), destination.c_str(), FALSE) == TRUE) {
    return true;
  }
  if (RenameFileForDeleteOnReboot(destination, reboot_required)) {
    const bool copied = CopyFileW(source.c_str(), destination.c_str(), FALSE) == TRUE;
    if (copied) {
      CleanupStalePendingFiles(destination);
    }
    return copied;
  }
  return false;
}

int ShowFailureAndReturn(const std::wstring& message, const bool silent) {
  ShowMessage(message, L"SetupHelper", MB_ICONERROR | MB_OK, silent);
  return kExitFailure;
}

int RunReregister(const Options& options) {
  const fs::path app_dir(options.app_dir);
  const fs::path dest32 = fs::path(GetSyswow64DirectoryPath()) / L"MoqiTextService.dll";
  const fs::path dest64 = fs::path(GetNativeSystemDirectoryPath()) / L"MoqiTextService.dll";
  const fs::path dest64_for_regsvr =
      fs::path(GetNativeSystemDirectoryForChildProcess()) / L"MoqiTextService.dll";
  const fs::path regsvr32 = fs::path(GetSyswow64DirectoryPath()) / L"regsvr32.exe";
  const fs::path regsvr64 = fs::path(GetNativeSystemDirectoryPath()) / L"regsvr32.exe";

  CleanupStaleOldFiles(dest32);
  CleanupStaleOldFiles(dest64);

  if (!RunRegsvr(regsvr32, dest32, app_dir, false)) {
    return ShowFailureAndReturn(L"Failed to register Win32 TSF DLL.",
                                options.silent);
  }
  if (!RunRegsvr(regsvr64, dest64_for_regsvr, app_dir, false)) {
    return ShowFailureAndReturn(L"Failed to register x64 TSF DLL.",
                                options.silent);
  }
  DeleteReregisterTask();
  return kExitSuccess;
}

int RunInstall(const Options& options) {
  const fs::path app_dir(options.app_dir);
  const fs::path source32 = app_dir / L"MoqiTextService.dll";
  const fs::path source64 = app_dir / L"x64" / L"MoqiTextService.dll";
  // TSF DLLs must live in system directories, or IME input will not work in
  // some games such as CS2.
  const fs::path dest32 = fs::path(GetSyswow64DirectoryPath()) / L"MoqiTextService.dll";
  const fs::path dest64 = fs::path(GetNativeSystemDirectoryPath()) / L"MoqiTextService.dll";
  const fs::path dest64_for_regsvr =
      fs::path(GetNativeSystemDirectoryForChildProcess()) / L"MoqiTextService.dll";
  const fs::path regsvr32 = fs::path(GetSyswow64DirectoryPath()) / L"regsvr32.exe";
  const fs::path regsvr64 = fs::path(GetNativeSystemDirectoryPath()) / L"regsvr32.exe";

  if (!fs::exists(source32)) {
    return ShowFailureAndReturn(L"Missing Win32 payload: " + source32.wstring(),
                                options.silent);
  }
  if (!fs::exists(source64)) {
    return ShowFailureAndReturn(L"Missing x64 payload: " + source64.wstring(),
                                options.silent);
  }

  DeleteReregisterTask();
  RunRegsvr(regsvr32, dest32, app_dir, true);
  RunRegsvr(regsvr64, dest64_for_regsvr, app_dir, true);

  bool reboot_required = false;
  if (!CopyFileWithFallback(source32, dest32, reboot_required)) {
    return ShowFailureAndReturn(
        L"Failed to update Win32 TSF DLL in " + dest32.wstring(), options.silent);
  }
  if (!CopyFileWithFallback(source64, dest64, reboot_required)) {
    return ShowFailureAndReturn(
        L"Failed to update x64 TSF DLL in " + dest64.wstring(), options.silent);
  }

  if (reboot_required) {
    if (!ScheduleReregisterTask(options)) {
      return ShowFailureAndReturn(
          L"Failed to schedule TSF re-registration after reboot.",
          options.silent);
    }
    return kExitRestartRequired;
  }

  if (!RunRegsvr(regsvr32, dest32, app_dir, false)) {
    return ShowFailureAndReturn(L"Failed to register Win32 TSF DLL.",
                                options.silent);
  }
  if (!RunRegsvr(regsvr64, dest64_for_regsvr, app_dir, false)) {
    return ShowFailureAndReturn(L"Failed to register x64 TSF DLL.",
                                options.silent);
  }
  return kExitSuccess;
}

int RunUninstall(const Options& options) {
  const fs::path app_dir(options.app_dir);
  const fs::path dest32 = fs::path(GetSyswow64DirectoryPath()) / L"MoqiTextService.dll";
  const fs::path dest64 = fs::path(GetNativeSystemDirectoryPath()) / L"MoqiTextService.dll";
  const fs::path dest64_for_regsvr =
      fs::path(GetNativeSystemDirectoryForChildProcess()) / L"MoqiTextService.dll";
  const fs::path regsvr32 = fs::path(GetSyswow64DirectoryPath()) / L"regsvr32.exe";
  const fs::path regsvr64 = fs::path(GetNativeSystemDirectoryPath()) / L"regsvr32.exe";

  DeleteReregisterTask();
  RunRegsvr(regsvr32, dest32, app_dir, true);
  RunRegsvr(regsvr64, dest64_for_regsvr, app_dir, true);

  bool reboot_required = false;
  if (!DeleteFileWithFallback(dest32, reboot_required)) {
    return ShowFailureAndReturn(
        L"Failed to remove Win32 TSF DLL from " + dest32.wstring(), options.silent);
  }
  if (!DeleteFileWithFallback(dest64, reboot_required)) {
    return ShowFailureAndReturn(
        L"Failed to remove x64 TSF DLL from " + dest64.wstring(), options.silent);
  }
  return reboot_required ? kExitRestartRequired : kExitSuccess;
}

void ShowUsage() {
  const std::wstring help_text =
      L"Usage: SetupHelper.exe /i|/r|/u [/s] [--appdir <path>]\n"
      L"  /i       Install or upgrade the TSF DLLs.\n"
      L"  /r       Register the TSF DLLs after a reboot.\n"
      L"  /u       Uninstall the TSF DLLs.\n"
      L"  /s       Silent mode.\n"
      L"  --appdir Explicit application directory.\n";
  MessageBoxW(nullptr, help_text.c_str(), L"SetupHelper",
              MB_ICONINFORMATION | MB_OK);
}

bool ParseOptions(const std::vector<std::wstring>& args,
                  Options& options,
                  std::wstring& error) {
  options.app_dir = GetModuleDirectory();

  for (size_t i = 1; i < args.size(); ++i) {
    const std::wstring& arg = args[i];
    if (arg == L"/i") {
      if (options.action != Action::kHelp) {
        error = L"Only one action may be specified.";
        return false;
      }
      options.action = Action::kInstall;
    } else if (arg == L"/r") {
      if (options.action != Action::kHelp) {
        error = L"Only one action may be specified.";
        return false;
      }
      options.action = Action::kReregister;
    } else if (arg == L"/u") {
      if (options.action != Action::kHelp) {
        error = L"Only one action may be specified.";
        return false;
      }
      options.action = Action::kUninstall;
    } else if (arg == L"/s") {
      options.silent = true;
    } else if (arg == L"/?" || arg == L"/help" || arg == L"--help") {
      options.action = Action::kHelp;
    } else if (arg == L"--appdir") {
      if (i + 1 >= args.size()) {
        error = L"--appdir requires a path.";
        return false;
      }
      options.app_dir = args[++i];
    } else if (arg.rfind(L"--appdir=", 0) == 0) {
      options.app_dir = arg.substr(9);
    } else {
      error = L"Unknown argument: " + arg;
      return false;
    }
  }

  if (options.action == Action::kHelp && args.size() > 1 &&
      options.app_dir == GetModuleDirectory()) {
    error = L"No action specified.";
    return false;
  }
  return true;
}

}  // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
  const std::vector<std::wstring> args = GetCommandLineArguments();
  Options options;
  std::wstring error;
  if (!ParseOptions(args, options, error)) {
    ShowMessage(error, L"SetupHelper", MB_ICONERROR | MB_OK, false);
    ShowUsage();
    return kExitInvalidArgs;
  }

  if (options.action == Action::kHelp) {
    ShowUsage();
    return kExitSuccess;
  }

  if (!IsRunningAsAdmin()) {
    return RestartElevated(args, options.silent);
  }

  if (options.action == Action::kInstall) {
    return RunInstall(options);
  }
  if (options.action == Action::kReregister) {
    return RunReregister(options);
  }
  return RunUninstall(options);
}
