#include "TsfLog.h"

#include <Windows.h>
#include <ShlObj.h>
#include <cstdio>
#include <mutex>

namespace MoqiIME {

namespace {

std::mutex g_tsfLogMutex;

std::wstring tsfLogPath() {
    wchar_t* appLocalDataDirPath = nullptr;
    if (FAILED(::SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &appLocalDataDirPath))) {
        return L"";
    }

    std::wstring dirPath{appLocalDataDirPath};
    ::CoTaskMemFree(appLocalDataDirPath);
    dirPath += L"\\MoqiIM\\Log";
    ::SHCreateDirectoryExW(nullptr, dirPath.c_str(), nullptr);
    return dirPath + L"\\tsf.log";
}

std::wstring timestampNow() {
    SYSTEMTIME st = {};
    ::GetLocalTime(&st);

    wchar_t buffer[64] = {};
    swprintf_s(
        buffer,
        L"%04u-%02u-%02u %02u:%02u:%02u.%03u",
        st.wYear,
        st.wMonth,
        st.wDay,
        st.wHour,
        st.wMinute,
        st.wSecond,
        st.wMilliseconds
    );
    return buffer;
}

} // namespace

void tsfLog(const std::wstring& message) {
    const auto path = tsfLogPath();
    if (path.empty()) {
        return;
    }

    std::lock_guard<std::mutex> lock(g_tsfLogMutex);
    FILE* file = nullptr;
    if (_wfopen_s(&file, path.c_str(), L"a+, ccs=UTF-8") != 0 || file == nullptr) {
        return;
    }

    fwprintf(file, L"[%ls] %ls\n", timestampNow().c_str(), message.c_str());
    fclose(file);
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
