//
//	Copyright (C) 2013 Hong Jen Yee (PCMan) <pcman.tw@gmail.com>
//
//	This library is free software; you can redistribute it and/or
//	modify it under the terms of the GNU Library General Public
//	License as published by the Free Software Foundation; either
//	version 2 of the License, or (at your option) any later version.
//
//	This library is distributed in the hope that it will be useful,
//	but WITHOUT ANY WARRANTY; without even the implied warranty of
//	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
//	Library General Public License for more details.
//
//	You should have received a copy of the GNU Library General Public
//	License along with this library; if not, write to the
//	Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
//	Boston, MA  02110-1301, USA.
//

#include "MoqiTextService.h"
#include <assert.h>
#include <string>
#include <algorithm>
#include <libIME2/src/DebugLogConfig.h>
#include <libIME2/src/DebugLogFile.h>
#include <libIME2/src/ComPtr.h>
#include <libIME2/src/Utils.h>
#include <libIME2/src/LangBarButton.h>
#include "MoqiImeModule.h"
#include "resource.h"
#include <Shellapi.h>
#include <sys/stat.h>
#include <cwctype>
#include <fstream>
#include <sstream>

using namespace std;

namespace Moqi {

namespace {

std::wstring currentProcessPath();
std::wstring processBaseName(const std::wstring& imagePath);
std::wstring timestampNow();
std::wstring formatDebugLogLine(const std::wstring& message);
constexpr wchar_t kDefaultCommentFontFace[] = L"Consolas";
constexpr ULONGLONG kCandidateWindowMoveThrottleMs = 50;

// {3FCBE4CC-CC03-4BD4-B39F-3B6B0BEA5D90}
const GUID kToggleUiLessOverrideGuid = {
	0x3fcbe4cc, 0xcc03, 0x4bd4, { 0xb3, 0x9f, 0x3b, 0x6b, 0x0b, 0xea, 0x5d, 0x90 }
};

void appendCandidateWindowLog(const std::wstring& message) {
	if (!Ime::isTraceLoggingEnabled()) {
		return;
	}

	const wchar_t* localAppData = _wgetenv(L"LOCALAPPDATA");
	if (!localAppData || !*localAppData) {
		return;
	}

	std::wstring logDir = std::wstring(localAppData) + L"\\MoqiIM\\Log";
	std::wstring logPath = Ime::DebugLogFile::prepareDailyLogFilePath(
		logDir, L"candidate-window.log");
	if (logPath.empty()) {
		return;
	}

	std::wofstream stream(logPath, std::ios::app);
	if (!stream.is_open()) {
		return;
	}
	stream << formatDebugLogLine(message) << L"\n";
}

void appendTsfDebugLog(const std::wstring& message) {
	if (!Ime::isTraceLoggingEnabled()) {
		return;
	}

	const wchar_t* localAppData = _wgetenv(L"LOCALAPPDATA");
	if (!localAppData || !*localAppData) {
		return;
	}

	std::wstring logDir = std::wstring(localAppData) + L"\\MoqiIM\\Log";
	std::wstring logPath = Ime::DebugLogFile::prepareDailyLogFilePath(
		logDir, L"tsf-debug.log");
	if (logPath.empty()) {
		return;
	}

	std::wofstream stream(logPath, std::ios::app);
	if (!stream.is_open()) {
		return;
	}
	stream << message << L"\n";
}

std::wstring toLowerCopy(std::wstring value) {
	std::transform(value.begin(), value.end(), value.begin(), [](wchar_t ch) {
		return static_cast<wchar_t>(std::towlower(ch));
	});
	return value;
}

std::wstring currentProcessPath() {
	std::wstring buffer(MAX_PATH, L'\0');
	DWORD len = ::GetModuleFileNameW(nullptr, &buffer[0], static_cast<DWORD>(buffer.size()));
	if (len == 0) {
		return L"";
	}
	while (len >= buffer.size() - 1) {
		buffer.resize(buffer.size() * 2);
		len = ::GetModuleFileNameW(nullptr, &buffer[0], static_cast<DWORD>(buffer.size()));
		if (len == 0) {
			return L"";
		}
	}
	buffer.resize(len);
	return buffer;
}

std::wstring processBaseName(const std::wstring& imagePath) {
	const size_t pos = imagePath.find_last_of(L"\\/");
	return pos == std::wstring::npos ? imagePath : imagePath.substr(pos + 1);
}

std::wstring currentProcessName() {
	const std::wstring exeName = processBaseName(currentProcessPath());
	return exeName.empty() ? L"<unknown>" : exeName;
}

std::wstring processPathForPid(DWORD pid) {
	if (pid == 0) {
		return L"";
	}

	HANDLE processHandle = ::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
	if (!processHandle) {
		return L"";
	}

	std::wstring buffer(MAX_PATH, L'\0');
	DWORD len = static_cast<DWORD>(buffer.size());
	while (!::QueryFullProcessImageNameW(processHandle, 0, &buffer[0], &len)) {
		const DWORD error = ::GetLastError();
		if (error != ERROR_INSUFFICIENT_BUFFER) {
			::CloseHandle(processHandle);
			return L"";
		}
		buffer.resize(buffer.size() * 2);
		len = static_cast<DWORD>(buffer.size());
	}

	::CloseHandle(processHandle);
	buffer.resize(len);
	return buffer;
}

std::wstring boolText(bool value) {
	return value ? L"true" : L"false";
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

std::wstring formatDebugLogLine(const std::wstring& message) {
	std::wostringstream line;
	line << L"[" << timestampNow() << L"]"
	     << L"[pid=" << ::GetCurrentProcessId() << L"]"
	     << L"[tid=" << ::GetCurrentThreadId() << L"]"
	     << L"[exe=" << currentProcessName() << L"] "
	     << message;
	return line.str();
}

void logDebug(const std::wstring& message) {
	if (!Ime::isTraceLoggingEnabled()) {
		return;
	}
	const std::wstring formatted = formatDebugLogLine(message);
	::OutputDebugStringW((formatted + L"\n").c_str());
	appendTsfDebugLog(formatted);
}

std::wstring guidToString(REFGUID guid) {
	LPOLESTR buffer = nullptr;
	if (FAILED(::StringFromCLSID(guid, &buffer))) {
		return L"<guid-convert-failed>";
	}
	std::wstring result{buffer};
	::CoTaskMemFree(buffer);
	return result;
}

std::wstring foregroundWindowSummary() {
	HWND hwnd = ::GetForegroundWindow();
	DWORD pid = 0;
	const DWORD tid = hwnd ? ::GetWindowThreadProcessId(hwnd, &pid) : 0;
	const std::wstring fgExe = processBaseName(processPathForPid(pid));
	std::wostringstream stream;
	stream << L"fg_hwnd=0x" << std::hex << reinterpret_cast<UINT_PTR>(hwnd) << std::dec
	       << L" fg_pid=" << pid
	       << L" fg_exe=" << (fgExe.empty() ? L"<unknown>" : fgExe)
	       << L" fg_tid=" << tid;
	return stream.str();
}

std::wstring keyEventSummary(const Ime::KeyEvent& keyEvent) {
	std::wostringstream stream;
	stream << L"vk=" << keyEvent.keyCode()
	       << L" char=" << keyEvent.charCode()
	       << L" scan=" << static_cast<unsigned int>(keyEvent.scanCode())
	       << L" ext=" << boolText(keyEvent.isExtended())
	       << L" repeat=" << keyEvent.repeatCount();
	return stream.str();
}

bool shouldForceUiLessForProcess(const std::wstring& imagePath) {
	const std::wstring lowerPath = toLowerCopy(imagePath);
	const std::wstring lowerBaseName = processBaseName(lowerPath);
	if (lowerBaseName == L"war3.exe" || lowerBaseName == L"minecraft.windows.exe") {
		return true;
	}
	if ((lowerBaseName == L"javaw.exe" || lowerBaseName == L"java.exe") &&
		(lowerPath.find(L"minecraft") != std::wstring::npos ||
		 lowerPath.find(L".minecraft") != std::wstring::npos ||
		 lowerPath.find(L"mojang") != std::wstring::npos)) {
		return true;
	}
	return false;
}

bool shouldEnableDummyAnchorCompatForProcess(const std::wstring& imagePath) {
	const std::wstring lowerBaseName = processBaseName(toLowerCopy(imagePath));
	return lowerBaseName == L"steamwebhelper.exe" ||
	       lowerBaseName == L"steam.exe" ||
	       lowerBaseName == L"gameoverlayui.exe" ||
	       lowerBaseName == L"cs2.exe" ||
	       lowerBaseName == L"csgo.exe";
}

bool shouldDisableInlinePreeditForProcess(const std::wstring& imagePath) {
	const std::wstring lowerBaseName = processBaseName(toLowerCopy(imagePath));
	return lowerBaseName == L"cs2.exe" || lowerBaseName == L"csgo.exe";
}

bool shouldDisableTsfCandidateUiForProcess(const std::wstring& imagePath) {
	const std::wstring lowerBaseName = processBaseName(toLowerCopy(imagePath));
	return lowerBaseName == L"cs2.exe" || lowerBaseName == L"csgo.exe";
}

}

TextService::TextService(ImeModule* module):
	Ime::TextService(module),
	client_(nullptr),
	messageWindow_(nullptr),
	messageTimerId_(0),
	validCandidateListElementId_(false),
	candidateListElementId_(0),
	shouldShowCandidateWindowUI_(true),
	manualUiLessOverride_(false),
	autoUiLessOverride_(shouldForceUiLessForProcess(currentProcessPath())),
	autoDummyAnchorCompat_(shouldEnableDummyAnchorCompatForProcess(currentProcessPath())),
	autoInlinePreeditDisabled_(shouldDisableInlinePreeditForProcess(currentProcessPath())),
	autoDisableTsfCandidateUi_(shouldDisableTsfCandidateUiForProcess(currentProcessPath())),
	candidateWindow_(nullptr),
	showingCandidates_(false),
	pendingCandidateRecovery_(false),
	hasAppliedCandidateContent_(false),
	hasAppliedCandidateCursor_(false),
	appliedCandidateCursor_(0),
	hasLastCandidateWindowPos_(false),
	lastCandidateWindowPos_{0, 0},
	lastCandidateWindowMoveTick_(0),
	updateFont_(false),
	candPerRow_(1),
	candSpacing_(20),
	selKeys_(L"1234567890"),
	candUseCursor_(true),
	candCommentFontName_(kDefaultCommentFontFace),
	candFontSize_(20),
	candCommentFontSize_(18),
	candBackgroundColor_(RGB(255, 255, 255)),
	candHighlightColor_(RGB(198, 221, 249)),
	candTextColor_(RGB(0, 0, 0)),
	candHighlightTextColor_(RGB(0, 0, 0)),
	candCommentColor_(RGB(0, 0, 0)),
	candCommentHighlightColor_(RGB(0, 0, 0)),
	inlinePreedit_(true),
	autoPairQuotes_(false),
	suppressNextCompositionTerminatedNotification_(false),
	candidatePreeditCursor_(0) {
	addPreservedKey('P', TF_MOD_CONTROL | TF_MOD_SHIFT, kToggleUiLessOverrideGuid);
	shouldShowCandidateWindowUI_ = !effectiveUiLess();

	// font for candidate and mesasge windows
	font_ = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
	LOGFONT lf;
	GetObject(font_, sizeof(lf), &lf);
	lf.lfHeight = candFontHeight(); // FIXME: make this configurable
	lf.lfWeight = FW_NORMAL;
	font_ = CreateFontIndirect(&lf);
	lf.lfHeight = candCommentFontHeight();
	const std::wstring& commentFontName =
		candCommentFontName_.empty() ? std::wstring(kDefaultCommentFontFace) : candCommentFontName_;
	wcsncpy_s(lf.lfFaceName, _countof(lf.lfFaceName), commentFontName.c_str(), _TRUNCATE);
	commentFont_ = CreateFontIndirect(&lf);
}

TextService::~TextService(void) {
	if (client_) {
		closeClient();
	}

	if(popupMenu_)
		::DestroyMenu(popupMenu_);

	destroyCandidateWindow();

	if(messageWindow_)
		hideMessage();

	if(font_)
		::DeleteObject(font_);
	if(commentFont_)
		::DeleteObject(commentFont_);
}

// virtual
void TextService::onActivate() {
	std::wostringstream log;
	log << L"[onActivate] exe=" << processBaseName(currentProcessPath())
	    << L" flags=0x" << std::hex << activateFlags() << std::dec
	    << L" keyboard_open=" << boolText(isKeyboardOpened())
	    << L" is_ui_less=" << boolText(isUiLess())
	    << L" effective_ui_less=" << boolText(effectiveUiLess())
	    << L" auto_ui_less=" << boolText(autoUiLessOverride_)
	    << L" inline_preedit=" << boolText(inlinePreedit_)
	    << L" effective_inline_preedit=" << boolText(effectiveInlinePreedit())
	    << L" auto_inline_preedit_disabled=" << boolText(autoInlinePreeditDisabled_)
	    << L" auto_disable_tsf_candidate_ui=" << boolText(autoDisableTsfCandidateUi_)
	    << L" dummy_anchor_compat=" << boolText(autoDummyAnchorCompat_)
	    << L" manual_ui_less=" << boolText(manualUiLessOverride_)
	    << L" " << foregroundWindowSummary();
	logDebug(log.str());
	// Since we support multiple language profiles in this text service,
	// we do nothing when the whole text service is activated.
	// Instead, we do the actual initilization for each language profile when it is activated.
	// In Moqi, we create different client connections for different language profiles.
}

// virtual
void TextService::onDeactivate() {
	logDebug(L"[onDeactivate] exe=" + processBaseName(currentProcessPath()) + L" " + foregroundWindowSummary());
	if(client_) {
		closeClient();
	}
}

// virtual
void TextService::onFocus() {
}

// virtual
void TextService::onSetFocus() {
	std::wostringstream log;
	log << L"[onSetFocus] exe=" << processBaseName(currentProcessPath())
		<< L" showing_candidates=" << boolText(showingCandidates_)
		<< L" pending_candidate_recovery=" << boolText(pendingCandidateRecovery_)
		<< L" " << foregroundWindowSummary();
	logDebug(log.str());
}

// virtual
void TextService::onKillFocus() {
	std::wostringstream log;
	log << L"[onKillFocus] exe=" << processBaseName(currentProcessPath())
		<< L" showing_candidates=" << boolText(showingCandidates_)
		<< L" pending_candidate_recovery=" << boolText(pendingCandidateRecovery_)
		<< L" " << foregroundWindowSummary();
	logDebug(log.str());
}

// virtual
void TextService::onSetThreadFocus() {
	std::wostringstream log;
	log << L"[onSetThreadFocus] exe=" << processBaseName(currentProcessPath())
	    << L" keyboard_open=" << boolText(isKeyboardOpened())
	    << L" effective_ui_less=" << boolText(effectiveUiLess())
	    << L" showing_candidates=" << boolText(showingCandidates_)
	    << L" pending_candidate_recovery=" << boolText(pendingCandidateRecovery_)
	    << L" " << foregroundWindowSummary();
	logDebug(log.str());
}

// virtual
void TextService::onKillThreadFocus() {
	std::wostringstream log;
	log << L"[onKillThreadFocus] exe=" << processBaseName(currentProcessPath())
		<< L" showing_candidates=" << boolText(showingCandidates_)
		<< L" pending_candidate_recovery=" << boolText(pendingCandidateRecovery_)
		<< L" " << foregroundWindowSummary();
	logDebug(log.str());
}

// virtual
bool TextService::filterKeyDown(Ime::KeyEvent& keyEvent) {
	logDebug(L"[filterKeyDown] " + keyEventSummary(keyEvent) +
		L" client=" + boolText(client_ != nullptr) +
		L" keyboard_open=" + boolText(isKeyboardOpened()) +
		L" effective_ui_less=" + boolText(effectiveUiLess()));
	// if (keyEvent.isKeyToggled(VK_CAPITAL))
	//	return true;
	if(!client_)
		return false;
	return client_->filterKeyDown(keyEvent);
}

// virtual
bool TextService::onKeyDown(Ime::KeyEvent& keyEvent, Ime::EditSession* session) {
	logDebug(L"[onKeyDown] " + keyEventSummary(keyEvent) +
		L" client=" + boolText(client_ != nullptr));
	//if (keyEvent.isKeyToggled(VK_CAPITAL))
	//	return true;
	if (!client_)
		return false;
	return client_->onKeyDown(keyEvent, session);
}

// virtual
bool TextService::filterKeyUp(Ime::KeyEvent& keyEvent) {
	logDebug(L"[filterKeyUp] " + keyEventSummary(keyEvent) +
		L" client=" + boolText(client_ != nullptr));
	if(!client_)
		return false;
	return client_->filterKeyUp(keyEvent);
}

// virtual
bool TextService::onKeyUp(Ime::KeyEvent& keyEvent, Ime::EditSession* session) {
	logDebug(L"[onKeyUp] " + keyEventSummary(keyEvent) +
		L" client=" + boolText(client_ != nullptr));
	if(!client_)
		return false;
	return client_->onKeyUp(keyEvent, session);
}

bool TextService::highlightCandidate(int index) {
	if (!client_)
		return false;
	return client_->highlightCandidate(index);
}

bool TextService::selectCandidate(int index) {
	if (!client_)
		return false;
	return client_->selectCandidate(index);
}

bool TextService::changeCandidatePage(bool backward) {
	if (!client_)
		return false;
	return client_->changePage(backward);
}

// virtual
bool TextService::onPreservedKey(const GUID& guid) {
	if (::IsEqualGUID(guid, kToggleUiLessOverrideGuid)) {
		manualUiLessOverride_ = !manualUiLessOverride_;
		applyUiLessOverrideState();
		logDebug(L"[onPreservedKey] toggle_uiless manual_ui_less=" + boolText(manualUiLessOverride_));
		return true;
	}
	if(!client_)
		return false;
	// some preserved keys registered in ctor are pressed
	return client_->onPreservedKey(guid);
}


// virtual
bool TextService::onCommand(UINT id, CommandType type) {
	if(!client_)
		return false;
	return client_->onCommand(id, type);
}


// called when a language bar button needs a menu
// virtual
bool TextService::onMenu(LangBarButton* btn, ITfMenu* pMenu) {
	if (client_ != nullptr) {
		return client_->onMenu(btn, pMenu);
	}
	return false;
}

// called when a language bar button needs a menu
// virtual
HMENU TextService::onMenu(LangBarButton* btn) {
	if (client_ != nullptr) {
		return client_->onMenu(btn);
	}
	return NULL;
}


// virtual
void TextService::onCompartmentChanged(const GUID& key) {
	Ime::TextService::onCompartmentChanged(key);
	if(client_)
		client_->onCompartmentChanged(key);
}

// called when the keyboard is opened or closed
// virtual
void TextService::onKeyboardStatusChanged(bool opened) {
	Ime::TextService::onKeyboardStatusChanged(opened);
	if(client_)
		client_->onKeyboardStatusChanged(opened);
	if(opened) { // keyboard is opened
	}
	else { // keyboard is closed
		if(isComposing()) {
			// end current composition if needed
			if(auto context = currentContext()) {
				endComposition(context);
			}
		}
		if(showingCandidates()) // disable candidate window if it's opened
			hideCandidates();
		hideMessage(); // hide message window, if there's any
	}
}

// called just before current composition is terminated for doing cleanup.
// if forced is true, the composition is terminated by others, such as
// the input focus is grabbed by another application.
// if forced is false, the composition is terminated gracefully by endComposition().
// virtual
void TextService::onCompositionTerminated(bool forced) {
	// we do special handling here for forced composition termination.
	if (!forced && suppressNextCompositionTerminatedNotification_) {
		suppressNextCompositionTerminatedNotification_ = false;
		return;
	}
	if(forced) {
		const bool shouldPreserveCandidateRecovery =
			showingCandidates_ || !candidatePreedit_.empty() || !candidates_.empty();
		std::wostringstream log;
		log << L"[onCompositionTerminated] forced=true"
			<< L" showing_candidates=" << boolText(showingCandidates_)
			<< L" candidate_count=" << candidates_.size()
			<< L" preedit_len=" << candidatePreedit_.length()
			<< L" preserve_candidate_recovery=" << boolText(shouldPreserveCandidateRecovery);
		logDebug(log.str());
		// we're still editing our composition and have something in the preedit buffer.
		// however, some other applications grabs the focus and force us to terminate
		// our composition.
		if (showingCandidates()) // disable candidate window if it's opened
			hideCandidates(shouldPreserveCandidateRecovery);
		hideMessage(); // hide message window, if there's any
	}
	else {
		logDebug(L"[onCompositionTerminated] forced=false");
	}
	if(client_)
		client_->onCompositionTerminated(forced);
}

void TextService::onLangProfileActivated(REFIID lang) {
	logDebug(L"[onLangProfileActivated] profile=" + guidToString(lang) +
		L" exe=" + processBaseName(currentProcessPath()) +
		L" " + foregroundWindowSummary());
	// Sometimes, Windows does not deactivate the old language profile before
	// activating the new one. So here we do it by ourselves.
	// If a new profile is activated, but there is an old one remaining active,
	// deactive it first.
	if (client_ != nullptr)
		closeClient();

	// create a new client connection to the input method server for the language profile
	client_ = std::make_unique<Client>(this, lang);
	client_->onActivate();
}

void TextService::onLangProfileDeactivated(REFIID lang) {
	logDebug(L"[onLangProfileDeactivated] profile=" + guidToString(lang) +
		L" exe=" + processBaseName(currentProcessPath()) +
		L" " + foregroundWindowSummary());
	closeClient();
}

bool TextService::ensureCandidateWindowValid(const wchar_t* reason) {
	if (!candidateWindow_) {
		return false;
	}
	if (candidateWindow_->isWindow()) {
		return true;
	}

	std::wostringstream log;
	log << L"[TextService::" << reason << L"] candidate window hwnd invalid; recreating";
	appendCandidateWindowLog(log.str());

	if (validCandidateListElementId_) {
		auto elementMgr = Ime::ComPtr<ITfUIElementMgr>::queryFrom(threadMgr());
		if (elementMgr) {
			elementMgr->EndUIElement(candidateListElementId_);
		}
		candidateListElementId_ = 0;
		validCandidateListElementId_ = false;
	}

	candidateWindow_ = nullptr;
	invalidateCandidateUiCache();
	return false;
}

void TextService::createCandidateWindow(Ime::EditSession* session) {
	if (!tsfCandidateUiEnabled()) {
		appendCandidateWindowLog(L"[TextService::createCandidateWindow] skipped by process policy");
		return;
	}
	ensureCandidateWindowValid(L"createCandidateWindow");
	if (!candidateWindow_) {
		appendCandidateWindowLog(L"[TextService::createCandidateWindow] creating");
		shouldShowCandidateWindowUI_ = !effectiveUiLess();
		candidateWindow_ = new Moqi::CandidateWindow(this, session); // assigning to smart ptr also inrease ref count
		candidateWindow_->Release();  // decrease ref count caused by new

		candidateWindow_->setFont(font_);
		candidateWindow_->setCommentFont(commentFont_);
		candidateWindow_->setBackgroundColor(candBackgroundColor_);
		candidateWindow_->setHighlightColor(candHighlightColor_);
		candidateWindow_->setTextColor(candTextColor_);
		candidateWindow_->setHighlightTextColor(candHighlightTextColor_);
		candidateWindow_->setPreeditText(effectiveInlinePreedit() ? L"" : candidatePreedit_);
		candidateWindow_->setPreeditCursor(effectiveInlinePreedit() ? 0 : candidatePreeditCursor_);
		auto elementMgr = Ime::ComPtr<ITfUIElementMgr>::queryFrom(threadMgr());
		if (elementMgr) {
			BOOL pbShow = shouldShowCandidateWindowUI_ ? TRUE : FALSE;
			if (validCandidateListElementId_ =
				(elementMgr->BeginUIElement(candidateWindow_, &pbShow, &candidateListElementId_) == S_OK)) {
				shouldShowCandidateWindowUI_ = !effectiveUiLess() && pbShow != FALSE;
				std::wostringstream log;
				log << L"[TextService::createCandidateWindow] BeginUIElement success pbShow=" << pbShow
					<< L" elementId=" << candidateListElementId_;
				appendCandidateWindowLog(log.str());
			}
			else {
				appendCandidateWindowLog(L"[TextService::createCandidateWindow] BeginUIElement failed");
			}
		}
		else {
			appendCandidateWindowLog(L"[TextService::createCandidateWindow] elementMgr unavailable");
		}
		candidateWindow_->Show(shouldShowCandidateWindowUI_ ? TRUE : FALSE);
		if (!shouldShowCandidateWindowUI_) {
			appendCandidateWindowLog(L"[TextService::createCandidateWindow] candidate window suppressed by UI-less host");
		}
	}
}

void TextService::destroyCandidateWindow() {
	if (validCandidateListElementId_) {
		auto elementMgr = Ime::ComPtr<ITfUIElementMgr>::queryFrom(threadMgr());
		if (elementMgr) {
			elementMgr->EndUIElement(candidateListElementId_);
		}
		candidateListElementId_ = 0;
		validCandidateListElementId_ = false;
	}
	if (candidateWindow_) {
		candidateWindow_->setPreeditText(L"");
		candidateWindow_->Show(FALSE);
		candidateWindow_ = nullptr;
		appendCandidateWindowLog(L"[TextService::destroyCandidateWindow] destroyed");
	}
	showingCandidates_ = false;
	pendingCandidateRecovery_ = false;
	candidatePreedit_.clear();
	candidatePreeditCursor_ = 0;
	invalidateCandidateUiCache();
}

void TextService::updateCandidates(Ime::EditSession* session) {
	if (!tsfCandidateUiEnabled()) {
		appendCandidateWindowLog(L"[TextService::updateCandidates] skipped by process policy");
		return;
	}
	createCandidateWindow(session);
	if (!candidateWindow_) {
		return;
	}
	candidateWindow_->syncOwner(session);

	applyCandidateAppearanceNow();

	const std::wstring renderedPreedit = effectiveInlinePreedit() ? L"" : candidatePreedit_;
	const bool contentChanged = !isCandidateContentApplied(renderedPreedit);
	if (contentChanged) {
		candidateWindow_->clear();
		candidateWindow_->setUseCursor(candUseCursor_);
		candidateWindow_->setCandPerRow(candPerRow_);
		candidateWindow_->setCandSpacing(candSpacing_);
		candidateWindow_->setBackgroundColor(candBackgroundColor_);
		candidateWindow_->setHighlightColor(candHighlightColor_);
		candidateWindow_->setTextColor(candTextColor_);
		candidateWindow_->setHighlightTextColor(candHighlightTextColor_);
		candidateWindow_->setCommentColor(candCommentColor_);
		candidateWindow_->setCommentHighlightColor(candCommentHighlightColor_);
		candidateWindow_->setPreeditText(renderedPreedit);
		candidateWindow_->setPreeditCursor(effectiveInlinePreedit() ? 0 : candidatePreeditCursor_);

		// the items in the candidate list should not exist the
		// number of available keys used to select them.
		assert(candidates_.size() <= selKeys_.size());
		for (int i = 0; i < candidates_.size(); ++i) {
			candidateWindow_->add(candidates_[i], selKeys_[i]);
		}
		candidateWindow_->recalculateSize();
		candidateWindow_->refresh();
		markCandidateContentApplied(renderedPreedit);
		hasAppliedCandidateCursor_ = false;
	}

	moveCandidateWindowToInputRect(session, L"updateCandidates", true);

	if (showingCandidates_) {
		candidateWindow_->Show(shouldShowCandidateWindowUI_ ? TRUE : FALSE);
		std::wostringstream log;
		log << L"[TextService::updateCandidates] ensured visibility should_show_ui="
			<< boolText(shouldShowCandidateWindowUI_);
		appendCandidateWindowLog(log.str());
	}

	if (contentChanged && validCandidateListElementId_) {
		auto elementMgr = Ime::ComPtr<ITfUIElementMgr>::queryFrom(threadMgr());
		if (elementMgr) {
			elementMgr->UpdateUIElement(candidateListElementId_);
		}
	}
}

void TextService::updateCandidatesWindow(Ime::EditSession* session) {
    if (!tsfCandidateUiEnabled()) {
        return;
    }
    ensureCandidateWindowValid(L"updateCandidatesWindow");
    if (candidateWindow_) {
        candidateWindow_->syncOwner(session);
		moveCandidateWindowToInputRect(session, L"updateCandidatesWindow", true);
    }
}

void TextService::refreshCandidates() {
	if (!tsfCandidateUiEnabled()) {
		return;
	}
	if (validCandidateListElementId_) {
		auto elementMgr = Ime::ComPtr<ITfUIElementMgr>::queryFrom(threadMgr());
		if (elementMgr) {
			elementMgr->UpdateUIElement(candidateListElementId_);
		}
	}
}

bool TextService::setCandidateCursor(int cursor) {
	if (!tsfCandidateUiEnabled()) {
		return false;
	}
	if (candidateWindow_) {
		if (hasAppliedCandidateCursor_ && appliedCandidateCursor_ == cursor) {
			return false;
		}
		candidateWindow_->setCurrentSel(cursor);
		appliedCandidateCursor_ = cursor;
		hasAppliedCandidateCursor_ = true;
		return true;
	}
	return false;
}

void TextService::invalidateCandidateUiCache() {
	appliedCandidates_.clear();
	appliedSelKeys_.clear();
	appliedCandidatePreedit_.clear();
	hasAppliedCandidateContent_ = false;
	hasAppliedCandidateCursor_ = false;
	appliedCandidateCursor_ = 0;
	hasLastCandidateWindowPos_ = false;
	lastCandidateWindowPos_ = {0, 0};
	lastCandidateWindowMoveTick_ = 0;
}

bool TextService::isCandidateContentApplied(const std::wstring& renderedPreedit) const {
	return hasAppliedCandidateContent_ &&
		appliedCandidatePreedit_ == renderedPreedit &&
		appliedCandidates_ == candidates_ &&
		appliedSelKeys_ == selKeys_;
}

void TextService::markCandidateContentApplied(const std::wstring& renderedPreedit) {
	appliedCandidatePreedit_ = renderedPreedit;
	appliedCandidates_ = candidates_;
	appliedSelKeys_ = selKeys_;
	hasAppliedCandidateContent_ = true;
}

bool TextService::moveCandidateWindowToInputRect(Ime::EditSession* session, const wchar_t* reason, bool throttleSamePosition) {
	if (!candidateWindow_) {
		return false;
	}

	RECT textRect;
	// get the position of composition area from TSF
	if (!inputRect(session, &textRect)) {
		std::wstring tag = L"[TextService::";
		tag += reason;
		tag += L"] inputRect unavailable";
		appendCandidateWindowLog(tag);
		return false;
	}

	const POINT nextPos{textRect.left, textRect.bottom};
	const ULONGLONG now = ::GetTickCount64();
	if (throttleSamePosition && hasLastCandidateWindowPos_ &&
		lastCandidateWindowPos_.x == nextPos.x &&
		lastCandidateWindowPos_.y == nextPos.y &&
		now - lastCandidateWindowMoveTick_ < kCandidateWindowMoveThrottleMs) {
		return false;
	}

	// FIXME: where should we put the candidate window?
	candidateWindow_->move(nextPos.x, nextPos.y);
	hasLastCandidateWindowPos_ = true;
	lastCandidateWindowPos_ = nextPos;
	lastCandidateWindowMoveTick_ = now;

	std::wostringstream log;
	log << L"[TextService::" << reason << L"] moved left=" << nextPos.x
		<< L" bottom=" << nextPos.y;
	appendCandidateWindowLog(log.str());
	return true;
}

// show candidate list window
void TextService::showCandidates(Ime::EditSession* session) {
	if (!tsfCandidateUiEnabled()) {
		showingCandidates_ = true;
		pendingCandidateRecovery_ = false;
		appendCandidateWindowLog(L"[TextService::showCandidates] skipped by process policy");
		return;
	}
	// NOTE: in Windows 8 store apps, candidate window should be owned by
	// composition window, which can be returned by TextService::compositionWindow().
	// Otherwise, the candidate window cannot be shown.
	// Ime::CandidateWindow handles this internally. If you create your own
	// candidate window, you need to call TextService::isImmersive() to check
	// if we're in a Windows store app. If isImmersive() returns true,
	// The candidate window created should be a child window of the composition window.
	// Please see Ime::CandidateWindow::CandidateWindow() for an example.
	createCandidateWindow(session);
	if (candidateWindow_) {
		candidateWindow_->syncOwner(session);
		candidateWindow_->Show(shouldShowCandidateWindowUI_ ? TRUE : FALSE);
	}
	showingCandidates_ = true;
	pendingCandidateRecovery_ = false;
	appendCandidateWindowLog(L"[TextService::showCandidates] shown");
}

// hide candidate list window
void TextService::hideCandidates(bool preserveRecoveryState) {
	if (!tsfCandidateUiEnabled()) {
		showingCandidates_ = false;
		pendingCandidateRecovery_ = preserveRecoveryState;
		appendCandidateWindowLog(L"[TextService::hideCandidates] skipped by process policy");
		return;
	}
	if (ensureCandidateWindowValid(L"hideCandidates")) {
		candidateWindow_->setPreeditText(L"");
		candidateWindow_->Show(FALSE);
		candidateWindow_->clear();
	}
	invalidateCandidateUiCache();
	showingCandidates_ = false;
	pendingCandidateRecovery_ = preserveRecoveryState;
	std::wostringstream log;
	log << L"[TextService::hideCandidates] hidden preserve_recovery="
		<< boolText(preserveRecoveryState);
	appendCandidateWindowLog(log.str());
}

// message window
void TextService::showMessage(Ime::EditSession* session, std::wstring message, int duration) {
	if (effectiveUiLess()) {
		hideMessage();
		return;
	}

	// remove previous message if there's any
	hideMessage();
	// FIXME: reuse the window whenever possible
	messageWindow_ = make_unique<Ime::MessageWindow>(this, session);
	messageWindow_->setFont(font_);
	messageWindow_->setText(message);
	
	int x = 0, y = 0;
	if(isComposing()) {
		RECT rc;
		if(inputRect(session, &rc)) {
			x = rc.left;
			y = rc.bottom;
		}
	}
	messageWindow_->move(x, y);
	messageWindow_->show();

	messageTimerId_ = ::SetTimer(messageWindow_->hwnd(), 1, duration * 1000, (TIMERPROC)TextService::onMessageTimeout);
}

void TextService::updateMessageWindow(Ime::EditSession* session) {
    if (messageWindow_) {
        RECT textRect;
        // get the position of composition area from TSF
        if (inputRect(session, &textRect)) {
            // FIXME: where should we put the message window?
            messageWindow_->move(textRect.left, textRect.bottom);
        }
    }
}

void TextService::hideMessage() {
	if(messageTimerId_) {
		::KillTimer(messageWindow_->hwnd(), messageTimerId_);
		messageTimerId_ = 0;
	}
	if(messageWindow_) {
		messageWindow_ = nullptr;
	}
}

// called when the message window timeout
void TextService::onMessageTimeout() {
	hideMessage();
}

// static
void CALLBACK TextService::onMessageTimeout(HWND hwnd, UINT msg, UINT_PTR id, DWORD time) {
	Ime::MessageWindow* messageWindow = (Ime::MessageWindow*)Ime::Window::fromHwnd(hwnd);
	assert(messageWindow);
	if(messageWindow) {
		TextService* pThis = (Moqi::TextService*)messageWindow->textService();
		pThis->onMessageTimeout();
	}
}


void TextService::updateLangButtons() {
}

int TextService::candFontHeight() {
	int candFontHeight_ = candFontSize_;
	HDC hdc = GetDC(NULL);
	if (hdc)
	{
		// Match fcitx5-windows: treat configured size as px at 96 DPI.
		candFontHeight_ = -MulDiv(candFontSize_, GetDeviceCaps(hdc, LOGPIXELSY), 96);
		ReleaseDC(NULL, hdc);
	}
	return candFontHeight_;
}

int TextService::candCommentFontHeight() {
	int candFontHeight_ = candCommentFontSize_;
	HDC hdc = GetDC(NULL);
	if (hdc)
	{
		candFontHeight_ = -MulDiv(candCommentFontSize_, GetDeviceCaps(hdc, LOGPIXELSY), 96);
		ReleaseDC(NULL, hdc);
	}
	return candFontHeight_;
}

void TextService::applyCandidateAppearanceNow() {
	if (!updateFont_) {
		return;
	}

	HFONT baseFont = font_;
	if (!baseFont) {
		baseFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
	}

	LOGFONT lf{};
	GetObject(baseFont, sizeof(lf), &lf);
	if (font_) {
		::DeleteObject(font_);
		font_ = nullptr;
	}
	if (commentFont_) {
		::DeleteObject(commentFont_);
		commentFont_ = nullptr;
	}

	lf.lfHeight = candFontHeight();
	lf.lfWeight = FW_NORMAL;
	if (!candFontName_.empty()) {
		wcsncpy_s(lf.lfFaceName, _countof(lf.lfFaceName), candFontName_.c_str(), _TRUNCATE);
	}
	font_ = CreateFontIndirect(&lf);

	lf.lfHeight = candCommentFontHeight();
	const std::wstring& commentFontName =
		candCommentFontName_.empty() ? std::wstring(kDefaultCommentFontFace) : candCommentFontName_;
	wcsncpy_s(lf.lfFaceName, _countof(lf.lfFaceName), commentFontName.c_str(), _TRUNCATE);
	commentFont_ = CreateFontIndirect(&lf);
	updateFont_ = false;

	if (messageWindow_) {
		messageWindow_->setFont(font_);
	}
	if (candidateWindow_) {
		candidateWindow_->setFont(font_);
		candidateWindow_->setCommentFont(commentFont_);
	}
	refreshCandidateAppearance();
}

void TextService::refreshCandidateAppearance() {
	if (!candidateWindow_) {
		return;
	}
	candidateWindow_->setUseCursor(candUseCursor_);
	candidateWindow_->setCandPerRow(candPerRow_);
	candidateWindow_->setCandSpacing(candSpacing_);
	candidateWindow_->setBackgroundColor(candBackgroundColor_);
	candidateWindow_->setHighlightColor(candHighlightColor_);
	candidateWindow_->setTextColor(candTextColor_);
	candidateWindow_->setHighlightTextColor(candHighlightTextColor_);
	candidateWindow_->setPreeditText(effectiveInlinePreedit() ? L"" : candidatePreedit_);
	candidateWindow_->setPreeditCursor(effectiveInlinePreedit() ? 0 : candidatePreeditCursor_);
	candidateWindow_->recalculateSize();
	candidateWindow_->refresh();
	refreshCandidates();
}

void TextService::applyUiLessOverrideState() {
	shouldShowCandidateWindowUI_ = !effectiveUiLess();
	if (!tsfCandidateUiEnabled()) {
		destroyCandidateWindow();
		hideMessage();
		return;
	}
	if (candidateWindow_) {
		candidateWindow_->setPreeditText(effectiveInlinePreedit() ? L"" : candidatePreedit_);
		candidateWindow_->setPreeditCursor(effectiveInlinePreedit() ? 0 : candidatePreeditCursor_);
		candidateWindow_->Show(shouldShowCandidateWindowUI_ ? TRUE : FALSE);
	}
	if (effectiveUiLess()) {
		hideMessage();
	}
	refreshCandidates();
}

void TextService::closeClient() {
	// deactive currently active language profile
	if (client_) {
		// disconnect from the server
		client_->onDeactivate();
		client_ = nullptr;
		// detroy UI resources
		hideMessage();
		destroyCandidateWindow();
	}
}

} // namespace Moqi
