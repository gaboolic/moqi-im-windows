#include "TsfLog.h"

#include <Windows.h>
#include <Objbase.h>
#include <fstream>
#include <sstream>

namespace MoqiIME {

namespace {

std::wstring logFilePath() {
    wchar_t localAppData[MAX_PATH] = {0};
    if (::ExpandEnvironmentStringsW(L"%LOCALAPPDATA%", localAppData, MAX_PATH) == 0) {
        return L"";
    }
    std::wstring baseDir = std::wstring(localAppData) + L"\\MoqiIM";
    std::wstring logDir = baseDir + L"\\Log";
    ::CreateDirectoryW(baseDir.c_str(), nullptr);
    ::CreateDirectoryW(logDir.c_str(), nullptr);
    return logDir + L"\\tsf-debug.log";
}

std::wstring timestampNow() {
    SYSTEMTIME st{};
    ::GetLocalTime(&st);
    wchar_t buffer[64] = {0};
    _snwprintf_s(buffer, _countof(buffer), _TRUNCATE,
                 L"%04u-%02u-%02u %02u:%02u:%02u.%03u",
                 st.wYear, st.wMonth, st.wDay,
                 st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
    return buffer;
}

}

void tsfLog(const std::wstring& message) {
    std::wostringstream line;
    line << L"[" << timestampNow() << L"]"
         << L"[pid=" << ::GetCurrentProcessId() << L"]"
         << L"[tid=" << ::GetCurrentThreadId() << L"] "
         << message;

    const std::wstring formatted = line.str();
    ::OutputDebugStringW((formatted + L"\n").c_str());

    const std::wstring path = logFilePath();
    if (path.empty()) {
        return;
    }
    std::wofstream stream(path, std::ios::app);
    if (!stream.is_open()) {
        return;
    }
    stream << formatted << L"\n";
}

std::wstring tsfGuidToString(REFGUID guid) {
    LPOLESTR buffer = nullptr;
    if (FAILED(::StringFromCLSID(guid, &buffer))) {
        return L"<guid-convert-failed>";
    }

    std::wstring result{buffer};
    ::CoTaskMemFree(buffer);
    return result;
}

} // namespace MoqiIME
