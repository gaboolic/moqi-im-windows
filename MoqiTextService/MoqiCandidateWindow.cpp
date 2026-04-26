//
//    Copyright (C) 2026
//

#include "MoqiCandidateWindow.h"
#include "MoqiTextService.h"

#include <LibIME2/src/DebugLogConfig.h>
#include <LibIME2/src/DebugLogFile.h>
#include <LibIME2/src/DrawUtils.h>
#include <LibIME2/src/EditSession.h>
#include <LibIME2/src/TextService.h>
#include <windowsx.h>

#include <algorithm>
#include <cassert>
#include <fstream>
#include <sstream>

namespace {

constexpr COLORREF kWindowBackground = RGB(255, 255, 255);
constexpr COLORREF kWindowBorder = RGB(150, 150, 150);
constexpr COLORREF kDividerColor = RGB(220, 220, 220);
constexpr COLORREF kItemText = RGB(0, 0, 0);
constexpr COLORREF kSelectedBackground = RGB(198, 221, 249);
constexpr COLORREF kSelectedText = RGB(0, 0, 0);
constexpr int kDefaultCandidateSpacing = 20;

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

std::wstring formatCandidateWindowLogLine(const std::wstring& message) {
    const std::wstring exeName = processBaseName(currentProcessPath());
    std::wostringstream line;
    line << L"[" << timestampNow() << L"]"
         << L"[pid=" << ::GetCurrentProcessId() << L"]"
         << L"[tid=" << ::GetCurrentThreadId() << L"]"
         << L"[exe=" << (exeName.empty() ? L"<unknown>" : exeName) << L"] "
         << message;
    return line.str();
}

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
    stream << formatCandidateWindowLogLine(message) << L"\n";
}

std::wstring hwndToString(HWND hwnd) {
    std::wostringstream stream;
    stream << L"0x" << std::hex << reinterpret_cast<UINT_PTR>(hwnd) << std::dec;
    return stream.str();
}

void logCandidateWindowState(const std::wstring& tag, HWND hwnd) {
    if (!hwnd) {
        appendCandidateWindowLog(tag + L" hwnd=<null>");
        return;
    }

    RECT rect{};
    ::GetWindowRect(hwnd, &rect);
    const DWORD style = static_cast<DWORD>(::GetWindowLongPtr(hwnd, GWL_STYLE));
    const DWORD exStyle = static_cast<DWORD>(::GetWindowLongPtr(hwnd, GWL_EXSTYLE));
    const HWND owner = reinterpret_cast<HWND>(::GetWindowLongPtr(hwnd, GWLP_HWNDPARENT));
    const HWND gwOwner = ::GetWindow(hwnd, GW_OWNER);
    const HWND parent = ::GetParent(hwnd);
    const HWND root = ::GetAncestor(hwnd, GA_ROOT);
    const HWND rootOwner = ::GetAncestor(hwnd, GA_ROOTOWNER);

    std::wostringstream log;
    log << tag
        << L" hwnd=" << hwndToString(hwnd)
        << L" visible=" << (::IsWindowVisible(hwnd) ? L"true" : L"false")
        << L" owner=" << hwndToString(owner)
        << L" gw_owner=" << hwndToString(gwOwner)
        << L" parent=" << hwndToString(parent)
        << L" root=" << hwndToString(root)
        << L" root_owner=" << hwndToString(rootOwner)
        << L" style=0x" << std::hex << style
        << L" exstyle=0x" << exStyle << std::dec
        << L" rect=(" << rect.left << L"," << rect.top << L"," << rect.right << L"," << rect.bottom << L")";
    appendCandidateWindowLog(log.str());
}

HWND normalizeCandidateOwnerWindow(HWND hwnd, bool immersive, const wchar_t* reason) {
    if (hwnd == nullptr) {
        return nullptr;
    }

    const HWND root = ::GetAncestor(hwnd, GA_ROOT);
    const HWND rootOwner = ::GetAncestor(hwnd, GA_ROOTOWNER);
    const HWND normalized = immersive ? hwnd : (root != nullptr ? root : hwnd);

    std::wostringstream log;
    log << L"[normalizeCandidateOwnerWindow] reason=" << reason
        << L" immersive=" << (immersive ? L"true" : L"false")
        << L" raw=" << hwndToString(hwnd)
        << L" root=" << hwndToString(root)
        << L" root_owner=" << hwndToString(rootOwner)
        << L" normalized=" << hwndToString(normalized);
    appendCandidateWindowLog(log.str());
    return normalized;
}

