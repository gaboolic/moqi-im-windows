//
//    Copyright (C) 2026
//

#pragma once

#include <ctffunc.h>
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

    std::wstring combinedText() const {
        if (comment.empty()) {
            return text;
        }
        return text + L" " + comment;
    }
};

using CandidateWindowComObject = Ime::ComObject<
    Ime::ComInterface<ITfCandidateListUIElement>,
    Ime::ComInterface<ITfCandidateListUIElementBehavior>,
    Ime::ComInterface<ITfIntegratableCandidateListUIElement>>;

class CandidateWindow
    : public Ime::ImeWindow,
      public CandidateWindowComObject {
public:
    CandidateWindow(Ime::TextService* service, Ime::EditSession* session);
    STDMETHODIMP QueryInterface(REFIID riid, void** ppvObj) override;

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
    STDMETHODIMP SetSelection(UINT nIndex);
    STDMETHODIMP Finalize();
    STDMETHODIMP Abort();
    STDMETHODIMP SetIntegrationStyle(GUID guidIntegrationStyle);
    STDMETHODIMP GetSelectionStyle(TfIntegratableCandidateListSelectionStyle* ptfSelectionStyle);
    STDMETHODIMP OnKeyDown(WPARAM wParam, LPARAM lParam, BOOL* pfEaten);
    STDMETHODIMP ShowCandidateNumbers(BOOL* pfShow);
    STDMETHODIMP FinalizeExactCompositionString();

    void add(CandidateUiItem item, wchar_t selKey);
    void clear();
    void setCandPerRow(int n);
    void setCurrentSel(int sel);
    void setUseCursor(bool use);
    int currentSel() const {
        return currentSel_;
    }
    void setPreeditText(std::wstring text);
    void setCommentFont(HFONT font);
    void setBackgroundColor(COLORREF color);
    void setHighlightColor(COLORREF color);
    void setTextColor(COLORREF color);
    void setHighlightTextColor(COLORREF color);
    void syncOwner(Ime::EditSession* session);
    void forceRedraw();
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
    int commentWidth_;
    int itemHeight_;
    int candPerRow_;
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
    std::wstring preedit_;
    HFONT commentFont_;
    std::vector<wchar_t> selKeys_;
    std::vector<CandidateUiItem> items_;
    int currentSel_;
    bool useCursor_;
    GUID integrationStyle_;
};

} // namespace Moqi
