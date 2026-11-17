#include "Utils.h"

#include <fstream>
#include <Windows.h>
#include <ShlObj.h>

namespace Moqi {

bool loadJsonFile(const std::wstring filename, Json::Value& result) {
    result.clear();
    std::ifstream fp(filename, std::ifstream::binary);
    if (!fp) {
        return false;
    }
    Json::CharReaderBuilder builder;
    std::string errors;
    return Json::parseFromStream(builder, fp, &result, &errors);
}

bool saveJsonFile(const std::wstring filename, Json::Value& data) {
	std::ofstream fp(filename, std::ifstream::binary);
	if (fp) {
		fp << data;
		return true;
	}
	return false;
}

std::wstring getCurrentExecutableDir() {
    std::wstring path(MAX_PATH, L'\0');
    for (;;) {
        DWORD len = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
        if (len == 0) {
            return L"";
        }
        if (len < path.size() - 1) {
            path.resize(len);
            return path.substr(0, path.find_last_of(L"\\/"));
        }
        path.resize(path.size() * 2);
    }
}

std::wstring getAppLocalDir() {
    wchar_t* appLocalDataDirPath = nullptr;
    if (FAILED(::SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &appLocalDataDirPath))) {
        return L"";
    }
    std::wstring result{appLocalDataDirPath};
    ::CoTaskMemFree(appLocalDataDirPath);
    return result;
}

std::wstring getMoqiAppDataDir() {
    wchar_t* appDataDirPath = nullptr;
    if (FAILED(::SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &appDataDirPath))) {
        return L"";
    }
    std::wstring result = std::wstring(appDataDirPath) + L"\\Moqi";
    ::CoTaskMemFree(appDataDirPath);
    return result;
}

bool makeDirs(const std::wstring& path) {
    return ::SHCreateDirectoryEx(nullptr, path.c_str(), nullptr) == ERROR_SUCCESS;
}

std::string generateUuidStr() {
    UUID uuid;
    UuidCreate(&uuid);

    RPC_CSTR uuid_str = nullptr;
    UuidToStringA(&uuid, &uuid_str);
    std::string result = (char*)uuid_str;
    RpcStringFreeA(&uuid_str);
    return result;
}

} // namespace Moqi
