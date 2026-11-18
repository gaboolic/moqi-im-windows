#include <windows.h>

#include <shellapi.h>
#include <taskschd.h>

#include "MachineRegistrationCleanup.h"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <algorithm>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

constexpr int kExitSuccess = 0;
constexpr int kExitFailure = 1;
constexpr int kExitRestartRequired = 2;
constexpr int kExitInvalidArgs = 3;
constexpr int kExitUserDeferred = 4;
constexpr wchar_t kProgramDirEnvVar[] = L"MOQI_PROGRAM_DIR";
constexpr wchar_t kReregisterTaskName[] = L"MoqiIM-ReRegisterTSF";
constexpr wchar_t kLauncherAutostartTaskName[] = L"MoqiIM-LauncherAutoStart";

enum class Action {
  kHelp,
  kInstall,
  kReregister,
  kUninstall,
  kUser,
  kValidate,
};

struct Options {
  Action action = Action::kHelp;
  bool silent = false;
  std::wstring app_dir;
};

bool g_userSetupLog = false;
void AppendSetupLog(const std::wstring& message) {
  wchar_t base[MAX_PATH] = {};
  DWORD length = GetEnvironmentVariableW(g_userSetupLog ? L"LOCALAPPDATA" : L"ProgramData", base, MAX_PATH);
  if (!length || length >= MAX_PATH) return;
  fs::path directory = fs::path(base) / L"MoqiIM" / L"Log";
  std::error_code ec;
  fs::create_directories(directory, ec);
  SYSTEMTIME now = {};
  GetLocalTime(&now);
  const std::wstring line = std::to_wstring(now.wYear) + L"-" + std::to_wstring(now.wMonth) + L"-" +
      std::to_wstring(now.wDay) + L" " + std::to_wstring(now.wHour) + L":" + std::to_wstring(now.wMinute) + L":" +
      std::to_wstring(now.wSecond) + L" pid=" + std::to_wstring(GetCurrentProcessId()) + L" " + message + L"\r\n";
  int bytes = WideCharToMultiByte(CP_UTF8, 0, line.c_str(), static_cast<int>(line.size()), nullptr, 0, nullptr, nullptr);
  std::string utf8(bytes, '\0');
  WideCharToMultiByte(CP_UTF8, 0, line.c_str(), static_cast<int>(line.size()), utf8.data(), bytes, nullptr, nullptr);
  HANDLE file = CreateFileW((directory / (g_userSetupLog ? L"setup-user.log" : L"setup-machine.log")).c_str(),
      FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file != INVALID_HANDLE_VALUE) {
    DWORD written = 0;
    WriteFile(file, utf8.data(), static_cast<DWORD>(utf8.size()), &written, nullptr);
    CloseHandle(file);
  }
}

bool NeedsCommandLineQuoting(const std::wstring& value) {
  if (value.empty()) {
    return true;
  }
  for (const wchar_t ch : value) {
    if (ch == L' ' || ch == L'\t' || ch == L'"') {
      return true;
    }
  }
  return false;
}

std::wstring QuoteCommandLineArgument(const std::wstring& value) {
  if (!NeedsCommandLineQuoting(value)) {
    return value;
  }

  std::wstring quoted;
  quoted.push_back(L'"');
  size_t backslash_count = 0;
  for (const wchar_t ch : value) {
    if (ch == L'\\') {
      ++backslash_count;
      continue;
    }

    if (ch == L'"') {
      quoted.append(backslash_count * 2 + 1, L'\\');
      quoted.push_back(L'"');
      backslash_count = 0;
      continue;
    }

    quoted.append(backslash_count, L'\\');
    backslash_count = 0;
    quoted.push_back(ch);
  }

  quoted.append(backslash_count * 2, L'\\');
  quoted.push_back(L'"');
  return quoted;
}

