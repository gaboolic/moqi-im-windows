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

#ifndef NODE_TEXT_SERVICE_H
#define NODE_TEXT_SERVICE_H

#include <LibIME2/src/TextService.h>
#include <LibIME2/src/MessageWindow.h>
#include <LibIME2/src/EditSession.h>
#include <LibIME2/src/LangBarButton.h>
#include "MoqiImeModule.h"
#include "MoqiCandidateWindow.h"
#include <sys/types.h>
#include "MoqiClient.h"
#include <memory>


namespace Moqi {

class TextService: public Ime::TextService {
	friend class Client;
public:
	TextService(ImeModule* module);

	virtual void onActivate();
	virtual void onDeactivate();

	virtual void onFocus();
	virtual void onSetFocus() override;
	virtual void onKillFocus() override;
	virtual void onSetThreadFocus() override;
	virtual void onKillThreadFocus() override;

	virtual bool filterKeyDown(Ime::KeyEvent& keyEvent);
	virtual bool onKeyDown(Ime::KeyEvent& keyEvent, Ime::EditSession* session);

	virtual bool filterKeyUp(Ime::KeyEvent& keyEvent);
	virtual bool onKeyUp(Ime::KeyEvent& keyEvent, Ime::EditSession* session);

	virtual bool onPreservedKey(const GUID& guid);

	virtual bool onCommand(UINT id, CommandType type);

	// called when a language bar button needs a menu
	virtual bool onMenu(LangBarButton* btn, ITfMenu* pMenu);

	// called when a language bar button needs a menu
	virtual HMENU onMenu(LangBarButton* btn);

	// called when a compartment value is changed
	virtual void onCompartmentChanged(const GUID& key);

	// called when the keyboard is opened or closed
	virtual void onKeyboardStatusChanged(bool opened);

	// called just before current composition is terminated for doing cleanup.
	// if forced is true, the composition is terminated by others, such as
	// the input focus is grabbed by another application.
	// if forced is false, the composition is terminated gracefully by endComposition().
	virtual void onCompositionTerminated(bool forced);

	virtual void onLangProfileActivated(REFIID lang);

	virtual void onLangProfileDeactivated(REFIID lang);

	// methods called by Moqi::Client
	int candPerRow() const {
		return candPerRow_;
	}

	void setCandPerRow(int candPerRow) {
		candPerRow_ = candPerRow;
	}

	std::wstring selKeys() const {
		return selKeys_;
	}

	void setSelKeys(std::wstring selKeys) {
		selKeys_ = selKeys;
	}

	bool candUseCursor() const {
		return candUseCursor_;
	}

	void setCandUseCursor(bool candUseCursor) {
		candUseCursor_ = candUseCursor;
	}

	std::wstring candFontName() const {
		return candFontName_;
	}

	void setCandFontName(std::wstring candFontName) {
		candFontName_ = candFontName;
		updateFont_ = true;
	}

	int candFontSize() {
		return candFontSize_;
	}

	void setCandFontSize(int candFontSize) {
		candFontSize_ = candFontSize;
		updateFont_ = true;
	}

	COLORREF candBackgroundColor() const {
		return candBackgroundColor_;
	}

	void setCandBackgroundColor(COLORREF color) {
		candBackgroundColor_ = color;
		if (candidateWindow_) {
			candidateWindow_->setBackgroundColor(color);
		}
	}

	COLORREF candHighlightColor() const {
		return candHighlightColor_;
	}

	void setCandHighlightColor(COLORREF color) {
		candHighlightColor_ = color;
		if (candidateWindow_) {
			candidateWindow_->setHighlightColor(color);
		}
	}

	COLORREF candTextColor() const {
		return candTextColor_;
	}

	void setCandTextColor(COLORREF color) {
		candTextColor_ = color;
		if (candidateWindow_) {
			candidateWindow_->setTextColor(color);
		}
	}

	COLORREF candHighlightTextColor() const {
		return candHighlightTextColor_;
	}

	void setCandHighlightTextColor(COLORREF color) {
		candHighlightTextColor_ = color;
		if (candidateWindow_) {
			candidateWindow_->setHighlightTextColor(color);
		}
	}

	bool inlinePreedit() const {
		return inlinePreedit_;
	}

	bool effectiveUiLess() const;

	bool shouldShowOwnedPopupUi() const;

	bool shouldAdvertiseCandidateUi() const;

	bool effectiveInlinePreedit() const {
		return effectiveUiLess() || inlinePreedit_;
	}

	bool effectiveExternalPreedit() const {
		return !effectiveInlinePreedit();
	}

	virtual bool inlinePreeditEnabledForComposition() const override {
		return effectiveInlinePreedit();
	}

	virtual bool shouldUseDummyCompositionAnchor() const override {
		return effectiveExternalPreedit() && autoDummyAnchorCompat_;
	}

	void setInlinePreedit(bool inlinePreedit) {
		inlinePreedit_ = inlinePreedit;
		if (candidateWindow_) {
			candidateWindow_->setPreeditText(effectiveInlinePreedit() ? L"" : candidatePreedit_);
		}
	}

	const std::wstring& candidatePreedit() const {
		return candidatePreedit_;
	}

	void setCandidatePreedit(std::wstring preedit) {
		candidatePreedit_ = preedit;
		if (candidateWindow_) {
			candidateWindow_->setPreeditText(effectiveInlinePreedit() ? L"" : candidatePreedit_);
		}
	}

	bool showingCandidates() {
		return showingCandidates_;
	}

	// candidate window
	void showCandidates(Ime::EditSession* session);
	void updateCandidates(Ime::EditSession* session);
    void updateCandidatesWindow(Ime::EditSession* session);
	void hideCandidates();

	void refreshCandidates();
	void setCandidateCursor(int cursor);
	bool hasCandidateWindow() const {
		return candidateWindow_ != nullptr;
	}

	// message window
	void showMessage(Ime::EditSession* session, std::wstring message, int duration = 3);
    void updateMessageWindow(Ime::EditSession* session);
	void hideMessage();

private:
	virtual ~TextService(void);  // COM object should only be deleted using Release()

	void onMessageTimeout();
	static void CALLBACK onMessageTimeout(HWND hwnd, UINT msg, UINT_PTR id, DWORD time);

	void updateLangButtons(); // update status of language bar buttons

	void createCandidateWindow(Ime::EditSession* session);
	void destroyCandidateWindow();
	int candFontHeight();
	void applyUiLessOverrideState();

	void closeClient();

private:
	bool validCandidateListElementId_;
	DWORD candidateListElementId_;
	bool shouldShowCandidateWindowUI_;
	bool manualUiLessOverride_;
	bool autoDummyAnchorCompat_;
	Ime::ComPtr<Moqi::CandidateWindow> candidateWindow_; // this is a ref-counted COM object and should not be managed with std::unique_ptr
	bool showingCandidates_;
	std::vector<std::wstring> candidates_; // current candidate list
	std::unique_ptr<Ime::MessageWindow> messageWindow_;
	UINT messageTimerId_;
	HFONT font_;
	bool updateFont_;
	int candPerRow_;
	std::wstring selKeys_;
	bool candUseCursor_;
	std::wstring candFontName_;
	int candFontSize_;
	COLORREF candBackgroundColor_;
	COLORREF candHighlightColor_;
	COLORREF candTextColor_;
	COLORREF candHighlightTextColor_;
	bool inlinePreedit_;
	std::wstring candidatePreedit_;

	HMENU popupMenu_;

	std::unique_ptr<Client> client_; // connection client
	GUID currentLangProfile_;
};

}

#endif
