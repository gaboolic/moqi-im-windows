#include "MoqiImeModule.h"
#include "resource.h"
#include <iostream>
#include <fstream>
#include <mutex>
#include <new>
#include <vector>
#include <ShlObj.h>
#include <Shlwapi.h> // for PathIsRelative
#include <VersionHelpers.h>  // Provided by Windows SDK >= 8.1
#include <strsafe.h>

#include <json/json.h>
#include "../libIME2/src/DebugLogConfig.h"
#include "../libIME2/src/DebugLogFile.h"
#include "../libIME2/src/Utils.h"

namespace {

bool endsWithCaseInsensitive(const std::wstring& value, const wchar_t* suffix) {
	if (!suffix) {
		return false;
	}
	const size_t suffixLen = wcslen(suffix);
	if (value.length() < suffixLen) {
		return false;
	}
	const wchar_t* valueSuffix = value.c_str() + value.length() - suffixLen;
	return _wcsicmp(valueSuffix, suffix) == 0;
}

void appendDllMainLog(const wchar_t* line) {
	wchar_t localAppData[MAX_PATH] = {};
	DWORD localAppDataLen = ::GetEnvironmentVariableW(L"LOCALAPPDATA", localAppData, _countof(localAppData));
	if (localAppDataLen == 0 || localAppDataLen >= _countof(localAppData)) {
		return;
	}

	wchar_t moqiDir[MAX_PATH] = {};
	if (FAILED(StringCchPrintfW(moqiDir, _countof(moqiDir), L"%s\\MoqiIM", localAppData))) {
		return;
	}
	::CreateDirectoryW(moqiDir, nullptr);

	wchar_t logDir[MAX_PATH] = {};
	if (FAILED(StringCchPrintfW(logDir, _countof(logDir), L"%s\\Log", moqiDir))) {
		return;
	}
	const std::wstring preparedLogPath = Ime::DebugLogFile::prepareDailyLogFilePath(
		logDir, L"tsf-dllmain.log");
	if (preparedLogPath.empty()) {
		return;
	}

	HANDLE file = ::CreateFileW(
		preparedLogPath.c_str(),
		FILE_APPEND_DATA,
		FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
		nullptr,
		OPEN_ALWAYS,
		FILE_ATTRIBUTE_NORMAL,
		nullptr);
	if (file == INVALID_HANDLE_VALUE) {
		return;
	}

	DWORD bytesWritten = 0;
	::WriteFile(file, line, static_cast<DWORD>(wcslen(line) * sizeof(wchar_t)), &bytesWritten, nullptr);
	static const wchar_t newline[] = L"\r\n";
	::WriteFile(file, newline, static_cast<DWORD>((_countof(newline) - 1) * sizeof(wchar_t)), &bytesWritten, nullptr);
	::CloseHandle(file);
}

const wchar_t* processBaseName(const wchar_t* path) {
	if (!path || !*path) {
		return L"";
	}
	const wchar_t* slash = wcsrchr(path, L'\\');
	const wchar_t* altSlash = wcsrchr(path, L'/');
	const wchar_t* base = slash;
	if (!base || (altSlash && altSlash > base)) {
		base = altSlash;
	}
	return base ? base + 1 : path;
}

void logDllMainEvent(const wchar_t* phase, HMODULE module, LPVOID reserved) {
	if (!Ime::isDebugLoggingEnabled()) {
		return;
	}

	SYSTEMTIME st{};
	::GetLocalTime(&st);

	wchar_t exePath[MAX_PATH] = {};
	::GetModuleFileNameW(nullptr, exePath, _countof(exePath));

	wchar_t dllPath[MAX_PATH] = {};
	::GetModuleFileNameW(module, dllPath, _countof(dllPath));

	wchar_t line[1024] = {};
	if (SUCCEEDED(StringCchPrintfW(
		line,
		_countof(line),
		L"[%04u-%02u-%02u %02u:%02u:%02u.%03u][pid=%lu][tid=%lu] [%s] exe=%s exe_path=%s dll=%s terminating=%s",
		st.wYear,
		st.wMonth,
		st.wDay,
		st.wHour,
		st.wMinute,
		st.wSecond,
		st.wMilliseconds,
		::GetCurrentProcessId(),
		::GetCurrentThreadId(),
		phase ? phase : L"unknown",
		processBaseName(exePath),
		exePath,
		dllPath,
		reserved ? L"true" : L"false"))) {
		appendDllMainLog(line);
		::OutputDebugStringW(line);
		::OutputDebugStringW(L"\n");
	}
}

}

Moqi::ImeModule* g_imeModule = NULL;
HMODULE g_dllModule = NULL;
std::mutex g_imeModuleMutex;