std::wstring FormatWindowsErrorMessage(const DWORD error_code) {
  if (error_code == 0) {
    return L"Win32 error 0";
  }

  LPWSTR buffer = nullptr;
  const DWORD flags = FORMAT_MESSAGE_ALLOCATE_BUFFER |
                      FORMAT_MESSAGE_FROM_SYSTEM |
                      FORMAT_MESSAGE_IGNORE_INSERTS;
  const DWORD length = FormatMessageW(flags, nullptr, error_code, 0,
                                      reinterpret_cast<LPWSTR>(&buffer), 0,
                                      nullptr);
  std::wstring message = L"Win32 error " + std::to_wstring(error_code);
  if (length > 0 && buffer != nullptr) {
    DWORD trimmed_length = length;
    while (trimmed_length > 0 &&
           (buffer[trimmed_length - 1] == L'\r' ||
            buffer[trimmed_length - 1] == L'\n')) {
      buffer[trimmed_length - 1] = L'\0';
      --trimmed_length;
    }
    if (*buffer != L'\0') {
      message += L": ";
      message += buffer;
    }
  }
  if (buffer != nullptr) {
    LocalFree(buffer);
  }
  return message;
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
    result += QuoteCommandLineArgument(args[i]);
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
                DWORD* exit_code,
                DWORD* error_code = nullptr) {
  if (error_code != nullptr) {
    *error_code = 0;
  }
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
    if (error_code != nullptr) {
      *error_code = GetLastError();
    }
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
  if (!got_exit_code && error_code != nullptr) {
    *error_code = GetLastError();
  }
  return got_exit_code == TRUE;
}

bool IsMissingRegistryKey(const LSTATUS error) {
  return error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND;
}

// No merged HKCR view, user hive, TSF COM API or regsvr fallback is used when
// the DLL is missing. Every operation is scoped to HKLM + an explicit view.
class MachineRegistrationRegistry {
 public:
  explicit MachineRegistrationRegistry(const REGSAM view) : view_(view) {}

  bool Children(const std::wstring& path, std::vector<std::wstring>& names) {
    HKEY key = nullptr;
    LSTATUS error = RegOpenKeyExW(HKEY_LOCAL_MACHINE, path.c_str(), 0,
                                  KEY_ENUMERATE_SUB_KEYS | view_, &key);
    if (IsMissingRegistryKey(error)) return true;
    if (error != ERROR_SUCCESS) return Fail(path, error);
    for (DWORD index = 0;; ++index) {
      wchar_t name[256] = {};  // Registry key components are at most 255 chars.
      DWORD length = static_cast<DWORD>(std::size(name));
      error = RegEnumKeyExW(key, index, name, &length, nullptr, nullptr, nullptr, nullptr);
      if (error == ERROR_NO_MORE_ITEMS) break;
      if (error != ERROR_SUCCESS) {
        RegCloseKey(key);
        return Fail(path, error);
      }
      names.emplace_back(name, length);
    }
    error = RegCloseKey(key);
    return error == ERROR_SUCCESS || Fail(path, error);
  }

  bool Delete(const std::wstring& path) {
    const auto separator = path.rfind(L'\\');
    if (separator == std::wstring::npos) return Fail(path, ERROR_INVALID_PARAMETER);
    const auto parent_path = path.substr(0, separator);
    const auto leaf = path.substr(separator + 1);
    HKEY parent = nullptr;
    LSTATUS error = RegOpenKeyExW(HKEY_LOCAL_MACHINE, parent_path.c_str(), 0,
        KEY_READ | KEY_WRITE | DELETE | view_, &parent);
    if (IsMissingRegistryKey(error)) return true;
    if (error != ERROR_SUCCESS) return Fail(path, error);
    error = RegDeleteTreeW(parent, leaf.c_str());
    if (error != ERROR_SUCCESS && !IsMissingRegistryKey(error)) {
      RegCloseKey(parent);
      return Fail(path, error);
    }
    // Do not report success if a failed/racing cleanup left or recreated a key.
    HKEY remaining = nullptr;
    error = RegOpenKeyExW(parent, leaf.c_str(), 0, KEY_READ | view_, &remaining);
    if (remaining) RegCloseKey(remaining);
    const LSTATUS closed = RegCloseKey(parent);
    if (!IsMissingRegistryKey(error))
      return Fail(path, error == ERROR_SUCCESS ? ERROR_ALREADY_EXISTS : error);
    return closed == ERROR_SUCCESS || Fail(path, closed);
  }

 private:
  bool Fail(const std::wstring& path, const LSTATUS error) const {
    AppendSetupLog(L"Machine cleanup HKLM" + std::wstring(view_ == KEY_WOW64_32KEY ? L"32\\" : L"64\\") +
        path + L" failed: " + FormatWindowsErrorMessage(error));
    return false;
  }
  REGSAM view_;
};

