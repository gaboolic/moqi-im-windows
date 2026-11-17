#include "MoqiImeModule.h"
#include "resource.h"
#include <iostream>
#include <fstream>
#include <mutex>
#include <new>
#include <stdexcept>
#include <filesystem>
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

static HRESULT logRegistrationResult(const wchar_t* operation, HRESULT result) noexcept try {
	wchar_t mode[16] = {}, base[MAX_PATH] = {};
	GetEnvironmentVariableW(L"MOQI_SETUP_MODE", mode, _countof(mode));
	if (wcscmp(mode, L"validate") == 0) return result;
	bool user = wcscmp(mode, L"user") == 0;
	DWORD length = GetEnvironmentVariableW(user ? L"LOCALAPPDATA" : L"ProgramData", base, _countof(base));
	if (!length || length >= _countof(base)) return result;
	std::error_code ec;
	auto directory = std::filesystem::path(base) / L"MoqiIM" / L"Log";
	std::filesystem::create_directories(directory, ec);
	SYSTEMTIME now = {};
	GetLocalTime(&now);
	wchar_t line[256] = {};
	StringCchPrintfW(line, _countof(line), L"%04u-%02u-%02u %02u:%02u:%02u pid=%lu %s HRESULT=0x%08lX\r\n",
		now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond,
		GetCurrentProcessId(), operation, static_cast<unsigned long>(result));
	auto utf8 = utf16ToUtf8(line);
	HANDLE file = CreateFileW((directory / (user ? L"setup-user.log" : L"setup-machine.log")).c_str(),
		FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (file != INVALID_HANDLE_VALUE) {
		DWORD written = 0;
		WriteFile(file, utf8.data(), static_cast<DWORD>(utf8.size()), &written, nullptr);
		CloseHandle(file);
	}
	return result;
} catch (...) { return result; }

STDAPI DllUnregisterServer(void) {
	const CLSID clsid = {0x8f204c91, 0x2d7a, 0x4b3e, {0x9e, 0x1f, 0x6a, 0x5c, 0x0d, 0x8b, 0x2e, 0x7f}};
	HRESULT initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
	if (FAILED(initialized) && initialized != RPC_E_CHANGED_MODE) return initialized;
	ITfInputProcessorProfiles* profiles = nullptr;
	ITfCategoryMgr* categories = nullptr;
	HRESULT hr = CoCreateInstance(CLSID_TF_InputProcessorProfiles, nullptr, CLSCTX_INPROC_SERVER,
		IID_ITfInputProcessorProfiles, (void**)&profiles);
	if (SUCCEEDED(hr)) hr = profiles->Unregister(clsid);
	if (SUCCEEDED(hr)) hr = CoCreateInstance(CLSID_TF_CategoryMgr, nullptr, CLSCTX_INPROC_SERVER,
		IID_ITfCategoryMgr, (void**)&categories);
	const GUID required[] = {GUID_TFCAT_CATEGORY_OF_TIP, GUID_TFCAT_TIP_KEYBOARD,
		GUID_TFCAT_TIPCAP_SECUREMODE, GUID_TFCAT_TIPCAP_UIELEMENTENABLED,
		GUID_TFCAT_TIPCAP_INPUTMODECOMPARTMENT, GUID_TFCAT_TIPCAP_COMLESS,
		GUID_TFCAT_TIPCAP_WOW16, GUID_TFCAT_TIPCAP_IMMERSIVESUPPORT,
		GUID_TFCAT_TIPCAP_SYSTRAYSUPPORT, GUID_TFCAT_PROP_AUDIODATA, GUID_TFCAT_PROP_INKDATA,
		GUID_TFCAT_PROPSTYLE_CUSTOM, GUID_TFCAT_PROPSTYLE_STATIC, GUID_TFCAT_PROPSTYLE_STATICCOMPACT,
		GUID_TFCAT_DISPLAYATTRIBUTEPROVIDER, GUID_TFCAT_DISPLAYATTRIBUTEPROPERTY};
	for (const auto& category : required) {
		if (FAILED(hr)) break;
		hr = categories->UnregisterCategory(clsid, category, clsid);
	}
	if (SUCCEEDED(hr)) {
		LONG error = RegDeleteTreeW(HKEY_LOCAL_MACHINE,
			L"Software\\Classes\\CLSID\\{8F204C91-2D7A-4B3E-9E1F-6A5C0D8B2E7F}");
		if (error != ERROR_SUCCESS && error != ERROR_FILE_NOT_FOUND) hr = HRESULT_FROM_WIN32(error);
	}
	if (categories) categories->Release();
	if (profiles) profiles->Release();
	if (SUCCEEDED(initialized)) CoUninitialize();
	return logRegistrationResult(L"unregister", hr);
}

static inline Ime::LangProfileInfo langProfileFromJson(std::wstring file, std::string& guid, int defaultIconIndex) {
	// load the json file to get the info of input method
	std::ifstream fp(file, std::ifstream::binary);
	if(fp) {
		Json::Value json;
		fp >> json;
		for (const char* field : {"name", "guid", "locale", "fallbackLocale", "icon"}) {
			if (!json[field].isString()) throw std::runtime_error("Invalid profile field");
		}
		auto name = utf8ToUtf16(json["name"].asCString());
		if (name.empty()) throw std::runtime_error("Empty profile name");
		guid = json["guid"].asCString();
		auto guidStr = utf8ToUtf16(guid.c_str());
		CLSID guid = {0};
		if (FAILED(CLSIDFromString(guidStr.c_str(), &guid)) || IsEqualGUID(guid, GUID_NULL))
			throw std::runtime_error("Invalid profile GUID");
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
		if ((!LocaleNameToLCID(locale.c_str(), 0) &&
		     !LocaleNameToLCID(fallbackLocale.c_str(), 0)) || iconFile.empty() ||
		    GetFileAttributesW(iconFile.c_str()) == INVALID_FILE_ATTRIBUTES)
			throw std::runtime_error("Invalid profile locale or missing icon");
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

// Keep installation policy in the parent project: libIME2's generic register
// routine enables the elevated account and edits every loaded user hive.
static HRESULT registerMoqiProfiles(const std::vector<Ime::LangProfileInfo>& langs) {
	wchar_t mode[16] = {};
	GetEnvironmentVariableW(L"MOQI_SETUP_MODE", mode, _countof(mode));
	if (wcscmp(mode, L"validate") == 0) return S_OK; // strictly read-only
	const bool userOnly = wcscmp(mode, L"user") == 0;
	const HRESULT initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
	if (FAILED(initialized) && initialized != RPC_E_CHANGED_MODE) return initialized;
	ITfInputProcessorProfiles* profiles = nullptr;
	ITfInputProcessorProfileMgr* manager = nullptr;
	ITfCategoryMgr* categories = nullptr;
	HRESULT hr = CoCreateInstance(CLSID_TF_InputProcessorProfiles, nullptr,
		CLSCTX_INPROC_SERVER, IID_ITfInputProcessorProfiles, (void**)&profiles);
	if (SUCCEEDED(hr)) hr = CoCreateInstance(CLSID_TF_InputProcessorProfiles, nullptr,
		CLSCTX_INPROC_SERVER, IID_ITfInputProcessorProfileMgr, (void**)&manager);
	const CLSID clsid = {0x8f204c91, 0x2d7a, 0x4b3e, {0x9e, 0x1f, 0x6a, 0x5c, 0x0d, 0x8b, 0x2e, 0x7f}};
	if (SUCCEEDED(hr) && !userOnly) {
		wchar_t module[MAX_PATH] = {};
		DWORD length = GetModuleFileNameW(g_dllModule, module, _countof(module));
		if (!length || length >= _countof(module)) hr = HRESULT_FROM_WIN32(ERROR_BAD_PATHNAME);
		HKEY key = nullptr;
		if (SUCCEEDED(hr)) {
			LONG error = RegCreateKeyExW(HKEY_LOCAL_MACHINE,
				L"Software\\Classes\\CLSID\\{8F204C91-2D7A-4B3E-9E1F-6A5C0D8B2E7F}\\InprocServer32",
				0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr);
			if (error == ERROR_SUCCESS) error = RegSetValueExW(key, nullptr, 0, REG_SZ,
				reinterpret_cast<const BYTE*>(module), (length + 1) * sizeof(wchar_t));
			const wchar_t apartment[] = L"Apartment";
			if (error == ERROR_SUCCESS) error = RegSetValueExW(key, L"ThreadingModel", 0,
				REG_SZ, reinterpret_cast<const BYTE*>(apartment), sizeof(apartment));
			if (key) RegCloseKey(key);
			hr = HRESULT_FROM_WIN32(error);
		}
		if (SUCCEEDED(hr)) hr = profiles->Register(clsid);
	}
	for (const auto& lang : langs) {
		if (FAILED(hr)) break;
		LCID locale = LocaleNameToLCID(lang.locale.c_str(), 0);
		if (!locale) locale = LocaleNameToLCID(lang.fallbackLocale.c_str(), 0);
		LANGID id = LANGIDFROMLCID(locale);
		if (!userOnly) {
			hr = manager->RegisterProfile(clsid, id, lang.profileGuid, lang.name.c_str(),
				static_cast<ULONG>(lang.name.size()), lang.iconFile.c_str(),
				static_cast<ULONG>(lang.iconFile.size()), lang.iconIndex, nullptr, 0, TRUE, 0);
			if (SUCCEEDED(hr)) hr = profiles->EnableLanguageProfileByDefault(clsid, id, lang.profileGuid, TRUE);
		} else {
			hr = profiles->EnableLanguageProfile(clsid, id, lang.profileGuid, TRUE);
			BOOL enabled = FALSE;
			if (SUCCEEDED(hr)) hr = profiles->IsEnabledLanguageProfile(clsid, id, lang.profileGuid, &enabled);
			if (SUCCEEDED(hr) && !enabled) hr = E_FAIL;
			// Keep the Windows language-settings marker, but only in this actual
			// user's hive (never enumerate HKU or load the default-user hive).
			if (SUCCEEDED(hr)) {
				wchar_t serviceGuid[40] = {}, profileGuid[40] = {}, prefix[16] = {};
				StringFromGUID2(clsid, serviceGuid, _countof(serviceGuid));
				StringFromGUID2(lang.profileGuid, profileGuid, _countof(profileGuid));
				StringCchPrintfW(prefix, _countof(prefix), L"%04x:", id);
				const std::wstring name = std::wstring(prefix) + serviceGuid + profileGuid;
				const std::wstring path = L"Control Panel\\International\\User Profile\\" + lang.locale;
				HKEY key = nullptr;
				LONG error = RegCreateKeyExW(HKEY_CURRENT_USER, path.c_str(), 0, nullptr, 0,
					KEY_QUERY_VALUE | KEY_SET_VALUE, nullptr, &key, nullptr);
				DWORD order = 0, bytes = sizeof(order);
				if (error == ERROR_SUCCESS && RegGetValueW(key, nullptr, name.c_str(), RRF_RT_REG_DWORD,
					nullptr, &order, &bytes) != ERROR_SUCCESS) {
					error = RegQueryInfoKeyW(key, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
						&order, nullptr, nullptr, nullptr, nullptr);
					++order;
					if (error == ERROR_SUCCESS) error = RegSetValueExW(key, name.c_str(), 0, REG_DWORD,
						reinterpret_cast<const BYTE*>(&order), sizeof(order));
				}
				if (key) RegCloseKey(key);
				if (error != ERROR_SUCCESS) hr = HRESULT_FROM_WIN32(error);
			}
		}
		TF_INPUTPROCESSORPROFILE registered = {};
		if (SUCCEEDED(hr)) hr = manager->GetProfile(TF_PROFILETYPE_INPUTPROCESSOR, id,
			clsid, lang.profileGuid, nullptr, &registered);
	}
	if (SUCCEEDED(hr) && !userOnly) {
		hr = CoCreateInstance(CLSID_TF_CategoryMgr, nullptr, CLSCTX_INPROC_SERVER,
			IID_ITfCategoryMgr, (void**)&categories);
		const GUID required[] = {GUID_TFCAT_CATEGORY_OF_TIP, GUID_TFCAT_TIP_KEYBOARD,
			GUID_TFCAT_TIPCAP_SECUREMODE, GUID_TFCAT_TIPCAP_UIELEMENTENABLED,
			GUID_TFCAT_TIPCAP_INPUTMODECOMPARTMENT, GUID_TFCAT_TIPCAP_COMLESS,
			GUID_TFCAT_TIPCAP_WOW16, GUID_TFCAT_TIPCAP_IMMERSIVESUPPORT,
			GUID_TFCAT_TIPCAP_SYSTRAYSUPPORT, GUID_TFCAT_PROP_AUDIODATA, GUID_TFCAT_PROP_INKDATA,
			GUID_TFCAT_PROPSTYLE_CUSTOM, GUID_TFCAT_PROPSTYLE_STATIC, GUID_TFCAT_PROPSTYLE_STATICCOMPACT,
			GUID_TFCAT_DISPLAYATTRIBUTEPROVIDER,
			GUID_TFCAT_DISPLAYATTRIBUTEPROPERTY};
		for (const auto& category : required) {
			if (FAILED(hr)) break;
			hr = categories->RegisterCategory(clsid, category, clsid);
		}
	}
	if (categories) categories->Release();
	if (manager) manager->Release();
	if (profiles) profiles->Release();
	if (SUCCEEDED(initialized)) CoUninitialize();
	return hr;
}

static HRESULT registerFromPayload() {
	// Do not instantiate ImeModule here: its base constructor registers display
	// GUIDs, which would make the validation-only mode mutate the system.
	wchar_t root[MAX_PATH] = {};
	DWORD length = GetEnvironmentVariableW(L"MOQI_PROGRAM_DIR", root, _countof(root));
	if (length >= _countof(root)) return HRESULT_FROM_WIN32(ERROR_BAD_PATHNAME);
	if (!length) {
		DWORD bytes = sizeof(root);
		HKEY key = nullptr;
		LONG configured = RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"Software\\MoqiIM", 0,
			KEY_QUERY_VALUE | KEY_WOW64_64KEY, &key);
		if (configured == ERROR_SUCCESS) {
			configured = RegGetValueW(key, nullptr, L"InstallDir",
				RRF_RT_REG_SZ, nullptr, root, &bytes);
			RegCloseKey(key);
		}
		if (configured != ERROR_SUCCESS) {
			if (FAILED(SHGetFolderPathW(nullptr, CSIDL_PROGRAM_FILESX86, nullptr, 0, root))) return E_FAIL;
			if (FAILED(StringCchCatW(root, _countof(root), L"\\MoqiIM"))) return E_FAIL;
		}
	}
	if (PathIsRelativeW(root)) return E_INVALIDARG;
	std::ifstream backendFile(std::wstring(root) + L"\\backends.json", std::ios::binary);
	Json::Value backends;
	if (!backendFile) return HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
	backendFile >> backends;
	if (!backends.isArray() || backends.empty()) return E_INVALIDARG;
	std::vector<std::wstring> backendDirs;
	for (const auto& backend : backends) {
		if (!backend["name"].isString()) return E_INVALIDARG;
		auto name = utf8ToUtf16(backend["name"].asCString());
		if (name.empty() || name == L"." || name == L".." || name.find_first_of(L"\\\\/:\"") != std::wstring::npos) return E_INVALIDARG;
		backendDirs.push_back(name);
	}
	int iconIndex = 0; // use classic icon
	if(::IsWindows8OrGreater()) {
		iconIndex = 1; // use Windows 8 style IME icon
    }
	std::vector<Ime::LangProfileInfo> langProfiles;
	std::wstring dirPath;
	for (const auto& backendDir: backendDirs) {
		const size_t previousCount = langProfiles.size();
		std::string backendName = utf16ToUtf8(backendDir.c_str());
		dirPath = root;
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
			DWORD error = GetLastError();
			::FindClose(hFind);
			if (error != ERROR_NO_MORE_FILES) return HRESULT_FROM_WIN32(error);
		}
		if (langProfiles.size() == previousCount) return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
	}
	if (langProfiles.empty()) return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
	for (size_t i = 0; i < langProfiles.size(); ++i)
		for (size_t j = 0; j < i; ++j)
			if (IsEqualGUID(langProfiles[i].profileGuid, langProfiles[j].profileGuid)) return E_INVALIDARG;
	return registerMoqiProfiles(langProfiles);
}

STDAPI DllRegisterServer(void) try {
	return logRegistrationResult(L"register/enable/verify", registerFromPayload());
} catch (...) {
	// Never allow malformed JSON or allocation exceptions across the COM ABI.
	return logRegistrationResult(L"parse profile", HRESULT_FROM_WIN32(ERROR_INVALID_DATA));
}