Moqi::ImeModule* getOrCreateImeModule() {
	std::lock_guard<std::mutex> lock(g_imeModuleMutex);
	if (g_imeModule == nullptr && g_dllModule != nullptr) {
		logDllMainEvent(L"module_init", g_dllModule, nullptr);
		g_imeModule = new (std::nothrow) Moqi::ImeModule(g_dllModule);
	}
	return g_imeModule;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD  ul_reason_for_call, LPVOID lpReserved) {
	switch (ul_reason_for_call) {
	case DLL_PROCESS_ATTACH:
		logDllMainEvent(L"process_attach", hModule, lpReserved);
		::DisableThreadLibraryCalls(hModule); // disable DllMain calls due to new thread creation
		g_dllModule = hModule;
		break;
	case DLL_PROCESS_DETACH:
		logDllMainEvent(L"process_detach", hModule, lpReserved);
		if(g_imeModule) {
			g_imeModule->Release();
			g_imeModule = NULL;
		}
		g_dllModule = NULL;
		break;
	}
	return TRUE;
}

STDAPI DllCanUnloadNow(void) {
	return g_imeModule ? g_imeModule->canUnloadNow() : S_OK;
}

STDAPI DllGetClassObject(REFCLSID rclsid, REFIID riid, void **ppvObj) {
	Moqi::ImeModule* imeModule = getOrCreateImeModule();
	return imeModule ? imeModule->getClassObject(rclsid, riid, ppvObj) : E_OUTOFMEMORY;
}

STDAPI DllUnregisterServer(void) {
	Moqi::ImeModule* imeModule = getOrCreateImeModule();
	return imeModule ? imeModule->unregisterServer() : E_FAIL;
}

static inline Ime::LangProfileInfo langProfileFromJson(std::wstring file, std::string& guid, int defaultIconIndex) {
	// load the json file to get the info of input method
	std::ifstream fp(file, std::ifstream::binary);
	if(fp) {
		Json::Value json;
		fp >> json;
		auto name = utf8ToUtf16(json["name"].asCString());
		guid = json["guid"].asCString();
		auto guidStr = utf8ToUtf16(guid.c_str());
		CLSID guid = {0};
		CLSIDFromString (guidStr.c_str(), &guid);
		// convert locale name to lanid
		auto locale = utf8ToUtf16(json["locale"].asCString());
		auto fallbackLocale = utf8ToUtf16(json["fallbackLocale"].asCString());
		// ::MessageBox(0, name.c_str(), 0, 0);
		auto iconFile = utf8ToUtf16(json["icon"].asCString());
		if (!iconFile.empty() && PathIsRelative(iconFile.c_str())) {
			int p = file.rfind('\\');
			if (p != file.npos) {
				iconFile = file.substr(0, p + 1) + iconFile;
			}
		}
		int iconIndex = endsWithCaseInsensitive(iconFile, L".ico") ? 0 : defaultIconIndex;
		// ::MessageBox(0, iconFile.c_str(), 0, 0);
		Ime::LangProfileInfo langProfile = {
			name,
			guid,
			locale,
			fallbackLocale,
			iconFile,
			iconIndex
		};
		return langProfile;
	}
	return Ime::LangProfileInfo();
}

STDAPI DllRegisterServer(void) {
	Moqi::ImeModule* imeModule = getOrCreateImeModule();
	if (imeModule == nullptr) {
		return E_FAIL;
	}
	int iconIndex = 0; // use classic icon
	if(::IsWindows8OrGreater()) {
		iconIndex = 1; // use Windows 8 style IME icon
    }
	std::vector<Ime::LangProfileInfo> langProfiles;
	std::wstring dirPath;
	for (const auto backendDir: imeModule->backendDirs()) {
		std::string backendName = utf16ToUtf8(backendDir.c_str());
		dirPath = imeModule->programDir();
		dirPath += L'\\';
		dirPath += backendDir;
		dirPath += L"\\input_methods";
		// scan the dir for lang profile definition files
		WIN32_FIND_DATA findData = { 0 };
		HANDLE hFind = ::FindFirstFile((dirPath + L"\\*").c_str(), &findData);
		if (hFind != INVALID_HANDLE_VALUE) {
			do {
				if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) { // this is a subdir
					if (findData.cFileName[0] != '.') {
						std::wstring imejson = dirPath;
						imejson += '\\';
						imejson += findData.cFileName;
						imejson += L"\\ime.json";
						// Make sure the file exists
						DWORD fileAttrib = GetFileAttributesW(imejson.c_str());
						if (fileAttrib != INVALID_FILE_ATTRIBUTES) {
							// load the json file to get the info of input method
							std::string guid;
							Ime::LangProfileInfo langProfile = langProfileFromJson(imejson, guid, iconIndex);
							if (!langProfile.name.empty()) {
								langProfiles.push_back(std::move(langProfile));
							}
						}
					}
				}
			} while (::FindNextFile(hFind, &findData));
			::FindClose(hFind);
		}
	}
	return imeModule->registerServer(L"MoqiTextService", langProfiles.data(), langProfiles.size());
}