bool RunRegsvr(const fs::path& regsvr_path,
               const fs::path& dll_path_for_process,
               const fs::path& program_dir,
               const bool unregister,
               const REGSAM machine_view = 0,
               const fs::path& dll_path_on_disk = {}) {
  // A 32-bit helper must inspect Sysnative for the native DLL, while the native
  // regsvr child must receive System32. Do not mistake a redirected path for it.
  const auto& disk_path = dll_path_on_disk.empty() ? dll_path_for_process : dll_path_on_disk;
  std::error_code error;
  const auto status = fs::status(disk_path, error);
  if (error && error != std::errc::no_such_file_or_directory) {
    AppendSetupLog(L"Cannot inspect DLL " + disk_path.wstring() + L": " + std::to_wstring(error.value()));
    return false;
  }
  if (!fs::is_regular_file(status)) {
    if (unregister && !fs::exists(status) &&
        (machine_view == KEY_WOW64_32KEY || machine_view == KEY_WOW64_64KEY)) {
      AppendSetupLog(L"DLL missing; performing checked machine-only cleanup: " + disk_path.wstring());
      MachineRegistrationRegistry registry(machine_view);
      return moqi_setup::CleanupMachineRegistration(registry);
    }
    AppendSetupLog(L"Missing or non-regular DLL: " + disk_path.wstring());
    return false;
  }

  std::wstring command = QuoteCommandLineArgument(regsvr_path.wstring());
  if (unregister) {
    command += L" /u";
  }
  command += L" /s " + QuoteCommandLineArgument(dll_path_for_process.wstring());

  std::wstring mutable_command = command;
  std::wstring working_dir = dll_path_for_process.parent_path().wstring();
  std::wstring previous_program_dir;
  const DWORD previous_len = GetEnvironmentVariableW(kProgramDirEnvVar, nullptr, 0);
  if (previous_len > 0) {
    previous_program_dir.resize(previous_len - 1);
    GetEnvironmentVariableW(kProgramDirEnvVar, previous_program_dir.data(), previous_len);
  }
  if (!SetEnvironmentVariableW(kProgramDirEnvVar, program_dir.c_str())) return false;

  DWORD exit_code = 0;
  const bool ran =
      RunProcess(regsvr_path.wstring(), mutable_command, working_dir, &exit_code);
  if (previous_len > 0) {
    SetEnvironmentVariableW(kProgramDirEnvVar, previous_program_dir.c_str());
  } else {
    SetEnvironmentVariableW(kProgramDirEnvVar, nullptr);
  }
  if (!ran || exit_code != 0) {
    AppendSetupLog(L"regsvr " + dll_path_for_process.wstring() + L" failed: exit=" +
        std::to_wstring(exit_code) + L" Win32=" + std::to_wstring(GetLastError()));
    return false;
  }
  return true;
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

fs::path BuildPendingRebootPath(const fs::path& source) {
  for (int i = 0; i < 16; ++i) {
    fs::path pending_path = source;
    pending_path += L".pending.reboot." + std::to_wstring(i);
    if (!fs::exists(pending_path)) {
      return pending_path;
    }
  }
  fs::path pending_path = source;
  pending_path += L".pending.reboot";
  return pending_path;
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

void CleanupStaleRebootCopies(const fs::path& source) {
  std::error_code ec;
  const fs::path directory = source.parent_path();
  if (!fs::exists(directory, ec)) {
    return;
  }

  const std::wstring prefix = source.filename().wstring() + L".pending.reboot";
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

bool ScheduleReplaceOnReboot(const fs::path& source,
                            const fs::path& destination,
                            bool& reboot_required,
                            std::wstring* error) {
  CleanupStaleRebootCopies(source);

  const fs::path staged_source = BuildPendingRebootPath(source);
  if (CopyFileW(source.c_str(), staged_source.c_str(), FALSE) != TRUE) {
    if (error != nullptr) {
      *error = L"Failed to create staged reboot copy " + staged_source.wstring() +
               L" (" + FormatWindowsErrorMessage(GetLastError()) + L").";
    }
    return false;
  }

  const std::wstring normalized_staged_source =
      NormalizePathForPendingOperation(staged_source.wstring());
  const std::wstring normalized_destination =
      NormalizePathForPendingOperation(destination.wstring());
  if (MoveFileExW(normalized_staged_source.c_str(),
                  normalized_destination.c_str(),
                  MOVEFILE_DELAY_UNTIL_REBOOT | MOVEFILE_REPLACE_EXISTING) !=
      TRUE) {
    const DWORD move_error = GetLastError();
    std::error_code ec;
    fs::remove(staged_source, ec);
    if (error != nullptr) {
      *error = L"Failed to schedule reboot replacement from " +
               staged_source.wstring() + L" to " + destination.wstring() +
               L" (" + FormatWindowsErrorMessage(move_error) + L").";
    }
    return false;
  }

  reboot_required = true;
  if (error != nullptr) {
    error->clear();
  }
  return true;
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

bool DeleteTask(const wchar_t* name) {
  HRESULT initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  if (FAILED(initialized) && initialized != RPC_E_CHANGED_MODE) return false;
  ITaskService* service = nullptr;
  ITaskFolder* folder = nullptr;
  HRESULT hr = CoCreateInstance(CLSID_TaskScheduler, nullptr, CLSCTX_INPROC_SERVER,
      IID_ITaskService, reinterpret_cast<void**>(&service));
  VARIANT empty = {};
  if (SUCCEEDED(hr)) hr = service->Connect(empty, empty, empty, empty);
  BSTR root = SysAllocString(L"\\");
  BSTR task = SysAllocString(name);
  if (!root || !task) hr = E_OUTOFMEMORY;
  if (SUCCEEDED(hr)) hr = service->GetFolder(root, &folder);
  if (SUCCEEDED(hr)) hr = folder->DeleteTask(task, 0);
  SysFreeString(task);
  SysFreeString(root);
  if (folder) folder->Release();
  if (service) service->Release();
  if (SUCCEEDED(initialized)) CoUninitialize();
  return SUCCEEDED(hr) || hr == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND) ||
         hr == HRESULT_FROM_WIN32(ERROR_PATH_NOT_FOUND);
}

bool DeleteReregisterTask() { return DeleteTask(kReregisterTaskName); }

bool DeleteLauncherAutostartTask() { return DeleteTask(kLauncherAutostartTaskName); }

bool ScheduleReregisterTask(const Options& options, std::wstring& error) {
  const fs::path schtasks =
      fs::path(GetNativeSystemDirectoryForChildProcess()) / L"schtasks.exe";
  const std::wstring task_command =
      QuoteCommandLineArgument(GetModulePath()) + L" /r /s --appdir " +
      QuoteCommandLineArgument(options.app_dir);
  std::wstring command = QuoteCommandLineArgument(schtasks.wstring()) +
                         L" /Create /TN " +
                         QuoteCommandLineArgument(kReregisterTaskName) +
                         L" /SC ONSTART /RU SYSTEM /RL HIGHEST /TR " +
                         QuoteCommandLineArgument(task_command) + L" /F";
  DWORD exit_code = 0;
  DWORD error_code = 0;
  if (!RunProcess(schtasks.wstring(), command, GetModuleDirectory(), &exit_code,
                  &error_code)) {
    error = L"Failed to launch schtasks.exe (" +
            FormatWindowsErrorMessage(error_code) + L").";
    return false;
  }
  if (exit_code != 0) {
    error = L"Failed to schedule TSF re-registration after reboot "
            L"(schtasks exit code " +
            std::to_wstring(exit_code) + L").";
    return false;
  }
  return true;
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
                         bool& reboot_required,
                         std::wstring* error,
                         DWORD* initial_copy_error = nullptr,
                         DWORD* fallback_error = nullptr) {
  if (initial_copy_error != nullptr) {
    *initial_copy_error = 0;
  }
  if (fallback_error != nullptr) {
    *fallback_error = 0;
  }
  if (!fs::exists(source)) {
    if (error != nullptr) {
      *error = L"Source file does not exist: " + source.wstring();
    }
    return false;
  }
  CleanupStalePendingFiles(destination);
  if (CopyFileW(source.c_str(), destination.c_str(), FALSE) == TRUE) {
    if (error != nullptr) {
      error->clear();
    }
    return true;
  }
  const DWORD initial_copy_error_code = GetLastError();
  if (initial_copy_error != nullptr) {
    *initial_copy_error = initial_copy_error_code;
  }
  if (RenameFileForDeleteOnReboot(destination, reboot_required)) {
    if (CopyFileW(source.c_str(), destination.c_str(), FALSE) == TRUE) {
      if (error != nullptr) {
        error->clear();
      }
      CleanupStalePendingFiles(destination);
      return true;
    }
    const DWORD retry_copy_error = GetLastError();
    if (fallback_error != nullptr) {
      *fallback_error = retry_copy_error;
    }
    if (error != nullptr) {
      *error = L"Initial copy failed (" +
               FormatWindowsErrorMessage(initial_copy_error_code) +
               L"); existing destination was scheduled for delete-on-reboot, "
               L"but retry copy also failed (" +
               FormatWindowsErrorMessage(retry_copy_error) + L").";
    }
    return false;
  }
  if (error != nullptr) {
    const DWORD rename_error = GetLastError();
    if (fallback_error != nullptr) {
      *fallback_error = rename_error;
    }
    *error = L"Initial copy failed (" +
             FormatWindowsErrorMessage(initial_copy_error_code) +
             L"); fallback rename/delete-on-reboot also failed (" +
             FormatWindowsErrorMessage(rename_error) + L").";
  }
  return false;
}

int ShowFailureAndReturn(const std::wstring& message, const bool silent) {
  AppendSetupLog(message);
  ShowMessage(message, L"SetupHelper", MB_ICONERROR | MB_OK, silent);
  return kExitFailure;
}

bool PersistInstallDirectory(const fs::path& directory) {
  const std::wstring path = directory.wstring();
  FILETIME now = {};
  GetSystemTimeAsFileTime(&now);
  const std::wstring generation = std::to_wstring(now.dwHighDateTime) + L"-" + std::to_wstring(now.dwLowDateTime);
  for (REGSAM view : {KEY_WOW64_32KEY, KEY_WOW64_64KEY}) {
    HKEY key = nullptr;
    LONG result = RegCreateKeyExW(HKEY_LOCAL_MACHINE, L"Software\\MoqiIM", 0,
        nullptr, 0, KEY_SET_VALUE | view, nullptr, &key, nullptr);
    if (result == ERROR_SUCCESS) {
      result = RegSetValueExW(key, L"InstallDir", 0, REG_SZ,
          reinterpret_cast<const BYTE*>(path.c_str()),
          static_cast<DWORD>((path.size() + 1) * sizeof(wchar_t)));
      if (result == ERROR_SUCCESS) result = RegSetValueExW(key, L"SetupGeneration", 0, REG_SZ,
          reinterpret_cast<const BYTE*>(generation.c_str()),
          static_cast<DWORD>((generation.size() + 1) * sizeof(wchar_t)));
      RegCloseKey(key);
    }
    if (result != ERROR_SUCCESS) return false;
  }
  return true;
}

bool SetRegistrationPending(DWORD pending) {
  for (REGSAM view : {KEY_WOW64_32KEY, KEY_WOW64_64KEY}) {
    HKEY key = nullptr;
    LONG result = RegCreateKeyExW(HKEY_LOCAL_MACHINE, L"Software\\MoqiIM", 0,
        nullptr, 0, KEY_SET_VALUE | view, nullptr, &key, nullptr);
    if (result == ERROR_SUCCESS) {
      result = RegSetValueExW(key, L"RegistrationPending", 0, REG_DWORD,
          reinterpret_cast<const BYTE*>(&pending), sizeof(pending));
      RegCloseKey(key);
    }
    if (result != ERROR_SUCCESS) return false;
  }
  return true;
}

bool HasMachineType(const fs::path& path, WORD expected) {
  std::ifstream file(path, std::ios::binary);
  IMAGE_DOS_HEADER dos = {};
  file.read(reinterpret_cast<char*>(&dos), sizeof(dos));
  if (!file || dos.e_magic != IMAGE_DOS_SIGNATURE || dos.e_lfanew < 0) return false;
  file.seekg(dos.e_lfanew);
  DWORD signature = 0;
  IMAGE_FILE_HEADER header = {};
  file.read(reinterpret_cast<char*>(&signature), sizeof(signature));
  file.read(reinterpret_cast<char*>(&header), sizeof(header));
  return file && signature == IMAGE_NT_SIGNATURE && header.Machine == expected;
}

bool ValidatePayload(const Options& options) {
  const fs::path root(options.app_dir);
  if (!HasMachineType(root / L"MoqiTextService.dll", IMAGE_FILE_MACHINE_I386) ||
      !HasMachineType(root / L"x64/MoqiTextService.dll", IMAGE_FILE_MACHINE_AMD64) ||
      !HasMachineType(root / L"MoqiLauncher.exe", IMAGE_FILE_MACHINE_I386) ||
      (!HasMachineType(root / L"moqi-ime/server.exe", IMAGE_FILE_MACHINE_AMD64) &&
       !HasMachineType(root / L"moqi-ime/server.exe", IMAGE_FILE_MACHINE_I386))) return false;
  // Exercise the exact profile parser without calling any registration API.
  if (!SetEnvironmentVariableW(kProgramDirEnvVar, options.app_dir.c_str()) ||
      !SetEnvironmentVariableW(L"MOQI_SETUP_MODE", L"validate")) return false;
  HMODULE dll = LoadLibraryExW((root / L"MoqiTextService.dll").c_str(), nullptr,
                              LOAD_WITH_ALTERED_SEARCH_PATH);
  using Register = HRESULT (STDAPICALLTYPE*)();
  auto entry = dll ? reinterpret_cast<Register>(GetProcAddress(dll, "DllRegisterServer")) : nullptr;
  bool valid = entry && SUCCEEDED(entry());
  if (dll) FreeLibrary(dll);
  SetEnvironmentVariableW(L"MOQI_SETUP_MODE", nullptr);
  return valid;
}

int RunUserSetup(const Options& options) {
  g_userSetupLog = true;
  // Never enable a TIP or start a launcher as a UAC credential account/SYSTEM.
  DWORD session = 0;
  if (IsRunningAsAdmin() || !ProcessIdToSessionId(GetCurrentProcessId(), &session) || session == 0) {
    AppendSetupLog(L"Deferred: /user requires the unelevated target user's interactive session.");
    return kExitUserDeferred;
  }
  HKEY state = nullptr;
  LONG error = RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"Software\\MoqiIM", 0,
      KEY_QUERY_VALUE | KEY_NOTIFY | KEY_WOW64_64KEY, &state);
  if (error != ERROR_SUCCESS) return ShowFailureAndReturn(L"Cannot read machine installation state: " + std::to_wstring(error), options.silent);
  HANDLE changed = CreateEventW(nullptr, FALSE, FALSE, nullptr);
  DWORD pending = 1;
  const ULONGLONG deadline = GetTickCount64() + 60000;
  while (true) {
    if (!changed) { error = GetLastError(); break; }
    error = RegNotifyChangeKeyValue(state, FALSE, REG_NOTIFY_CHANGE_LAST_SET, changed, TRUE);
    if (error != ERROR_SUCCESS) break;
    DWORD bytes = sizeof(pending);
    error = RegGetValueW(state, nullptr, L"RegistrationPending", RRF_RT_REG_DWORD, nullptr, &pending, &bytes);
    if (error != ERROR_SUCCESS || !pending) break;
    const ULONGLONG now = GetTickCount64();
    if (now >= deadline) { error = ERROR_TIMEOUT; break; }
    DWORD wait = WaitForSingleObject(changed, static_cast<DWORD>(deadline - now));
    if (wait != WAIT_OBJECT_0) { error = wait == WAIT_TIMEOUT ? ERROR_TIMEOUT : GetLastError(); break; }
  }
  RegCloseKey(state);
  if (changed) CloseHandle(changed);
  if (error != ERROR_SUCCESS || pending) {
    AppendSetupLog(L"Deferred: machine registration not ready after bounded wait; Win32=" + std::to_wstring(error));
    ShowMessage(L"Machine registration is still pending or failed. See setup-machine.log; retry SetupHelper /user after registration completes.", L"SetupHelper", MB_ICONWARNING | MB_OK, options.silent);
    return kExitUserDeferred;
  }
  wchar_t generation[64] = {}, applied[64] = {};
  DWORD bytes = sizeof(generation);
  HKEY machineState = nullptr;
  LONG generationResult = RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"Software\\MoqiIM", 0,
      KEY_QUERY_VALUE | KEY_WOW64_64KEY, &machineState);
  if (generationResult == ERROR_SUCCESS) {
    generationResult = RegGetValueW(machineState, nullptr, L"SetupGeneration",
        RRF_RT_REG_SZ, nullptr, generation, &bytes);
    RegCloseKey(machineState);
  }
  if (generationResult != ERROR_SUCCESS) return kExitFailure;
  bytes = sizeof(applied);
  const bool alreadyApplied = RegGetValueW(HKEY_CURRENT_USER, L"Software\\MoqiIM", L"SetupGeneration",
      RRF_RT_REG_SZ, nullptr, applied, &bytes) == ERROR_SUCCESS && wcscmp(generation, applied) == 0;
  if (!alreadyApplied && !ValidatePayload(options)) return ShowFailureAndReturn(L"Invalid IME payload/profile.", options.silent);
  if (!alreadyApplied) {
    if (!SetEnvironmentVariableW(L"MOQI_SETUP_MODE", L"user")) return kExitFailure;
    bool registered = RunRegsvr(fs::path(GetSyswow64DirectoryPath()) / L"regsvr32.exe",
        fs::path(options.app_dir) / L"MoqiTextService.dll", options.app_dir, false);
    SetEnvironmentVariableW(L"MOQI_SETUP_MODE", nullptr);
    if (!registered) return ShowFailureAndReturn(L"User profile enable/verification failed.", options.silent);
    HKEY key = nullptr;
    LONG result = RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\MoqiIM", 0,
        nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr);
    if (result == ERROR_SUCCESS) {
      result = RegSetValueExW(key, L"SetupGeneration", 0, REG_SZ,
          reinterpret_cast<const BYTE*>(generation), static_cast<DWORD>((wcslen(generation) + 1) * sizeof(wchar_t)));
      RegCloseKey(key);
    }
    if (result != ERROR_SUCCESS) return kExitFailure;
  }
  // Replace the old per-user autostart only from that user's own context.
  LONG removed = RegDeleteKeyValueW(HKEY_CURRENT_USER,
      L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", L"MoqiLauncher");
  if (removed != ERROR_SUCCESS && removed != ERROR_FILE_NOT_FOUND && removed != ERROR_PATH_NOT_FOUND)
    return ShowFailureAndReturn(L"Failed to remove legacy user autostart.", options.silent);
  const auto launcher = fs::path(options.app_dir) / L"MoqiLauncher.exe";
  const auto started = reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr, L"open",
      launcher.c_str(), nullptr, options.app_dir.c_str(), SW_SHOWNORMAL));
  return started > 32 ? kExitSuccess : kExitFailure;
}

