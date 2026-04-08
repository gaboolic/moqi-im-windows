//
//    Copyright (C) 2026
//

#include "MoqiCandidateWindow.h"

#include <LibIME2/src/DrawUtils.h>
#include <LibIME2/src/EditSession.h>
#include <LibIME2/src/TextService.h>

#include <algorithm>
#include <cassert>
#include <fstream>
#include <sstream>

namespace {

constexpr COLORREF kWindowBackground = RGB(255, 220, 220);
constexpr COLORREF kWindowBorder = RGB(255, 0, 0);
constexpr COLORREF kItemText = RGB(90, 0, 0);
constexpr COLORREF kItemAuxText = RGB(220, 0, 0);
constexpr COLORREF kSelectedBackground = RGB(200, 0, 0);
constexpr COLORREF kSelectedText = RGB(255, 255, 255);
constexpr COLORREF kSelectedAuxText = RGB(255, 230, 230);

void appendCandidateWindowLog(const std::wstring& message) {
    const wchar_t* localAppData = _wgetenv(L"LOCALAPPDATA");
    if (!localAppData || !*localAppData) {
        return;
    }

    std::wstring logDir = std::wstring(localAppData) + L"\\MoqiIM\\Log";
    ::CreateDirectoryW((std::wstring(localAppData) + L"\\MoqiIM").c_str(), nullptr);
    ::CreateDirectoryW(logDir.c_str(), nullptr);
    std::wstring logPath = logDir + L"\\candidate-window.log";

    std::wofstream stream(logPath, std::ios::app);
    if (!stream.is_open()) {
        return;
    }
    stream << message << L"\n";
}

} // namespace