void enforceCandidateWindowTopmost(HWND hwnd, bool showWindow, const wchar_t* reason) {
    if (!hwnd) {
        return;
    }

    UINT flags = SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE;
    if (showWindow) {
        flags |= SWP_SHOWWINDOW;
    }

    ::SetLastError(0);
    const BOOL ok = ::SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, flags);
    const DWORD error = ok ? 0 : ::GetLastError();

    std::wostringstream log;
    log << L"[enforceCandidateWindowTopmost] reason=" << reason
        << L" hwnd=" << hwndToString(hwnd)
        << L" show=" << (showWindow ? L"true" : L"false")
        << L" ok=" << (ok ? L"true" : L"false");
    if (!ok) {
        log << L" last_error=" << error;
    }
    appendCandidateWindowLog(log.str());
    logCandidateWindowState(L"[CandidateWindow::state]", hwnd);
}

HWND resolveCandidateOwnerWindow(Ime::EditSession* session) {
    HWND hwnd = nullptr;
    const wchar_t* source = L"none";
    if (session != nullptr) {
        if (ITfContext* context = session->context()) {
            ITfContextView* view = nullptr;
            if (SUCCEEDED(context->GetActiveView(&view)) && view != nullptr) {
                view->GetWnd(&hwnd);
                if (hwnd != nullptr) {
                    source = L"context-view";
                }
                view->Release();
            }
        }
    }
    if (hwnd == nullptr) {
        hwnd = ::GetFocus();
        if (hwnd != nullptr) {
            source = L"GetFocus";
        }
    }
    if (hwnd == nullptr) {
        hwnd = ::GetForegroundWindow();
        if (hwnd != nullptr) {
            source = L"GetForegroundWindow";
        }
    }
    std::wostringstream log;
    log << L"[resolveCandidateOwnerWindow] session=" << session
        << L" source=" << source << L" hwnd=" << hwnd;
    appendCandidateWindowLog(log.str());
    return hwnd;
}

} // namespace