bool SamePayload(const fs::path& source, const fs::path& destination) {
  std::error_code ec;
  const auto size = fs::file_size(source, ec);
  if (ec || size != fs::file_size(destination, ec) || ec) return false;
  std::ifstream left(source, std::ios::binary), right(destination, std::ios::binary);
  return left && right && std::equal(std::istreambuf_iterator<char>(left),
      std::istreambuf_iterator<char>(), std::istreambuf_iterator<char>(right),
      std::istreambuf_iterator<char>());
}

int RunReregister(const Options& options) {
  if (!ValidatePayload(options))
    return ShowFailureAndReturn(L"Missing, wrong-architecture or invalid IME payload/profile.", options.silent);
  const fs::path app_dir(options.app_dir);
  const fs::path source32 = app_dir / L"MoqiTextService.dll";
  const fs::path source64 = app_dir / L"x64" / L"MoqiTextService.dll";
  const fs::path dest32 = fs::path(GetSyswow64DirectoryPath()) / L"MoqiTextService.dll";
  const fs::path dest64 = fs::path(GetNativeSystemDirectoryPath()) / L"MoqiTextService.dll";
  const fs::path dest64_for_regsvr =
      fs::path(GetNativeSystemDirectoryForChildProcess()) / L"MoqiTextService.dll";
  const fs::path regsvr32 = fs::path(GetSyswow64DirectoryPath()) / L"regsvr32.exe";
  const fs::path regsvr64 = fs::path(GetNativeSystemDirectoryPath()) / L"regsvr32.exe";

  // Do not register an older system DLL if delayed replacement has not run.
  if (!SamePayload(source32, dest32) || !SamePayload(source64, dest64))
    return ShowFailureAndReturn(L"System TSF payload does not match the installed version; reboot/retry is required.", options.silent);
  CleanupStaleOldFiles(dest32);
  CleanupStaleOldFiles(dest64);
  CleanupStaleRebootCopies(source32);
  CleanupStaleRebootCopies(source64);

  if (!RunRegsvr(regsvr32, dest32, app_dir, false)) {
    return ShowFailureAndReturn(L"Failed to register Win32 TSF DLL.",
                                options.silent);
  }
  if (!RunRegsvr(regsvr64, dest64_for_regsvr, app_dir, false, 0, dest64)) {
    return ShowFailureAndReturn(L"Failed to register x64 TSF DLL.",
                                options.silent);
  }
  if (!SetRegistrationPending(0) || !DeleteReregisterTask())
    return ShowFailureAndReturn(L"Failed to finalize reboot registration.", options.silent);
  return kExitSuccess;
}

