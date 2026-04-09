#include <windows.h>
#include <windowsx.h>

#include <algorithm>
#include <string>
#include <vector>

namespace {

constexpr wchar_t kWindowClassName[] = L"MoqiCandidatePreviewWindow";
constexpr wchar_t kWindowTitle[] =
    L"Moqi Candidate Preview  |  Up/Down: Select  +/-: Font  C/J/L: Sample  H: Highlight  M: Columns";

constexpr COLORREF kAppBackground = RGB(246, 246, 246);
constexpr COLORREF kPanelBackground = RGB(255, 255, 255);
constexpr COLORREF kPanelBorder = RGB(150, 150, 150);
constexpr COLORREF kItemText = RGB(0, 0, 0);
constexpr COLORREF kItemAuxText = RGB(0, 0, 0);
constexpr COLORREF kSelectedBackground = RGB(198, 221, 249);
constexpr COLORREF kSelectedText = RGB(0, 0, 0);
constexpr COLORREF kSelectedAuxText = RGB(0, 0, 0);
constexpr COLORREF kHintText = RGB(110, 110, 110);
constexpr COLORREF kPreeditBackground = RGB(235, 245, 255);

int scalePx(HWND hwnd, int value) {
    HDC hdc = GetDC(hwnd);
    const UINT dpi = hdc ? static_cast<UINT>(GetDeviceCaps(hdc, LOGPIXELSX)) : 96;
    if (hdc) {
        ReleaseDC(hwnd, hdc);
    }
    return MulDiv(value, static_cast<int>(dpi), 96);
}

void fillSolidRect(HDC dc, const RECT &rc, COLORREF color) {
    HBRUSH brush = CreateSolidBrush(color);
    FillRect(dc, &rc, brush);
    DeleteObject(brush);
}

std::wstring candidateLabel(size_t index) {
    if (index < 9) {
        return std::to_wstring(index + 1) + L".";
    }
    if (index == 9) {
        return L"0.";
    }
    return std::to_wstring(index + 1) + L".";
}

struct SampleData {
    std::wstring name;
    std::wstring preedit;
    std::vector<std::wstring> items;
};

const std::vector<SampleData> &samples() {
    static const std::vector<SampleData> data = {
        {L"Chinese", L"xm zd",
         {L"现在 x", L"先在", L"陷在", L"先 ne", L"线 sj"}},
        {L"Japanese", L"あっt",
         {L"あっ", L"アッ", L"合っ", L"会っ", L"あッ"}},
        {L"Long", L"hz xr kl",
         {L"候选框", L"候选", L"侯选", L"后 ik", L"厚 iz", L"後 rw", L"吼 kv", L"侯 ru", L"猴 qu"}}};
    return data;
}

class PreviewWindow {
public:
    bool create(HINSTANCE instance) {
        WNDCLASSEXW wc = {};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = &PreviewWindow::staticWndProc;
        wc.hInstance = instance;
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = nullptr;
        wc.lpszClassName = kWindowClassName;
        if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
            return false;
        }

        hwnd_ = CreateWindowExW(0, kWindowClassName, kWindowTitle,
                                WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
                                CW_USEDEFAULT, CW_USEDEFAULT, 640, 520, nullptr, nullptr,
                                instance, this);
        if (!hwnd_) {
            return false;
        }

