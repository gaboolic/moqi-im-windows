//
//    Copyright (C) 2026
//

#pragma once

#include <LibIME2/src/ComObject.h>
#include <LibIME2/src/ImeWindow.h>

#include <string>
#include <vector>

namespace Ime {
class EditSession;
}

namespace Moqi {

struct CandidateUiItem {
    std::wstring text;
    std::wstring comment;

    bool operator==(const CandidateUiItem& other) const {
        return text == other.text && comment == other.comment;
    }

    bool operator!=(const CandidateUiItem& other) const {
        return !(*this == other);
    }

    std::wstring combinedText() const {
        if (comment.empty()) {
            return text;
        }
        return text + L" " + comment;
    }
};

class CandidateWindow
    : public Ime::ImeWindow,
      public Ime::ComObject<Ime::ComInterface<ITfCandidateListUIElement>> {
public:
    CandidateWindow(Ime::TextService* service, Ime::EditSession* session);

    STDMETHODIMP GetDescription(BSTR* pbstrDescription);
    STDMETHODIMP GetGUID(GUID* pguid);
    STDMETHODIMP Show(BOOL bShow);
    STDMETHODIMP IsShown(BOOL* pbShow);

    STDMETHODIMP GetUpdatedFlags(DWORD* pdwFlags);
    STDMETHODIMP GetDocumentMgr(ITfDocumentMgr** ppdim);
    STDMETHODIMP GetCount(UINT* puCount);
    STDMETHODIMP GetSelection(UINT* puIndex);
    STDMETHODIMP GetString(UINT uIndex, BSTR* pbstr);
    STDMETHODIMP GetPageIndex(UINT* puIndex, UINT uSize, UINT* puPageCnt);
    STDMETHODIMP SetPageIndex(UINT* puIndex, UINT uPageCnt);
    STDMETHODIMP GetCurrentPage(UINT* puPage);

    void add(CandidateUiItem item, wchar_t selKey);
    void clear();
    void setCandPerRow(int n);
    void setCandSpacing(int spacing);
    void setCurrentSel(int sel);
    void setUseCursor(bool use);
    void setPreeditText(std::wstring text);
    void setPreeditCursor(int cursor);
    void setCommentFont(HFONT font);
    void setBackgroundColor(COLORREF color);
    void setHighlightColor(COLORREF color);
    void setTextColor(COLORREF color);
    void setHighlightTextColor(COLORREF color);
    void setCommentColor(COLORREF color);
    void setCommentHighlightColor(COLORREF color);
    void syncOwner(Ime::EditSession* session);
    void recalculateSize() override;

protected:
    ~CandidateWindow(void) override;

    LRESULT wndProc(UINT msg, WPARAM wp, LPARAM lp) override;

private:
    void onPaint();
    void paintItem(HDC hdc, int index, int x, int y);
    void itemRect(int index, RECT& rect) const;
    int hitTestCandidate(POINT pt) const;
    void onLButtonDown(WPARAM wp, LPARAM lp);
    void onLButtonUp(WPARAM wp, LPARAM lp);
    void onMouseMove(WPARAM wp, LPARAM lp);
    void onMouseLeave();
    void onMouseWheel(WPARAM wp, LPARAM lp);
    void paintPreeditCursor(HDC hdc, const RECT& preeditRc);
    void applyWindowShape();

private:
    BOOL shown_;
    int selKeyWidth_;
    int textWidth_;
    int commentWidth_;
    int itemHeight_;
    int candPerRow_;
    int candSpacing_;
    int colSpacing_;
    int rowSpacing_;
    int padX_;
    int padY_;
    int labelGap_;
    int commentGap_;
    int borderWidth_;
    int borderRadius_;
    int minWidth_;
    int preeditHeight_;
    int preeditGap_;
    int contentTop_;
    COLORREF backgroundColor_;
    COLORREF highlightColor_;
    COLORREF textColor_;
    COLORREF highlightTextColor_;
    COLORREF commentColor_;
    COLORREF commentHighlightColor_;
    std::wstring preedit_;
    int preeditCursor_;
    HFONT commentFont_;
    std::vector<wchar_t> selKeys_;
    std::vector<CandidateUiItem> items_;
    int currentSel_;
    int pressedSel_;
    bool draggingWindow_;
    bool trackingMouse_;
    bool useCursor_;
};

} // namespace Moqi