int RunInstall(const Options& options) {
  if (!ValidatePayload(options))
    return ShowFailureAndReturn(L"Missing, wrong-architecture or invalid IME payload/profile.", options.silent);
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

  if (!SetRegistrationPending(1))
    return ShowFailureAndReturn(L"Failed to record pending registration.", options.silent);
  if (!DeleteReregisterTask() || !DeleteLauncherAutostartTask())
    return ShowFailureAndReturn(L"Failed to remove legacy scheduled tasks.", options.silent);
  // During an in-place reinstall/upgrade, unregistering first removes the TIP
  // from the user's language profile list. Re-registering the DLL does not
  // always restore that list entry reliably, so keep the existing registration
  // in place and overwrite the system DLLs before registering again.

  bool reboot_required = false;
  std::wstring copy_error;
  DWORD initial_copy_error = 0;
  DWORD fallback_error = 0;
  if (!CopyFileWithFallback(source32, dest32, reboot_required, &copy_error,
                            &initial_copy_error, &fallback_error)) {
    if (!((initial_copy_error == ERROR_SHARING_VIOLATION ||
           initial_copy_error == ERROR_ACCESS_DENIED ||
           fallback_error == ERROR_SHARING_VIOLATION ||
           fallback_error == ERROR_ACCESS_DENIED) &&
          ScheduleReplaceOnReboot(source32, dest32, reboot_required,
                                  &copy_error))) {
      return ShowFailureAndReturn(
          L"Failed to update Win32 TSF DLL in " + dest32.wstring() + L"\n\n" +
              copy_error,
          options.silent);
    }
  }
  initial_copy_error = 0;
  fallback_error = 0;
  if (!CopyFileWithFallback(source64, dest64, reboot_required, &copy_error,
                            &initial_copy_error, &fallback_error)) {
    if (!((initial_copy_error == ERROR_SHARING_VIOLATION ||
           initial_copy_error == ERROR_ACCESS_DENIED ||
           fallback_error == ERROR_SHARING_VIOLATION ||
           fallback_error == ERROR_ACCESS_DENIED) &&
          ScheduleReplaceOnReboot(source64, dest64, reboot_required,
                                  &copy_error))) {
      return ShowFailureAndReturn(
          L"Failed to update x64 TSF DLL in " + dest64.wstring() + L"\n\n" +
              copy_error,
          options.silent);
    }
  }

  if (!PersistInstallDirectory(app_dir))
    return ShowFailureAndReturn(L"Failed to persist installation directory.", options.silent);

  if (reboot_required) {
    std::wstring schedule_error;
    if (!ScheduleReregisterTask(options, schedule_error)) {
      return ShowFailureAndReturn(schedule_error, options.silent);
    }
    return kExitRestartRequired;
  }

  if (!RunRegsvr(regsvr32, dest32, app_dir, false)) {
    return ShowFailureAndReturn(L"Failed to register Win32 TSF DLL.",
                                options.silent);
  }
  if (!RunRegsvr(regsvr64, dest64_for_regsvr, app_dir, false, 0, dest64)) {
    return ShowFailureAndReturn(L"Failed to register x64 TSF DLL.",
                                options.silent);
  }
  if (!SetRegistrationPending(0))
    return ShowFailureAndReturn(L"Failed to finalize installation state.", options.silent);
  // Never create an ONLOGON task as the elevated installer/SYSTEM account.
  // Interactive-user enablement and autostart are a separate /user step.
  if (!DeleteLauncherAutostartTask())
    return ShowFailureAndReturn(L"Failed to remove legacy launcher autostart task.", options.silent);
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

  if (!DeleteReregisterTask() || !DeleteLauncherAutostartTask())
    return ShowFailureAndReturn(L"Failed to remove scheduled tasks.", options.silent);
  // Attempt both views even when one fails; never delete files on any error.
  const bool removed32 = RunRegsvr(regsvr32, dest32, app_dir, true, KEY_WOW64_32KEY);
  const bool removed64 = RunRegsvr(regsvr64, dest64_for_regsvr, app_dir, true,
                                  KEY_WOW64_64KEY, dest64);
  if (!removed32 || !removed64)
    return ShowFailureAndReturn(L"TSF unregistration failed; files were not deleted.", options.silent);

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
    } else if (arg == L"/user" || arg == L"/validate") {
      if (options.action != Action::kHelp) {
        error = L"Only one action may be specified.";
        return false;
      }
      options.action = arg == L"/user" ? Action::kUser : Action::kValidate;
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

  if (options.app_dir.empty() || !fs::path(options.app_dir).is_absolute())
    return ShowFailureAndReturn(L"Application directory must be absolute.", options.silent);
  options.app_dir = fs::path(options.app_dir).lexically_normal().wstring();
  if (options.action == Action::kValidate)
    return ValidatePayload(options) ? kExitSuccess : kExitFailure;
  if (options.action == Action::kUser) return RunUserSetup(options);

  if (!IsRunningAsAdmin()) {
    return RestartElevated(args, options.silent);
  }
  // Prevent inherited mode variables from changing privileged registration.
  SetEnvironmentVariableW(L"MOQI_SETUP_MODE", nullptr);

  if (options.action == Action::kInstall) {
    return RunInstall(options);
  }
  if (options.action == Action::kReregister) {
    return RunReregister(options);
  }
  return RunUninstall(options);
}