        recreateFonts();
        switchSample(0);
        ShowWindow(hwnd_, SW_SHOW);
        UpdateWindow(hwnd_);
        return true;
    }

private:
    static LRESULT CALLBACK staticWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
        PreviewWindow *self = nullptr;
        if (msg == WM_NCCREATE) {
            auto *create = reinterpret_cast<CREATESTRUCTW *>(lp);
            self = static_cast<PreviewWindow *>(create->lpCreateParams);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
            self->hwnd_ = hwnd;
        } else {
            self = reinterpret_cast<PreviewWindow *>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        }
        return self ? self->wndProc(msg, wp, lp) : DefWindowProcW(hwnd, msg, wp, lp);
    }

    LRESULT wndProc(UINT msg, WPARAM wp, LPARAM lp) {
        switch (msg) {
        case WM_DPICHANGED: {
            const RECT *suggested = reinterpret_cast<const RECT *>(lp);
            if (suggested) {
                SetWindowPos(hwnd_, nullptr, suggested->left, suggested->top,
                             suggested->right - suggested->left,
                             suggested->bottom - suggested->top,
                             SWP_NOACTIVATE | SWP_NOZORDER);
            }
            recreateFonts();
            recalculateLayout();
            InvalidateRect(hwnd_, nullptr, TRUE);
            return 0;
        }
        case WM_KEYDOWN:
            if (handleKeyDown(static_cast<UINT>(wp))) {
                return 0;
            }
            break;
        case WM_LBUTTONDOWN:
            handleClick(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
            return 0;
        case WM_PAINT:
            onPaint();
            return 0;
        case WM_DESTROY:
            destroyFonts();
            PostQuitMessage(0);
            return 0;
        default:
            break;
        }
        return DefWindowProcW(hwnd_, msg, wp, lp);
    }

    bool handleKeyDown(UINT key) {
        switch (key) {
        case VK_UP:
            moveSelection(-1);
            return true;
        case VK_DOWN:
            moveSelection(1);
            return true;
        case VK_ADD:
        case VK_OEM_PLUS:
            adjustFontSize(1);
            return true;
        case VK_SUBTRACT:
        case VK_OEM_MINUS:
            adjustFontSize(-1);
            return true;
        case 'C':
            switchSample(0);
            return true;
        case 'J':
            switchSample(1);
            return true;
        case 'L':
            switchSample(2);
            return true;
        case 'H':
            useCursor_ = !useCursor_;
            InvalidateRect(hwnd_, nullptr, TRUE);
            return true;
        case 'M':
            candPerRow_ = candPerRow_ == 1 ? 3 : 1;
            recalculateLayout();
            resizeWindowToContent();
            InvalidateRect(hwnd_, nullptr, TRUE);
            return true;
        default:
            return false;
        }
    }

    void handleClick(int x, int y) {
        if (x < panelRect_.left || x >= panelRect_.right || y < panelRect_.top || y >= panelRect_.bottom) {
            return;
        }
        const int contentTop = panelRect_.top + borderWidth_ + padY_;
        const int rowHeight = itemHeight_ + rowSpacing_;
        if (y < contentTop || rowHeight <= 0) {
            return;
        }
        const int row = (y - contentTop) / rowHeight;
        const int col = candPerRow_ == 1 ? 0 : (x - (panelRect_.left + borderWidth_ + padX_)) /
                                                (selKeyWidth_ + labelGap_ + textWidth_ + colSpacing_);
        if (row < 0 || col < 0) {
            return;
        }
        const int index = row * candPerRow_ + col;
        if (index >= 0 && index < static_cast<int>(items_.size())) {
            currentSel_ = index;
            InvalidateRect(hwnd_, nullptr, TRUE);
        }
    }

    void switchSample(size_t index) {
        const auto &all = samples();
        sampleIndex_ = (std::min)(index, all.size() - 1);
        const auto &sample = all[sampleIndex_];
        preedit_ = sample.preedit;
        items_ = sample.items;
        currentSel_ = 0;
        recalculateLayout();
        resizeWindowToContent();
        InvalidateRect(hwnd_, nullptr, TRUE);
    }

    void moveSelection(int delta) {
        if (items_.empty()) {
            return;
        }
        currentSel_ += delta;
        if (currentSel_ < 0) {
            currentSel_ = static_cast<int>(items_.size()) - 1;
        } else if (currentSel_ >= static_cast<int>(items_.size())) {
            currentSel_ = 0;
        }
        InvalidateRect(hwnd_, nullptr, TRUE);
    }

    void adjustFontSize(int delta) {
        fontPx_ = (std::max)(10, (std::min)(32, fontPx_ + delta));
        recreateFonts();
        recalculateLayout();
        resizeWindowToContent();
        InvalidateRect(hwnd_, nullptr, TRUE);
    }

    void recreateFonts() {
        destroyFonts();

        const int fontHeight = -scalePx(hwnd_, fontPx_);
        font_ = CreateFontW(fontHeight, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

        const int hintHeight = -scalePx(hwnd_, 12);
        hintFont_ = CreateFontW(hintHeight, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    }

    void destroyFonts() {
        if (font_) {
            DeleteObject(font_);
            font_ = nullptr;
        }
        if (hintFont_) {
            DeleteObject(hintFont_);
            hintFont_ = nullptr;
        }
    }

    void recalculateLayout() {
        if (!hwnd_) {
            return;
        }

        padX_ = scalePx(hwnd_, 7);
        padY_ = scalePx(hwnd_, 3);
        labelGap_ = scalePx(hwnd_, 6);
        borderWidth_ = scalePx(hwnd_, 1);
        borderRadius_ = scalePx(hwnd_, 4);
        minWidth_ = scalePx(hwnd_, 200);
        colSpacing_ = 0;
        rowSpacing_ = 0;
        outerPadding_ = scalePx(hwnd_, 20);
        panelGap_ = scalePx(hwnd_, 16);
        preeditPadX_ = scalePx(hwnd_, 12);
        preeditPadY_ = scalePx(hwnd_, 8);

        HDC hdc = GetDC(hwnd_);
        if (!hdc) {
            return;
        }

        HGDIOBJ oldFont = SelectObject(hdc, font_);
        selKeyWidth_ = 0;
        textWidth_ = 0;
        itemHeight_ = 0;
        for (size_t i = 0; i < items_.size(); ++i) {
            SIZE labelSize = {};
            const std::wstring label = candidateLabel(i);
            GetTextExtentPoint32W(hdc, label.c_str(), static_cast<int>(label.size()), &labelSize);
            selKeyWidth_ = (std::max)(selKeyWidth_, static_cast<int>(labelSize.cx));

            SIZE textSize = {};
            GetTextExtentPoint32W(hdc, items_[i].c_str(), static_cast<int>(items_[i].size()), &textSize);
            textWidth_ = (std::max)(textWidth_, static_cast<int>(textSize.cx));
            itemHeight_ = (std::max)(itemHeight_, static_cast<int>(textSize.cy));
        }

        SIZE preeditSize = {};
        GetTextExtentPoint32W(hdc, preedit_.c_str(), static_cast<int>(preedit_.size()), &preeditSize);

        TEXTMETRICW metrics = {};
        GetTextMetricsW(hdc, &metrics);
        itemHeight_ = (std::max)(itemHeight_, static_cast<int>(metrics.tmHeight + metrics.tmExternalLeading + scalePx(hwnd_, 2)));

        SelectObject(hdc, hintFont_);
        RECT hintMeasure = {0, 0, scalePx(hwnd_, 520), 0};
        DrawTextW(hdc,
                  L"Preview only. Up/Down select, +/- font size, C/J/L switch sample, H toggle highlight, M toggle columns.",
                  -1, &hintMeasure, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);

        SelectObject(hdc, oldFont);
        ReleaseDC(hwnd_, hdc);

        const int itemWidth = selKeyWidth_ + labelGap_ + textWidth_;
        const int columns = (std::max)(1, (std::min)(candPerRow_, static_cast<int>(items_.size())));
        const int rows = items_.empty() ? 0 : (static_cast<int>(items_.size()) + candPerRow_ - 1) / candPerRow_;
        const int panelContentWidth = columns * itemWidth + (std::max)(0, columns - 1) * colSpacing_;
        const int panelWidth = (std::max)(minWidth_, padX_ * 2 + panelContentWidth) + borderWidth_ * 2;
        const int panelHeight = padY_ * 2 + rows * itemHeight_ + (std::max)(0, rows - 1) * rowSpacing_ + borderWidth_ * 2;

        preeditRect_.left = outerPadding_;
        preeditRect_.top = outerPadding_;
        preeditRect_.right = preeditRect_.left + preeditSize.cx + preeditPadX_ * 2;
        preeditRect_.bottom = preeditRect_.top + preeditSize.cy + preeditPadY_ * 2;

        panelRect_.left = outerPadding_;
        panelRect_.top = preeditRect_.bottom + panelGap_;
        panelRect_.right = panelRect_.left + panelWidth;
        panelRect_.bottom = panelRect_.top + panelHeight;

        hintRect_.left = outerPadding_;
        hintRect_.top = panelRect_.bottom + panelGap_;
        hintRect_.right = hintRect_.left + (hintMeasure.right - hintMeasure.left);
        hintRect_.bottom = hintRect_.top + (hintMeasure.bottom - hintMeasure.top);

        contentWidth_ = (std::max)(panelRect_.right, hintRect_.right) + outerPadding_;
        contentHeight_ = hintRect_.bottom + outerPadding_;
    }

    void resizeWindowToContent() {
        if (!hwnd_) {
            return;
        }
        RECT outer = {0, 0, contentWidth_, contentHeight_};
        const DWORD style = static_cast<DWORD>(GetWindowLongPtrW(hwnd_, GWL_STYLE));
        const DWORD exStyle = static_cast<DWORD>(GetWindowLongPtrW(hwnd_, GWL_EXSTYLE));
        AdjustWindowRectEx(&outer, style, FALSE, exStyle);
        SetWindowPos(hwnd_, nullptr, 0, 0, outer.right - outer.left, outer.bottom - outer.top,
                     SWP_NOMOVE | SWP_NOACTIVATE | SWP_NOZORDER);
    }

    void paintPanel(HDC hdc) {
        HRGN region = CreateRoundRectRgn(panelRect_.left, panelRect_.top,
                                         panelRect_.right + 1, panelRect_.bottom + 1,
                                         borderRadius_ * 2, borderRadius_ * 2);
        HBRUSH panelBrush = CreateSolidBrush(kPanelBackground);
        HBRUSH borderBrush = CreateSolidBrush(kPanelBorder);
        FillRgn(hdc, region, panelBrush);
        FrameRgn(hdc, region, borderBrush, borderWidth_, borderWidth_);
        DeleteObject(panelBrush);
        DeleteObject(borderBrush);
        DeleteObject(region);

        HFONT oldFont = static_cast<HFONT>(SelectObject(hdc, font_));
        SetBkMode(hdc, TRANSPARENT);

        int col = 0;
        int x = panelRect_.left + borderWidth_ + padX_;
        int y = panelRect_.top + borderWidth_ + padY_;
        for (int i = 0; i < static_cast<int>(items_.size()); ++i) {
            paintItem(hdc, i, x, y);
            ++col;
            if (col >= candPerRow_) {
                col = 0;
                x = panelRect_.left + borderWidth_ + padX_;
                y += itemHeight_ + rowSpacing_;
            } else {
                x += selKeyWidth_ + labelGap_ + textWidth_ + colSpacing_;
            }
        }

        SelectObject(hdc, oldFont);
    }

    void paintItem(HDC hdc, int index, int x, int y) {
        const bool selected = useCursor_ && index == currentSel_;
        RECT itemRect = {x, y, x + selKeyWidth_ + labelGap_ + textWidth_, y + itemHeight_};
        RECT highlightRect = itemRect;
        RECT labelRect = itemRect;
        labelRect.right = labelRect.left + selKeyWidth_;
        RECT textRect = itemRect;
        textRect.left += selKeyWidth_ + labelGap_;

        if (selected) {
            if (candPerRow_ == 1) {
                highlightRect.left = panelRect_.left + borderWidth_ + padX_;
                highlightRect.right = panelRect_.right - borderWidth_ - padX_;
            }
            fillSolidRect(hdc, highlightRect, kSelectedBackground);
        }

        const std::wstring label = candidateLabel(static_cast<size_t>(index));
        SetTextColor(hdc, selected ? kSelectedAuxText : kItemAuxText);
        DrawTextW(hdc, label.c_str(), static_cast<int>(label.size()), &labelRect,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

        SetTextColor(hdc, selected ? kSelectedText : kItemText);
        DrawTextW(hdc, items_[index].c_str(), static_cast<int>(items_[index].size()), &textRect,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }

    void onPaint() {
        PAINTSTRUCT ps = {};
        HDC hdc = BeginPaint(hwnd_, &ps);

        RECT client = {};
        GetClientRect(hwnd_, &client);
        HDC memdc = CreateCompatibleDC(hdc);
        HBITMAP membmp = CreateCompatibleBitmap(hdc, client.right - client.left, client.bottom - client.top);
        HGDIOBJ oldBitmap = SelectObject(memdc, membmp);

        fillSolidRect(memdc, client, kAppBackground);

        HFONT oldFont = static_cast<HFONT>(SelectObject(memdc, font_));
        SetBkMode(memdc, TRANSPARENT);
        SetTextColor(memdc, kItemText);

        HRGN preeditRegion = CreateRoundRectRgn(preeditRect_.left, preeditRect_.top,
                                                preeditRect_.right + 1, preeditRect_.bottom + 1,
                                                scalePx(hwnd_, 8) * 2, scalePx(hwnd_, 8) * 2);
        HBRUSH preeditBrush = CreateSolidBrush(kPreeditBackground);
        FillRgn(memdc, preeditRegion, preeditBrush);
        DeleteObject(preeditBrush);
        DeleteObject(preeditRegion);

        RECT preeditTextRect = preeditRect_;
        preeditTextRect.left += preeditPadX_;
        preeditTextRect.top += preeditPadY_;
        DrawTextW(memdc, preedit_.c_str(), static_cast<int>(preedit_.size()), &preeditTextRect,
                  DT_LEFT | DT_TOP | DT_SINGLELINE | DT_NOPREFIX);

        paintPanel(memdc);

        SelectObject(memdc, hintFont_);
        SetTextColor(memdc, kHintText);
        RECT hintTextRect = hintRect_;
        DrawTextW(memdc,
                  L"Preview only. Up/Down select, +/- font size, C/J/L switch sample, H toggle highlight, M toggle columns.",
                  -1, &hintTextRect, DT_WORDBREAK | DT_NOPREFIX);

        SelectObject(memdc, oldFont);
        BitBlt(hdc, 0, 0, client.right - client.left, client.bottom - client.top, memdc, 0, 0, SRCCOPY);

        SelectObject(memdc, oldBitmap);
        DeleteObject(membmp);
        DeleteDC(memdc);
        EndPaint(hwnd_, &ps);
    }

private:
    HWND hwnd_ = nullptr;
    HFONT font_ = nullptr;
    HFONT hintFont_ = nullptr;
    std::wstring preedit_ = L"xm zd";
    std::vector<std::wstring> items_;
    size_t sampleIndex_ = 0;
    int currentSel_ = 0;
    int candPerRow_ = 1;
    bool useCursor_ = true;
    int fontPx_ = 16;

    int selKeyWidth_ = 0;
    int textWidth_ = 0;
    int itemHeight_ = 0;
    int colSpacing_ = 0;
    int rowSpacing_ = 0;
    int padX_ = 0;
    int padY_ = 0;
    int labelGap_ = 0;
    int borderWidth_ = 0;
    int borderRadius_ = 0;
    int minWidth_ = 0;
    int outerPadding_ = 0;
    int panelGap_ = 0;
    int preeditPadX_ = 0;
    int preeditPadY_ = 0;
    int contentWidth_ = 640;
    int contentHeight_ = 480;

    RECT preeditRect_ = {};
    RECT panelRect_ = {};
    RECT hintRect_ = {};
};

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    PreviewWindow window;
    if (!window.create(instance)) {
        return 1;
    }

    MSG msg = {};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return static_cast<int>(msg.wParam);
}