namespace Moqi {

CandidateWindow::CandidateWindow(Ime::TextService* service, Ime::EditSession* session)
    : Ime::ImeWindow(service),
      shown_(false),
      selKeyWidth_(0),
      textWidth_(0),
      itemHeight_(0),
      candPerRow_(1),
      colSpacing_(service->isImmersive() ? 12 : 8),
      rowSpacing_(service->isImmersive() ? 8 : 4),
      currentSel_(0),
      useCursor_(false) {
    if (service->isImmersive()) {
        margin_ = 10;
    } else {
        margin_ = 6;
    }

    HWND parent = service->compositionWindow(session);
    create(parent, WS_POPUP | WS_CLIPCHILDREN, WS_EX_TOOLWINDOW | WS_EX_TOPMOST);

    std::wostringstream log;
    log << L"[CandidateWindow::ctor] hwnd=" << hwnd_ << L" parent=" << parent;
    appendCandidateWindowLog(log.str());
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
        log << L"[CandidateWindow::Show] bShow=" << bShow << L" hwnd=" << hwnd_;
        appendCandidateWindowLog(log.str());
    }
    if (shown_) {
        show();
    } else {
        hide();
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
    *pbstr = SysAllocString(items_[uIndex].c_str());
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

void CandidateWindow::add(std::wstring item, wchar_t selKey) {
    items_.push_back(std::move(item));
    selKeys_.push_back(selKey);
}

void CandidateWindow::clear() {
    items_.clear();
    selKeys_.clear();
    currentSel_ = 0;
}

void CandidateWindow::setCandPerRow(int n) {
    n = (std::max)(1, n);
    if (candPerRow_ != n) {
        candPerRow_ = n;
        recalculateSize();
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

void CandidateWindow::recalculateSize() {
    if (items_.empty()) {
        selKeyWidth_ = 0;
        textWidth_ = 0;
        itemHeight_ = 0;
        resize(margin_ * 2, margin_ * 2);
        return;
    }

    HDC hdc = ::GetWindowDC(hwnd());
    if (!hdc) {
        return;
    }

    selKeyWidth_ = 0;
    textWidth_ = 0;
    itemHeight_ = 0;

    HGDIOBJ oldFont = ::SelectObject(hdc, font_);
    for (int i = 0, n = static_cast<int>(items_.size()); i < n; ++i) {
        SIZE selKeySize = {};
        wchar_t selKey[] = L"?. ";
        selKey[0] = selKeys_[i];
        ::GetTextExtentPoint32W(hdc, selKey, 3, &selKeySize);
        selKeyWidth_ = (std::max)(selKeyWidth_, static_cast<int>(selKeySize.cx));

        SIZE candidateSize = {};
        const std::wstring& item = items_[i];
        ::GetTextExtentPoint32W(hdc, item.c_str(), static_cast<int>(item.length()), &candidateSize);
        textWidth_ = (std::max)(textWidth_, static_cast<int>(candidateSize.cx));
        itemHeight_ = (std::max)(itemHeight_, (std::max)(static_cast<int>(candidateSize.cy), static_cast<int>(selKeySize.cy)));
    }
    ::SelectObject(hdc, oldFont);
    ::ReleaseDC(hwnd(), hdc);

    const int itemsPerRow = (std::max)(1, candPerRow_);
    const int itemWidth = selKeyWidth_ + textWidth_;
    const int columns = (std::min)(itemsPerRow, static_cast<int>(items_.size()));
    const int rows = (static_cast<int>(items_.size()) + itemsPerRow - 1) / itemsPerRow;
    const int width = margin_ * 2 + columns * itemWidth + (std::max)(0, columns - 1) * colSpacing_;
    const int height = margin_ * 2 + rows * itemHeight_ + (std::max)(0, rows - 1) * rowSpacing_;
    resize(width, height);

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
    HGDIOBJ oldFont = ::SelectObject(hdc, font_);

    RECT rc = {};
    GetClientRect(hwnd_, &rc);
    ::FillSolidRect(hdc, &rc, kWindowBackground);
    ::Draw3DBorder(hdc, &rc, kWindowBorder, kWindowBorder);

    int col = 0;
    int x = margin_;
    int y = margin_;
    for (int i = 0, n = static_cast<int>(items_.size()); i < n; ++i) {
        paintItem(hdc, i, x, y);
        ++col;
        if (col >= candPerRow_) {
            col = 0;
            x = margin_;
            y += itemHeight_ + rowSpacing_;
        } else {
            x += colSpacing_ + selKeyWidth_ + textWidth_;
        }
    }

    ::SelectObject(hdc, oldFont);
    EndPaint(hwnd_, &ps);
}

void CandidateWindow::paintItem(HDC hdc, int index, int x, int y) {
    const bool selected = useCursor_ && index == currentSel_;

    RECT itemRc = {x, y, x + selKeyWidth_ + textWidth_, y + itemHeight_};
    RECT selRc = itemRc;
    selRc.right = selRc.left + selKeyWidth_;
    RECT textRc = itemRc;
    textRc.left += selKeyWidth_;

    const COLORREF bgColor = selected ? kSelectedBackground : kWindowBackground;
    const COLORREF textColor = selected ? kSelectedText : kItemText;
    const COLORREF selColor = selected ? kSelectedAuxText : kItemAuxText;

    ::FillSolidRect(hdc, &itemRc, bgColor);

    wchar_t selKey[] = L"?. ";
    selKey[0] = selKeys_[index];
    ::SetBkMode(hdc, TRANSPARENT);
    const COLORREF oldColor = ::SetTextColor(hdc, selColor);
    ::DrawTextW(hdc, selKey, 3, &selRc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    ::SetTextColor(hdc, textColor);
    const std::wstring& item = items_[index];
    ::DrawTextW(hdc, item.c_str(), static_cast<int>(item.length()), &textRc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    ::SetTextColor(hdc, oldColor);
}

void CandidateWindow::itemRect(int index, RECT& rect) const {
    const int row = index / candPerRow_;
    const int col = index % candPerRow_;
    rect.left = margin_ + col * (selKeyWidth_ + textWidth_ + colSpacing_);
    rect.top = margin_ + row * (itemHeight_ + rowSpacing_);
    rect.right = rect.left + selKeyWidth_ + textWidth_;
    rect.bottom = rect.top + itemHeight_;
}

} // namespace Moqi