namespace Moqi {

CandidateWindow::CandidateWindow(Ime::TextService* service, Ime::EditSession* session)
    : Ime::ImeWindow(service),
      shown_(false),
      selKeyWidth_(0),
      textWidth_(0),
      commentWidth_(0),
      itemHeight_(0),
      candPerRow_(1),
      candSpacing_(kDefaultCandidateSpacing),
      colSpacing_(0),
      rowSpacing_(0),
      padX_(service->isImmersive() ? 10 : 7),
      padY_(service->isImmersive() ? 6 : 3),
      labelGap_(6),
      commentGap_(8),
      borderWidth_(1),
      borderRadius_(4),
      minWidth_(200),
      preeditHeight_(0),
      preeditGap_(8),
      contentTop_(0),
      backgroundColor_(kWindowBackground),
      highlightColor_(kSelectedBackground),
      textColor_(kItemText),
      highlightTextColor_(kSelectedText),
      commentColor_(kItemText),
      commentHighlightColor_(kSelectedText),
      currentSel_(0),
      pressedSel_(-1),
      draggingWindow_(false),
      trackingMouse_(false),
      useCursor_(false),
      commentFont_(nullptr) {
    margin_ = 0;

    const HWND rawOwner = resolveCandidateOwnerWindow(session);
    const HWND owner = normalizeCandidateOwnerWindow(rawOwner, service->isImmersive(), L"ctor");
    create(owner, WS_POPUP | WS_CLIPCHILDREN,
           WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE);

    std::wostringstream log;
    log << L"[CandidateWindow::ctor] hwnd=" << hwnd_
        << L" raw_owner=" << rawOwner
        << L" owner=" << owner;
    appendCandidateWindowLog(log.str());
    logCandidateWindowState(L"[CandidateWindow::ctor.state]", hwnd_);
}

CandidateWindow::~CandidateWindow(void) {
}

STDMETHODIMP CandidateWindow::GetDescription(BSTR* pbstrDescription) {
    if (!pbstrDescription) {
        return E_INVALIDARG;
    }
    *pbstrDescription = SysAllocString(L"Moqi candidate window");
    return S_OK;
}

STDMETHODIMP CandidateWindow::GetGUID(GUID* pguid) {
    if (!pguid) {
        return E_INVALIDARG;
    }
    *pguid = {0x89671502, 0x43ab, 0x4939, {0x84, 0x6f, 0xe8, 0x30, 0x2b, 0x73, 0x7d, 0x3c}};
    return S_OK;
}

STDMETHODIMP CandidateWindow::Show(BOOL bShow) {
    shown_ = bShow;
    {
        std::wostringstream log;
        log << L"[CandidateWindow::Show] bShow=" << bShow
            << L" hwnd=" << hwnd_
            << L" owner=" << reinterpret_cast<HWND>(::GetWindowLongPtr(hwnd_, GWLP_HWNDPARENT))
            << L" gw_owner=" << ::GetWindow(hwnd_, GW_OWNER);
        appendCandidateWindowLog(log.str());
    }
    if (shown_) {
        show();
        enforceCandidateWindowTopmost(hwnd_, true, L"Show(TRUE)");
    } else {
        hide();
        logCandidateWindowState(L"[CandidateWindow::hide.state]", hwnd_);
    }
    return S_OK;
}

STDMETHODIMP CandidateWindow::IsShown(BOOL* pbShow) {
    if (!pbShow) {
        return E_INVALIDARG;
    }
    *pbShow = shown_;
    return S_OK;
}

STDMETHODIMP CandidateWindow::GetUpdatedFlags(DWORD* pdwFlags) {
    if (!pdwFlags) {
        return E_INVALIDARG;
    }
    *pdwFlags = TF_CLUIE_DOCUMENTMGR | TF_CLUIE_COUNT | TF_CLUIE_SELECTION |
                TF_CLUIE_STRING | TF_CLUIE_PAGEINDEX | TF_CLUIE_CURRENTPAGE;
    return S_OK;
}

STDMETHODIMP CandidateWindow::GetDocumentMgr(ITfDocumentMgr** ppdim) {
    if (!textService_ || !textService_->currentContext()) {
        return E_FAIL;
    }
    return textService_->currentContext()->GetDocumentMgr(ppdim);
}

STDMETHODIMP CandidateWindow::GetCount(UINT* puCount) {
    if (!puCount) {
        return E_INVALIDARG;
    }
    *puCount = (std::min<UINT>)(10, static_cast<UINT>(items_.size()));
    return S_OK;
}

STDMETHODIMP CandidateWindow::GetSelection(UINT* puIndex) {
    if (!puIndex) {
        return E_INVALIDARG;
    }
    assert(currentSel_ >= 0);
    *puIndex = static_cast<UINT>(currentSel_);
    return S_OK;
}

STDMETHODIMP CandidateWindow::GetString(UINT uIndex, BSTR* pbstr) {
    if (!pbstr) {
        return E_INVALIDARG;
    }
    if (uIndex >= items_.size()) {
        return E_INVALIDARG;
    }
    *pbstr = SysAllocString(items_[uIndex].combinedText().c_str());
    return S_OK;
}

STDMETHODIMP CandidateWindow::GetPageIndex(UINT* puIndex, UINT uSize, UINT* puPageCnt) {
    if (!puPageCnt) {
        return E_INVALIDARG;
    }
    *puPageCnt = 1;
    if (puIndex) {
        if (uSize < *puPageCnt) {
            return E_INVALIDARG;
        }
        puIndex[0] = 0;
    }
    return S_OK;
}

STDMETHODIMP CandidateWindow::SetPageIndex(UINT* puIndex, UINT uPageCnt) {
    (void)uPageCnt;
    if (!puIndex) {
        return E_INVALIDARG;
    }
    return S_OK;
}

STDMETHODIMP CandidateWindow::GetCurrentPage(UINT* puPage) {
    if (!puPage) {
        return E_INVALIDARG;
    }
    *puPage = 0;
    return S_OK;
}

void CandidateWindow::add(CandidateUiItem item, wchar_t selKey) {
    items_.push_back(std::move(item));
    selKeys_.push_back(selKey);
}

void CandidateWindow::clear() {
    items_.clear();
    selKeys_.clear();
    if (::GetCapture() != hwnd_) {
        pressedSel_ = -1;
    }
}

void CandidateWindow::setCandPerRow(int n) {
    n = (std::max)(1, n);
    if (candPerRow_ != n) {
        candPerRow_ = n;
        recalculateSize();
    }
}

void CandidateWindow::setCandSpacing(int spacing) {
    spacing = (std::max)(0, spacing);
    if (candSpacing_ != spacing) {
        candSpacing_ = spacing;
        recalculateSize();
        if (isVisible()) {
            ::InvalidateRect(hwnd_, NULL, TRUE);
        }
    }
}

void CandidateWindow::setCurrentSel(int sel) {
    if (items_.empty()) {
        currentSel_ = 0;
        return;
    }
    if (sel < 0 || sel >= static_cast<int>(items_.size())) {
        sel = 0;
    }
    if (currentSel_ != sel) {
        currentSel_ = sel;
        if (isVisible()) {
            ::InvalidateRect(hwnd_, NULL, TRUE);
        }
    }
}

void CandidateWindow::setUseCursor(bool use) {
    useCursor_ = use;
    if (isVisible()) {
        ::InvalidateRect(hwnd_, NULL, TRUE);
    }
}

void CandidateWindow::setPreeditText(std::wstring text) {
    if (preedit_ != text) {
        preedit_ = std::move(text);
        recalculateSize();
        if (isVisible()) {
            ::InvalidateRect(hwnd_, NULL, TRUE);
        }
    }
}

void CandidateWindow::setCommentFont(HFONT font) {
    if (commentFont_ != font) {
        commentFont_ = font;
        recalculateSize();
        if (isVisible()) {
            ::InvalidateRect(hwnd_, NULL, TRUE);
        }
    }
}

void CandidateWindow::setBackgroundColor(COLORREF color) {
    if (backgroundColor_ != color) {
        backgroundColor_ = color;
        if (isVisible()) {
            ::InvalidateRect(hwnd_, NULL, TRUE);
        }
    }
}

void CandidateWindow::setHighlightColor(COLORREF color) {
    if (highlightColor_ != color) {
        highlightColor_ = color;
        if (isVisible()) {
            ::InvalidateRect(hwnd_, NULL, TRUE);
        }
    }
}

void CandidateWindow::setTextColor(COLORREF color) {
    if (textColor_ != color) {
        textColor_ = color;
        if (isVisible()) {
            ::InvalidateRect(hwnd_, NULL, TRUE);
        }
    }
}

void CandidateWindow::setHighlightTextColor(COLORREF color) {
    if (highlightTextColor_ != color) {
        highlightTextColor_ = color;
        if (isVisible()) {
            ::InvalidateRect(hwnd_, NULL, TRUE);
        }
    }
}

void CandidateWindow::setCommentColor(COLORREF color) {
    if (commentColor_ != color) {
        commentColor_ = color;
        if (isVisible()) {
            ::InvalidateRect(hwnd_, NULL, TRUE);
        }
    }
}

void CandidateWindow::setCommentHighlightColor(COLORREF color) {
    if (commentHighlightColor_ != color) {
        commentHighlightColor_ = color;
        if (isVisible()) {
            ::InvalidateRect(hwnd_, NULL, TRUE);
        }
    }
}

void CandidateWindow::syncOwner(Ime::EditSession* session) {
    if (!hwnd_) {
        return;
    }

    const HWND rawOwner = resolveCandidateOwnerWindow(session);
    const HWND owner = normalizeCandidateOwnerWindow(rawOwner, textService_->isImmersive(), L"syncOwner");
    if (owner == nullptr) {
        std::wostringstream log;
        log << L"[CandidateWindow::syncOwner] owner unavailable hwnd=" << hwnd_
            << L" current_owner="
            << reinterpret_cast<HWND>(::GetWindowLongPtr(hwnd_, GWLP_HWNDPARENT))
            << L" current_gw_owner=" << ::GetWindow(hwnd_, GW_OWNER)
            << L" raw_owner=" << rawOwner
            << L" shown=" << shown_;
        appendCandidateWindowLog(log.str());
        logCandidateWindowState(L"[CandidateWindow::syncOwner.owner_unavailable.state]", hwnd_);
        return;
    }

    const HWND currentOwner =
        reinterpret_cast<HWND>(::GetWindowLongPtr(hwnd_, GWLP_HWNDPARENT));
    const HWND currentGwOwner = ::GetWindow(hwnd_, GW_OWNER);
    bool ownerUpdated = false;
    DWORD ownerError = 0;
    if (currentOwner != owner) {
        ::SetLastError(0);
        ::SetWindowLongPtr(hwnd_, GWLP_HWNDPARENT, reinterpret_cast<LONG_PTR>(owner));
        ownerError = ::GetLastError();
        ownerUpdated = ownerError == 0;
    }

    if (shown_) {
        enforceCandidateWindowTopmost(hwnd_, true, L"syncOwner");
    }

    std::wostringstream log;
    log << L"[CandidateWindow::syncOwner] hwnd=" << hwnd_
        << L" old_owner=" << currentOwner
        << L" old_gw_owner=" << currentGwOwner
        << L" raw_owner=" << rawOwner
        << L" new_owner=" << owner
        << L" shown=" << shown_
        << L" owner_updated=" << (ownerUpdated ? L"true" : L"false");
    if (ownerError != 0) {
        log << L" last_error=" << ownerError;
    }
    appendCandidateWindowLog(log.str());
    logCandidateWindowState(L"[CandidateWindow::syncOwner.state]", hwnd_);
}

void CandidateWindow::recalculateSize() {
    if (items_.empty() && preedit_.empty()) {
        selKeyWidth_ = 0;
        textWidth_ = 0;
        commentWidth_ = 0;
        itemHeight_ = 0;
        preeditHeight_ = 0;
        contentTop_ = borderWidth_ + padY_;
        resize(padX_ * 2 + borderWidth_ * 2, padY_ * 2 + borderWidth_ * 2);
        applyWindowShape();
        return;
    }

    HDC hdc = ::GetWindowDC(hwnd());
    if (!hdc) {
        return;
    }

    selKeyWidth_ = 0;
    textWidth_ = 0;
    commentWidth_ = 0;
    itemHeight_ = 0;
    preeditHeight_ = 0;

    HGDIOBJ oldFont = ::SelectObject(hdc, font_);
    TEXTMETRICW metrics = {};
    TEXTMETRICW commentMetrics = {};
    for (int i = 0, n = static_cast<int>(items_.size()); i < n; ++i) {
        SIZE selKeySize = {};
        wchar_t selKey[] = L"?.";
        selKey[0] = selKeys_[i];
        ::GetTextExtentPoint32W(hdc, selKey, 2, &selKeySize);
        selKeyWidth_ = (std::max)(selKeyWidth_, static_cast<int>(selKeySize.cx));

        SIZE candidateSize = {};
        const CandidateUiItem& item = items_[i];
        ::GetTextExtentPoint32W(hdc, item.text.c_str(), static_cast<int>(item.text.length()), &candidateSize);
        textWidth_ = (std::max)(textWidth_, static_cast<int>(candidateSize.cx));
        int candidateHeight = static_cast<int>(candidateSize.cy);
        if (!item.comment.empty() && commentFont_) {
            SIZE commentSize = {};
            ::SelectObject(hdc, commentFont_);
            ::GetTextExtentPoint32W(hdc, item.comment.c_str(), static_cast<int>(item.comment.length()), &commentSize);
            ::SelectObject(hdc, font_);
            commentWidth_ = (std::max)(commentWidth_, static_cast<int>(commentSize.cx));
            candidateHeight = (std::max)(candidateHeight, static_cast<int>(commentSize.cy));
        }
        itemHeight_ = (std::max)(itemHeight_, (std::max)(candidateHeight, static_cast<int>(selKeySize.cy)));
    }
    if (!preedit_.empty()) {
        SIZE preeditSize = {};
        ::GetTextExtentPoint32W(hdc, preedit_.c_str(), static_cast<int>(preedit_.length()), &preeditSize);
        textWidth_ = (std::max)(textWidth_, static_cast<int>(preeditSize.cx));
        preeditHeight_ = static_cast<int>(preeditSize.cy);
    }
    ::GetTextMetricsW(hdc, &metrics);
    if (commentFont_) {
        ::SelectObject(hdc, commentFont_);
        ::GetTextMetricsW(hdc, &commentMetrics);
        ::SelectObject(hdc, font_);
    }
    ::SelectObject(hdc, oldFont);
    ::ReleaseDC(hwnd(), hdc);

    const int itemsPerRow = (std::max)(1, candPerRow_);
    itemHeight_ = (std::max)(itemHeight_, static_cast<int>(metrics.tmHeight + metrics.tmExternalLeading + 2));
    if (commentFont_) {
        itemHeight_ = (std::max)(itemHeight_, static_cast<int>(commentMetrics.tmHeight + commentMetrics.tmExternalLeading + 2));
    }
    preeditHeight_ = preedit_.empty()
                         ? 0
                         : (std::max)(preeditHeight_, static_cast<int>(metrics.tmHeight + metrics.tmExternalLeading));

    const int commentSectionWidth = commentWidth_ > 0 ? commentGap_ + commentWidth_ : 0;
    const int trailingGap = candPerRow_ > 1 && commentWidth_ == 0 ? candSpacing_ : 0;
    const int itemWidth = selKeyWidth_ + labelGap_ + textWidth_ + commentSectionWidth + trailingGap;
    const int columns = (std::min)(itemsPerRow, static_cast<int>(items_.size()));
    const int rows = (static_cast<int>(items_.size()) + itemsPerRow - 1) / itemsPerRow;
    const int candidateContentWidth = columns * itemWidth + (std::max)(0, columns - 1) * colSpacing_;
    const int contentWidth = (std::max)(candidateContentWidth, textWidth_);
    const int width = (std::max)(minWidth_, padX_ * 2 + contentWidth) + borderWidth_ * 2;
    int contentHeight = rows * itemHeight_ + (std::max)(0, rows - 1) * rowSpacing_;
    if (!preedit_.empty()) {
        contentTop_ = borderWidth_ + padY_ + preeditHeight_ + preeditGap_;
        contentHeight += preeditHeight_ + preeditGap_;
    } else {
        contentTop_ = borderWidth_ + padY_;
    }
    const int height = padY_ * 2 + contentHeight + borderWidth_ * 2;
    resize(width, height);
    applyWindowShape();

    std::wostringstream log;
    log << L"[CandidateWindow::recalculateSize] items=" << items_.size()
        << L" width=" << width << L" height=" << height
        << L" perRow=" << candPerRow_;
    appendCandidateWindowLog(log.str());
}

LRESULT CandidateWindow::wndProc(UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_PAINT:
        onPaint();
        return 0;
    case WM_ERASEBKGND:
        return TRUE;
    case WM_LBUTTONDOWN:
        onLButtonDown(wp, lp);
        return 0;
    case WM_MOUSEMOVE:
        onMouseMove(wp, lp);
        return 0;
    case WM_LBUTTONUP:
        onLButtonUp(wp, lp);
        return 0;
    case WM_MOUSELEAVE:
        onMouseLeave();
        return 0;
    case WM_MOUSEWHEEL:
        onMouseWheel(wp, lp);
        return 0;
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;
    default:
        return Window::wndProc(msg, wp, lp);
    }
}

