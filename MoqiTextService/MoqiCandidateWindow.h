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

    void add(std::wstring item, wchar_t selKey);
    void clear();
    void setCandPerRow(int n);
    void setCurrentSel(int sel);
    void setUseCursor(bool use);
    void setPreeditText(std::wstring text);
    void setBackgroundColor(COLORREF color);
    void setHighlightColor(COLORREF color);
    void setTextColor(COLORREF color);
    void setHighlightTextColor(COLORREF color);
    void recalculateSize() override;

protected:
    ~CandidateWindow(void) override;

    LRESULT wndProc(UINT msg, WPARAM wp, LPARAM lp) override;

private:
    void onPaint();
    void paintItem(HDC hdc, int index, int x, int y);
    void itemRect(int index, RECT& rect) const;
    void applyWindowShape();

private:
    BOOL shown_;
    int selKeyWidth_;
    int textWidth_;
    int itemHeight_;
    int candPerRow_;
    int colSpacing_;
    int rowSpacing_;
    int padX_;
    int padY_;
    int labelGap_;
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
    std::wstring preedit_;
    std::vector<wchar_t> selKeys_;
    std::vector<std::wstring> items_;
    int currentSel_;
    bool useCursor_;
};

} // namespace Moqi