void CandidateWindow::onPaint() {
    PAINTSTRUCT ps = {};
    BeginPaint(hwnd_, &ps);
    HDC hdc = ps.hdc;

    RECT rc = {};
    GetClientRect(hwnd_, &rc);
    HDC memdc = ::CreateCompatibleDC(hdc);
    HBITMAP membmp = ::CreateCompatibleBitmap(hdc, rc.right - rc.left, rc.bottom - rc.top);
    HGDIOBJ oldBitmap = ::SelectObject(memdc, membmp);
    HGDIOBJ oldFont = ::SelectObject(memdc, font_);
    ::SetBkMode(memdc, TRANSPARENT);

    HBRUSH backgroundBrush = ::CreateSolidBrush(backgroundColor_);
    HBRUSH borderBrush = ::CreateSolidBrush(kWindowBorder);
    HRGN windowRgn = ::CreateRoundRectRgn(
        rc.left, rc.top, rc.right + 1, rc.bottom + 1,
        borderRadius_ * 2, borderRadius_ * 2);
    ::FillRect(memdc, &rc, static_cast<HBRUSH>(::GetStockObject(WHITE_BRUSH)));
    ::FillRgn(memdc, windowRgn, backgroundBrush);
    ::FrameRgn(memdc, windowRgn, borderBrush, borderWidth_, borderWidth_);

    int y = borderWidth_ + padY_;
    if (!preedit_.empty()) {
        RECT preeditRc = {
            borderWidth_ + padX_,
            y,
            rc.right - borderWidth_ - padX_,
            y + preeditHeight_};
        ::SetTextColor(memdc, textColor_);
        ::DrawTextW(memdc, preedit_.c_str(), static_cast<int>(preedit_.length()), &preeditRc,
                    DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

        const int dividerY = preeditRc.bottom + preeditGap_ / 2;
        HPEN dividerPen = ::CreatePen(PS_SOLID, 1, kDividerColor);
        HGDIOBJ oldPen = ::SelectObject(memdc, dividerPen);
        ::MoveToEx(memdc, borderWidth_ + padX_, dividerY, nullptr);
        ::LineTo(memdc, rc.right - borderWidth_ - padX_, dividerY);
        ::SelectObject(memdc, oldPen);
        ::DeleteObject(dividerPen);
    }

    int col = 0;
    int x = borderWidth_ + padX_;
    y = contentTop_;
    const int trailingGap = candPerRow_ > 1 && commentWidth_ == 0 ? candSpacing_ : 0;
    const int itemWidth = selKeyWidth_ + labelGap_ + textWidth_ +
                          (commentWidth_ > 0 ? commentGap_ + commentWidth_ : 0) + trailingGap;
    for (int i = 0, n = static_cast<int>(items_.size()); i < n; ++i) {
        paintItem(memdc, i, x, y);
        ++col;
        if (col >= candPerRow_) {
            col = 0;
            x = borderWidth_ + padX_;
            y += itemHeight_ + rowSpacing_;
        } else {
            x += colSpacing_ + itemWidth;
        }
    }

    ::BitBlt(hdc, 0, 0, rc.right - rc.left, rc.bottom - rc.top, memdc, 0, 0, SRCCOPY);

    ::DeleteObject(windowRgn);
    ::DeleteObject(borderBrush);
    ::DeleteObject(backgroundBrush);
    ::SelectObject(memdc, oldFont);
    ::SelectObject(memdc, oldBitmap);
    ::DeleteObject(membmp);
    ::DeleteDC(memdc);
    EndPaint(hwnd_, &ps);
}

void CandidateWindow::paintItem(HDC hdc, int index, int x, int y) {
    const bool selected = useCursor_ && index == currentSel_;

    RECT itemRc = {x, y, x + selKeyWidth_ + labelGap_ + textWidth_, y + itemHeight_};
    if (commentWidth_ > 0) {
        itemRc.right += commentGap_ + commentWidth_;
    }
    RECT highlightRc = itemRc;
    RECT selRc = itemRc;
    selRc.right = selRc.left + selKeyWidth_;
    RECT textRc = itemRc;
    textRc.left += selKeyWidth_ + labelGap_;
    textRc.right = textRc.left + textWidth_;
    RECT commentRc = textRc;
    commentRc.left = textRc.right + commentGap_;
    commentRc.right = commentRc.left + commentWidth_;

    const COLORREF bgColor = selected ? highlightColor_ : backgroundColor_;
    const COLORREF textColor = selected ? highlightTextColor_ : textColor_;
    const COLORREF selColor = selected ? highlightTextColor_ : textColor_;
    const COLORREF commentColor = selected ? commentHighlightColor_ : commentColor_;

    if (selected) {
        if (candPerRow_ == 1) {
            RECT clientRc = {};
            ::GetClientRect(hwnd_, &clientRc);
            highlightRc.left = borderWidth_ + padX_;
            highlightRc.right = clientRc.right - borderWidth_ - padX_;
        }
        HBRUSH highlightBrush = ::CreateSolidBrush(bgColor);
        ::FillRect(hdc, &highlightRc, highlightBrush);
        ::DeleteObject(highlightBrush);
    }

    wchar_t selKey[] = L"?.";
    selKey[0] = selKeys_[index];
    const COLORREF oldColor = ::SetTextColor(hdc, selColor);
    ::DrawTextW(hdc, selKey, 2, &selRc, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    ::SetTextColor(hdc, textColor);
    const CandidateUiItem& item = items_[index];
    HGDIOBJ oldFont = ::SelectObject(hdc, font_);
    ::DrawTextW(hdc, item.text.c_str(), static_cast<int>(item.text.length()), &textRc,
                DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    if (!item.comment.empty() && commentFont_) {
        ::SelectObject(hdc, commentFont_);
        ::SetTextColor(hdc, commentColor);
        ::DrawTextW(hdc, item.comment.c_str(), static_cast<int>(item.comment.length()), &commentRc,
                    DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }
    ::SelectObject(hdc, oldFont);
    ::SetTextColor(hdc, oldColor);
}

int CandidateWindow::hitTestCandidate(POINT pt) const {
    if (items_.empty()) {
        return -1;
    }

    RECT clientRc = {};
    ::GetClientRect(hwnd_, &clientRc);
    for (int i = 0, n = static_cast<int>(items_.size()); i < n; ++i) {
        RECT rect = {};
        itemRect(i, rect);
        if (candPerRow_ == 1) {
            rect.left = borderWidth_ + padX_;
            rect.right = clientRc.right - borderWidth_ - padX_;
        }
        if (::PtInRect(&rect, pt)) {
            return i;
        }
    }
    return -1;
}

void CandidateWindow::onLButtonDown(WPARAM wp, LPARAM lp) {
    POINT pt = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
    const int hitIndex = hitTestCandidate(pt);
    if (hitIndex >= 0) {
        pressedSel_ = hitIndex;
        draggingWindow_ = false;
        ::SetCapture(hwnd_);
        return;
    }

    pressedSel_ = -1;
    draggingWindow_ = true;
    Ime::ImeWindow::onLButtonDown(wp, lp);
}

void CandidateWindow::onLButtonUp(WPARAM wp, LPARAM lp) {
    const bool hadCapture = ::GetCapture() == hwnd_;
    if (hadCapture) {
        ::ReleaseCapture();
    }

    if (draggingWindow_) {
        draggingWindow_ = false;
        Ime::ImeWindow::onLButtonUp(wp, lp);
        return;
    }

    POINT pt = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
    const int hitIndex = hitTestCandidate(pt);
    if (pressedSel_ >= 0 && hitIndex >= 0 &&
        (hitIndex == currentSel_ || hitIndex == pressedSel_)) {
        if (auto* textService = static_cast<Moqi::TextService*>(textService_)) {
            textService->selectCandidate(hitIndex);
        }
    }
    pressedSel_ = -1;
}

void CandidateWindow::onMouseMove(WPARAM wp, LPARAM lp) {
    if (!trackingMouse_) {
        TRACKMOUSEEVENT tme = {};
        tme.cbSize = sizeof(tme);
        tme.dwFlags = TME_LEAVE;
        tme.hwndTrack = hwnd_;
        if (::TrackMouseEvent(&tme)) {
            trackingMouse_ = true;
        }
    }

    if (draggingWindow_) {
        Ime::ImeWindow::onMouseMove(wp, lp);
        return;
    }
}

void CandidateWindow::onMouseLeave() {
    trackingMouse_ = false;
}

void CandidateWindow::onMouseWheel(WPARAM wp, LPARAM) {
    const short delta = GET_WHEEL_DELTA_WPARAM(wp);
    if (delta == 0) {
        return;
    }
    if (auto* textService = static_cast<Moqi::TextService*>(textService_)) {
        textService->changeCandidatePage(delta > 0);
    }
}

void CandidateWindow::itemRect(int index, RECT& rect) const {
    const int row = index / candPerRow_;
    const int col = index % candPerRow_;
    const int trailingGap = candPerRow_ > 1 && commentWidth_ == 0 ? candSpacing_ : 0;
    const int itemWidth = selKeyWidth_ + labelGap_ + textWidth_ +
                          (commentWidth_ > 0 ? commentGap_ + commentWidth_ : 0) + trailingGap;
    rect.left = borderWidth_ + padX_ + col * (itemWidth + colSpacing_);
    rect.top = contentTop_ + row * (itemHeight_ + rowSpacing_);
    rect.right = rect.left + itemWidth;
    rect.bottom = rect.top + itemHeight_;
}

void CandidateWindow::applyWindowShape() {
    if (!hwnd_) {
        return;
    }

    RECT rc = {};
    ::GetClientRect(hwnd_, &rc);
    if (rc.right <= rc.left || rc.bottom <= rc.top) {
        return;
    }

    HRGN region = ::CreateRoundRectRgn(
        rc.left, rc.top, rc.right + 1, rc.bottom + 1,
        borderRadius_ * 2, borderRadius_ * 2);
    ::SetWindowRgn(hwnd_, region, TRUE);
}

} // namespace Moqi
