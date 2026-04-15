//
//	Copyright (C) 2015 - 2020 Hong Jen Yee (PCMan) <pcman.tw@gmail.com>
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

#include "MoqiClient.h"
#include "libIME2/src/Utils.h"
#include "../proto/ProtoFraming.h"
#include "proto/moqi.pb.h"
#include <algorithm>
#include <json/json.h>

#include "MoqiTextService.h"
#include "MoqiImeModule.h"
#include <Shellapi.h>
#include <VersionHelpers.h> // Provided by Windows SDK >= 8.1
#include <Winnls.h> // for IS_HIGH_SURROGATE() macro for checking UTF16 surrogate pairs
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <memory>
#include <sstream>
#include <thread>

using namespace std;

namespace Moqi {

static constexpr UINT ASYNC_RPC_POLL_INTERVAL_MS = 50;

namespace {

std::wstring quotePairLogPath() {
  const wchar_t *localAppData = _wgetenv(L"LOCALAPPDATA");
  if (!localAppData || !*localAppData) {
    return L"";
  }
  return std::wstring(localAppData) + L"\\MoqiIM\\Log\\quote-pair-debug.log";
}

void appendQuotePairLog(const std::wstring &message) {
  const std::wstring logPath = quotePairLogPath();
  if (logPath.empty()) {
    return;
  }

  SYSTEMTIME now{};
  ::GetLocalTime(&now);
  wchar_t timestamp[32] = {};
  swprintf_s(timestamp, L"%04d-%02d-%02d %02d:%02d:%02d", now.wYear,
             now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond);

  std::wofstream stream(logPath, std::ios::app);
  if (!stream.is_open()) {
    return;
  }
  stream << L"[" << timestamp << L"] " << message << L"\n";
}

std::wstring formatCodePoints(const std::wstring &text) {
  if (text.empty()) {
    return L"(empty)";
  }

  std::wostringstream stream;
  stream << std::uppercase << std::hex;
  for (size_t i = 0; i < text.size(); ++i) {
    if (i != 0) {
      stream << L" ";
    }
    stream << L"U+" << std::setw(4) << std::setfill(L'0')
           << static_cast<unsigned int>(text[i]);
  }
  return stream.str();
}

bool loadAutoPairQuotesSetting(bool fallbackValue) {
  const wchar_t *appData = _wgetenv(L"APPDATA");
  if (!appData || !*appData) {
    return fallbackValue;
  }

  Json::Value config;
  std::ifstream stream(std::wstring(appData) + L"\\Moqi\\appearance_config.json",
                       std::ifstream::binary);
  if (!stream) {
    return fallbackValue;
  }
  stream >> config;
  const Json::Value &value = config["auto_pair_quotes"];
  if (value.isBool()) {
    return value.asBool();
  }
  return fallbackValue;
}

bool shouldAutoPairQuote(const std::wstring &commitString,
                         std::wstring &pairedString) {
  if (commitString == L"“" || commitString == L"”") {
    pairedString = L"“”";
    return true;
  }
  if (commitString == L"‘" || commitString == L"’") {
    pairedString = L"‘’";
    return true;
  }
  return false;
}

void sendDelayedLeftArrow(HWND targetWindow) {
  if (targetWindow == nullptr) {
    appendQuotePairLog(L"[caretMove] skipped target_window=null");
    return;
  }

  std::thread([targetWindow]() {
    ::Sleep(25);
    HWND foreground = ::GetForegroundWindow();
    if (foreground != targetWindow) {
      appendQuotePairLog(L"[caretMove] skipped foreground_changed");
      return;
    }

    INPUT inputs[2] = {};
    inputs[0].type = INPUT_KEYBOARD;
    inputs[0].ki.wVk = VK_LEFT;
    inputs[1].type = INPUT_KEYBOARD;
    inputs[1].ki.wVk = VK_LEFT;
    inputs[1].ki.dwFlags = KEYEVENTF_KEYUP;

    const UINT sent = ::SendInput(2, inputs, sizeof(INPUT));
    if (sent == 2) {
      appendQuotePairLog(L"[caretMove] sent VK_LEFT");
    } else {
      appendQuotePairLog(L"[caretMove] SendInput failed");
    }
  }).detach();
}

} // namespace

static moqi::protocol::Method methodNameToProto(const char *methodName) {
  if (strcmp(methodName, "init") == 0)
    return moqi::protocol::METHOD_INIT;
  if (strcmp(methodName, "onActivate") == 0)
    return moqi::protocol::METHOD_ON_ACTIVATE;
  if (strcmp(methodName, "onDeactivate") == 0)
    return moqi::protocol::METHOD_ON_DEACTIVATE;
  if (strcmp(methodName, "filterKeyDown") == 0)
    return moqi::protocol::METHOD_FILTER_KEY_DOWN;
  if (strcmp(methodName, "onKeyDown") == 0)
    return moqi::protocol::METHOD_ON_KEY_DOWN;
  if (strcmp(methodName, "filterKeyUp") == 0)
    return moqi::protocol::METHOD_FILTER_KEY_UP;
  if (strcmp(methodName, "onKeyUp") == 0)
    return moqi::protocol::METHOD_ON_KEY_UP;
  if (strcmp(methodName, "onPreservedKey") == 0)
    return moqi::protocol::METHOD_ON_PRESERVED_KEY;
  if (strcmp(methodName, "onCommand") == 0)
    return moqi::protocol::METHOD_ON_COMMAND;
  if (strcmp(methodName, "onMenu") == 0)
    return moqi::protocol::METHOD_ON_MENU;
  if (strcmp(methodName, "onCompartmentChanged") == 0)
    return moqi::protocol::METHOD_ON_COMPARTMENT_CHANGED;
  if (strcmp(methodName, "onKeyboardStatusChanged") == 0)
    return moqi::protocol::METHOD_ON_KEYBOARD_STATUS_CHANGED;
  if (strcmp(methodName, "onCompositionTerminated") == 0)
    return moqi::protocol::METHOD_ON_COMPOSITION_TERMINATED;
  return moqi::protocol::METHOD_UNSPECIFIED;
}

static Json::Value menuItemsToJson(
    const google::protobuf::RepeatedPtrField<moqi::protocol::MenuItem> &items) {
  Json::Value result(Json::arrayValue);
  for (const auto &item : items) {
    Json::Value jsonItem;
    if (item.separator()) {
      jsonItem["id"] = 0;
      jsonItem["text"] = "";
    } else {
      jsonItem["id"] = item.id();
      jsonItem["text"] = item.text();
      jsonItem["checked"] = item.checked();
      jsonItem["enabled"] = item.enabled();
    }
    if (!item.submenu().empty()) {
      jsonItem["submenu"] = menuItemsToJson(item.submenu());
    }
    result.append(jsonItem);
  }
  return result;
}

static Json::Value customizeUiToJson(const moqi::protocol::CustomizeUi &ui) {
  Json::Value result;
  if (ui.has_cand_font_name())
    result["candFontName"] = ui.cand_font_name();
  if (ui.has_cand_font_size())
    result["candFontSize"] = ui.cand_font_size();
  if (ui.has_cand_comment_font_size())
    result["candCommentFontSize"] = ui.cand_comment_font_size();
  if (ui.has_cand_per_row())
    result["candPerRow"] = ui.cand_per_row();
  if (ui.has_cand_use_cursor())
    result["candUseCursor"] = ui.cand_use_cursor();
  if (ui.has_inline_preedit())
    result["inlinePreedit"] = ui.inline_preedit();
  if (ui.has_cand_background_color())
    result["candBackgroundColor"] = ui.cand_background_color();
  if (ui.has_cand_highlight_color())
    result["candHighlightColor"] = ui.cand_highlight_color();
  if (ui.has_cand_text_color())
    result["candTextColor"] = ui.cand_text_color();
  if (ui.has_cand_highlight_text_color())
    result["candHighlightTextColor"] = ui.cand_highlight_text_color();
  return result;
}

static Json::Value buttonInfoToJson(const moqi::protocol::ButtonInfo &button) {
  Json::Value result;
  result["id"] = button.id();
  if (!button.icon().empty())
    result["icon"] = button.icon();
  if (!button.text().empty())
    result["text"] = button.text();
  if (!button.tooltip().empty())
    result["tooltip"] = button.tooltip();
  if (button.command_id() != 0)
    result["commandId"] = button.command_id();
  switch (button.type()) {
  case moqi::protocol::BUTTON_TYPE_BUTTON:
    result["type"] = "button";
    break;
  case moqi::protocol::BUTTON_TYPE_TOGGLE:
    result["type"] = "toggle";
    break;
  case moqi::protocol::BUTTON_TYPE_MENU:
    result["type"] = "menu";
    break;
  default:
    break;
  }
  result["enable"] = button.enable();
  result["toggled"] = button.toggled();
  if (button.has_style())
    result["style"] = button.style();
  return result;
}

static Json::Value responseToJson(const moqi::protocol::ServerResponse &response) {
  Json::Value result;
  result["success"] = response.success();
  result["seqNum"] = response.seq_num();
  result["return"] = response.return_value();
  result["compositionString"] = response.composition_string();
  result["showCandidates"] = response.show_candidates();
  result["cursorPos"] = response.cursor_pos();
  result["compositionCursor"] = response.composition_cursor();
  result["candidateCursor"] = response.candidate_cursor();
  result["selStart"] = response.sel_start();
  result["selEnd"] = response.sel_end();

  Json::Value candidateList(Json::arrayValue);
  for (const auto &candidate : response.candidate_list()) {
    candidateList.append(candidate);
  }
  result["candidateList"] = candidateList;
  if (!response.candidate_entries().empty()) {
    Json::Value candidateEntries(Json::arrayValue);
    for (const auto &candidate : response.candidate_entries()) {
      Json::Value item;
      item["text"] = candidate.text();
      if (!candidate.comment().empty())
        item["comment"] = candidate.comment();
      candidateEntries.append(item);
    }
    result["candidateEntries"] = candidateEntries;
  }

  if (!response.menu_items().empty()) {
    result["return"] = menuItemsToJson(response.menu_items());
  }
  if (!response.commit_string().empty())
    result["commitString"] = response.commit_string();
  if (!response.set_sel_keys().empty())
    result["setSelKeys"] = response.set_sel_keys();
  if (response.has_customize_ui())
    result["customizeUI"] = customizeUiToJson(response.customize_ui());
  if (response.has_show_message()) {
    Json::Value message;
    message["message"] = response.show_message().message();
    message["duration"] = response.show_message().duration();
    result["showMessage"] = message;
  }
  if (response.hide_message())
    result["hideMessage"] = true;
  if (response.open_keyboard())
    result["openKeyboard"] = true;

  if (!response.add_button().empty()) {
    Json::Value addButtons(Json::arrayValue);
    for (const auto &button : response.add_button()) {
      addButtons.append(buttonInfoToJson(button));
    }
    result["addButton"] = addButtons;
  }
  if (!response.remove_button().empty()) {
    Json::Value removeButtons(Json::arrayValue);
    for (const auto &buttonId : response.remove_button()) {
      removeButtons.append(buttonId);
    }
    result["removeButton"] = removeButtons;
  }
  if (!response.change_button().empty()) {
    Json::Value changeButtons(Json::arrayValue);
    for (const auto &button : response.change_button()) {
      changeButtons.append(buttonInfoToJson(button));
    }
    result["changeButton"] = changeButtons;
  }
  if (!response.add_preserved_key().empty()) {
    Json::Value preservedKeys(Json::arrayValue);
    for (const auto &item : response.add_preserved_key()) {
      Json::Value key;
      key["keyCode"] = item.key_code();
      key["modifiers"] = item.modifiers();
      key["guid"] = item.guid();
      preservedKeys.append(key);
    }
    result["addPreservedKey"] = preservedKeys;
  }
  if (!response.remove_preserved_key().empty()) {
    Json::Value preservedKeys(Json::arrayValue);
    for (const auto &guid : response.remove_preserved_key()) {
      preservedKeys.append(guid);
    }
    result["removePreservedKey"] = preservedKeys;
  }
  if (!response.error().empty())
    result["error"] = response.error();
  return result;
}

static std::string uuidToString(const UUID &uuid) {
  std::string result;
  LPOLESTR buf = nullptr;
  if (SUCCEEDED(::StringFromCLSID(uuid, &buf))) {
    result = utf16ToUtf8(buf);
    ::CoTaskMemFree(buf);
    // convert GUID to lower case
    std::transform(
        result.begin(), result.end(), result.begin(),
        [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
  }
  return result;
}

bool uuidFromString(const char *uuidStr, UUID &result) {
  std::wstring utf16UuidStr = utf8ToUtf16(uuidStr);
  return SUCCEEDED(CLSIDFromString(utf16UuidStr.c_str(), &result));
}

bool parseHexColor(const std::string &text, COLORREF &result) {
  std::string value = text;
  if (!value.empty() && value.front() == '#') {
    value.erase(value.begin());
  }
  if (value.size() != 6) {
    return false;
  }
  char *end = nullptr;
  const unsigned long rgb = std::strtoul(value.c_str(), &end, 16);
  if (end == nullptr || *end != '\0') {
    return false;
  }
  result = RGB((rgb >> 16) & 0xff, (rgb >> 8) & 0xff, rgb & 0xff);
  return true;
}

Client::Client(TextService *service, REFIID langProfileGuid)
    : textService_(service), pipe_(INVALID_HANDLE_VALUE), nextSeqNum_(0),
      isActivated_(false), guid_{uuidToString(langProfileGuid)},
      shouldWaitConnection_{true}, launcherStartAttempted_{false},
      asyncPollTimerWindow_(nullptr),
      asyncPollTimerId_(0) {}

Client::~Client(void) {
  if (asyncPollTimerId_ != 0) {
    ::KillTimer(asyncPollTimerWindow_, asyncPollTimerId_);
    asyncPollTimerId_ = 0;
    asyncPollTimerWindow_ = nullptr;
  }
  closeRpcConnection();
  resetTextServiceState();
  LangBarButton::clearIconCache();
}

// pack a keyEvent object into a json value
// static
void Client::addKeyEventToRpcRequest(moqi::protocol::ClientRequest &request,
                                     Ime::KeyEvent &keyEvent) {
  auto *protoKeyEvent = request.mutable_key_event();
  protoKeyEvent->set_char_code(keyEvent.charCode());
  protoKeyEvent->set_key_code(keyEvent.keyCode());
  protoKeyEvent->set_repeat_count(keyEvent.repeatCount());
  protoKeyEvent->set_scan_code(keyEvent.scanCode());
  protoKeyEvent->set_is_extended(keyEvent.isExtended());
  const BYTE *states = keyEvent.keyStates();
  for (int i = 0; i < 256; ++i) {
    protoKeyEvent->add_key_states(states[i]);
  }
}

bool Client::handleRpcResponse(Json::Value &msg, Ime::EditSession *session) {
  bool success = msg.get("success", false).asBool();
  if (success) {
    updateStatus(msg, session);
  }
  return success;
}

void Client::updateUI(const Json::Value &data) {
  for (auto it = data.begin(); it != data.end(); ++it) {
    const char *name = it.memberName();
    const Json::Value &value = *it;
    if (value.isString() && strcmp(name, "candFontName") == 0) {
      wstring fontName = utf8ToUtf16(value.asCString());
      textService_->setCandFontName(fontName);
    } else if (value.isInt() && strcmp(name, "candFontSize") == 0) {
      textService_->setCandFontSize(value.asInt());
    } else if (value.isInt() && strcmp(name, "candCommentFontSize") == 0) {
      textService_->setCandCommentFontSize(value.asInt());
    } else if (value.isInt() && strcmp(name, "candPerRow") == 0) {
      textService_->setCandPerRow(value.asInt());
    } else if (value.isBool() && strcmp(name, "candUseCursor") == 0) {
      textService_->setCandUseCursor(value.asBool());
    } else if (value.isBool() && strcmp(name, "inlinePreedit") == 0) {
      textService_->setInlinePreedit(value.asBool());
    } else if (value.isBool() && strcmp(name, "autoPairQuotes") == 0) {
      textService_->setAutoPairQuotes(value.asBool());
    } else if (value.isString() && strcmp(name, "candBackgroundColor") == 0) {
      COLORREF color = textService_->candBackgroundColor();
      if (parseHexColor(value.asCString(), color)) {
        textService_->setCandBackgroundColor(color);
      }
    } else if (value.isString() && strcmp(name, "candHighlightColor") == 0) {
      COLORREF color = textService_->candHighlightColor();
      if (parseHexColor(value.asCString(), color)) {
        textService_->setCandHighlightColor(color);
      }
    } else if (value.isString() && strcmp(name, "candTextColor") == 0) {
      COLORREF color = textService_->candTextColor();
      if (parseHexColor(value.asCString(), color)) {
        textService_->setCandTextColor(color);
      }
    } else if (value.isString() && strcmp(name, "candHighlightTextColor") == 0) {
      COLORREF color = textService_->candHighlightTextColor();
      if (parseHexColor(value.asCString(), color)) {
        textService_->setCandHighlightTextColor(color);
      }
    }
  }
  textService_->applyCandidateAppearanceNow();
  textService_->refreshCandidateAppearance();
}

void Client::updateSelectionKeys(Json::Value &msg) {
  // set sel keys before update candidates
  const auto &setSelKeysVal = msg["setSelKeys"];
  if (setSelKeysVal.isString()) {
    // keys used to select candidates
    std::wstring selKeys = utf8ToUtf16(setSelKeysVal.asCString());
    textService_->setSelKeys(selKeys);
  }
}

void Client::updateMessageWindow(Json::Value &msg, Ime::EditSession *session,
                                 bool &endComposition) {
  const auto &showMessageVal = msg["showMessage"];
  if (showMessageVal.isObject()) {
    const Json::Value &message = showMessageVal["message"];
    const Json::Value &duration = showMessageVal["duration"];
    if (message.isString() && duration.isInt()) {
      if (!textService_->isComposing()) {
        textService_->startComposition(session->context());
        endComposition = true;
      }
      textService_->showMessage(session, utf8ToUtf16(message.asCString()),
                                duration.asInt());
    }
  }

  // hide message
  const auto &hideMessageVal = msg["hideMessage"];
  if (hideMessageVal.isBool() && hideMessageVal.asBool()) {
    textService_->hideMessage();
  }
}

void Client::updateCommitString(Json::Value &msg, Ime::EditSession *session) {
  // handle comosition and commit strings
  const auto &commitStringVal = msg["commitString"];
  if (commitStringVal.isString()) {
    const std::wstring rawCommitString = utf8ToUtf16(commitStringVal.asCString());
    const bool autoPairQuotesEnabled =
        loadAutoPairQuotesSetting(textService_->autoPairQuotes());
    std::wstring commitString = rawCommitString;
    const bool isQuoteLikeCommit = rawCommitString == L"“" || rawCommitString == L"”" ||
                                   rawCommitString == L"‘" || rawCommitString == L"’";
    if (isQuoteLikeCommit) {
      appendQuotePairLog(L"[updateCommitString] raw=" +
                         formatCodePoints(rawCommitString) + L" auto_pair_quotes=" +
                         (autoPairQuotesEnabled ? L"true" : L"false"));
    }

    std::wstring pairedCommitString;
    const bool autoPairedQuotes =
        autoPairQuotesEnabled &&
        shouldAutoPairQuote(rawCommitString, pairedCommitString);
    if (autoPairedQuotes) {
      commitString = pairedCommitString;
      appendQuotePairLog(L"[updateCommitString] paired=" +
                         formatCodePoints(commitString));
    }
    if (!commitString.empty()) {
      HWND targetWindow = ::GetForegroundWindow();
      if (!textService_->isComposing()) {
        textService_->startComposition(session->context());
      }
      textService_->setCompositionString(session, commitString.c_str(),
                                         commitString.length());
      if (autoPairedQuotes) {
        textService_->setCompositionCursor(session, 1);
      }
      // FIXME: update the position of candidate and message window when the
      // composition string is changed.
      if (textService_->hasCandidateWindow()) {
        textService_->updateCandidatesWindow(session);
      }
      if (textService_->messageWindow_ != nullptr) {
        textService_->updateMessageWindow(session);
      }
      textService_->endComposition(session->context());
      if (autoPairedQuotes) {
        sendDelayedLeftArrow(targetWindow);
      }
    }
  }
}

void Client::updateComposition(Json::Value &msg, Ime::EditSession *session,
                               bool &endComposition) {
  const auto &compositionStringVal = msg["compositionString"];
  bool emptyComposition = false;
  bool hasCompositionString = false;
  std::wstring compositionString;
  if (compositionStringVal.isString()) {
    // composition buffer
    compositionString = utf8ToUtf16(compositionStringVal.asCString());
    hasCompositionString = true;
    textService_->setCandidatePreedit(compositionString);
    if (!textService_->effectiveInlinePreedit()) {
      emptyComposition = compositionString.empty();
      if (!compositionString.empty()) {
        if (!textService_->isComposing()) {
          textService_->startComposition(session->context());
        }
        // Keep the TSF composition alive for key routing/anchor positioning,
        // but leave it visually empty because the preedit is rendered
        // externally in the candidate window.
        textService_->setCompositionString(session, L"", 0);
        textService_->showCandidates(session);
        textService_->updateCandidates(session);
      }
      if (textService_->hasCandidateWindow()) {
        textService_->refreshCandidates();
        textService_->updateCandidatesWindow(session);
      }
      if (textService_->messageWindow_ != nullptr) {
        textService_->updateMessageWindow(session);
      }
      if (compositionString.empty() && textService_->isComposing() &&
          !textService_->showingCandidates()) {
        textService_->setCompositionString(session, L"", 0);
        endComposition = true;
      }
    } else {
      if (compositionString.empty()) {
        textService_->setCandidatePreedit(L"");
        emptyComposition = true;
        if (textService_->isComposing() && !textService_->showingCandidates()) {
          // when the composition buffer is empty and we are not showing the
          // candidate list, end composition.
          textService_->setCompositionString(session, L"", 0);
          endComposition = true;
        }
      } else {
        if (!textService_->isComposing()) {
          textService_->startComposition(session->context());
        }
        textService_->setCompositionString(session, compositionString.c_str(),
                                           compositionString.length());
      }
      // FIXME: update the position of candidate and message window when the
      // composition string is changed.
      if (textService_->hasCandidateWindow()) {
        textService_->updateCandidatesWindow(session);
      }
      if (textService_->messageWindow_ != nullptr) {
        textService_->updateMessageWindow(session);
      }
    }
  }

  const auto &compositionCursorVal = msg["compositionCursor"];
  if (compositionCursorVal.isInt()) {
    // composition cursor
    if (textService_->effectiveInlinePreedit() && !emptyComposition) {
      int compositionCursor = compositionCursorVal.asInt();
      if (!textService_->isComposing()) {
        textService_->startComposition(session->context());
      }
      // NOTE:
      // This fixes Moqi bug #166: incorrect handling of UTF-16 surrogate pairs.
      // The TSF API unfortunately treat a UTF-16 surrogate pair as two
      // characters while they actually represent one unicode character only. To
      // workaround this TSF bug, we get the composition string, and try to move
      // the cursor twice when a UTF-16 surrogate pair is found.
      if (!hasCompositionString)
        compositionString = textService_->compositionString(session);
      int fixedCursorPos = 0;
      for (int i = 0; i < compositionCursor; ++i) {
        ++fixedCursorPos;
        if (IS_HIGH_SURROGATE(
                compositionString[i])) // this is the first part of a UTF16
                                       // surrogate pair (Windows uses UTF16-LE)
          ++fixedCursorPos;
      }
      textService_->setCompositionCursor(session, fixedCursorPos);
    }
  }
  if (endComposition) {
    textService_->endComposition(session->context());
  }
}

void Client::updateLanguageButtons(Json::Value &msg) {
  // language buttons
  const auto &addButtonVal = msg["addButton"];
  if (addButtonVal.isArray()) {
    for (const auto &btn : addButtonVal) {
      // FIXME: when to clear the id <=> button map??
      auto langBtn = Ime::ComPtr<Moqi::LangBarButton>::takeover(
          Moqi::LangBarButton::fromJson(textService_, btn));
      if (langBtn != nullptr) {
        buttons_.emplace(langBtn->id(), langBtn); // insert into the map
        textService_->addButton(langBtn);
      }
    }
  }

  const auto &removeButtonVal = msg["removeButton"];
  if (removeButtonVal.isArray()) {
    // FIXME: handle windows-mode-icon
    for (const auto &btn : removeButtonVal) {
      if (btn.isString()) {
        string id = btn.asString();
        auto map_it = buttons_.find(id);
        if (map_it != buttons_.end()) {
          textService_->removeButton(map_it->second);
          buttons_.erase(map_it); // remove from the map
        }
      }
    }
  }
  const auto &changeButtonVal = msg["changeButton"];
  if (changeButtonVal.isArray()) {
    // FIXME: handle windows-mode-icon
    for (const auto &btn : changeButtonVal) {
      if (btn.isObject()) {
        string id = btn["id"].asString();
        auto map_it = buttons_.find(id);
        if (map_it != buttons_.end()) {
          map_it->second->updateFromJson(btn);
        }
      }
    }
  }
}

void Client::updatePreservedKeys(Json::Value &msg) {
  const auto &addPreservedKeyVal = msg["addPreservedKey"];
  if (addPreservedKeyVal.isArray()) {
    // preserved keys
    for (auto &key : addPreservedKeyVal) {
      if (key.isObject()) {
        UINT keyCode = key["keyCode"].asUInt();
        UINT modifiers = key["modifiers"].asUInt();
        UUID guid = {0};
        if (uuidFromString(key["guid"].asCString(), guid)) {
          textService_->addPreservedKey(keyCode, modifiers, guid);
        }
      }
    }
  }

  const auto &removePreservedKeyVal = msg["removePreservedKey"];
  if (removePreservedKeyVal.isArray()) {
    for (auto &item : removePreservedKeyVal) {
      if (item.isString()) {
        UUID guid = {0};
        if (uuidFromString(item.asCString(), guid)) {
          textService_->removePreservedKey(guid);
        }
      }
    }
  }
}

void Client::updateKeyboardStatus(Json::Value &msg) {
  const auto &openKeyboardVal = msg["openKeyboard"];
  if (openKeyboardVal.isBool()) {
    textService_->setKeyboardOpen(openKeyboardVal.asBool());
  }
}

void Client::updateStatus(Json::Value &msg, Ime::EditSession *session) {
  // We need to handle ordering of some types of the requests.
  // For example, setCompositionCursor() should happen after
  // setCompositionCursor().
  updateSelectionKeys(msg);

  // show message
  bool endComposition = false;
  const auto &customizeUIVal = msg["customizeUI"];
  if (customizeUIVal.isObject()) {
    updateUI(customizeUIVal);
  }
  updateMessageWindow(msg, session, endComposition);

  if (session != nullptr) { // if an edit session is available
    updateCandidateList(msg, session);

    updateCommitString(msg, session);

    updateComposition(msg, session, endComposition);
  }

  updateLanguageButtons(msg);

  // preserved keys
  updatePreservedKeys(msg);

  // keyboard status
  updateKeyboardStatus(msg);

}

void Client::updateCandidateList(Json::Value &msg, Ime::EditSession *session) {
  // handle candidate list
  const auto &showCandidatesVal = msg["showCandidates"];
  if (showCandidatesVal.isBool()) {
    if (showCandidatesVal.asBool()) {
      // start composition if we are not composing.
      // this is required to get correctly position the candidate window
      if (!textService_->isComposing()) {
        textService_->startComposition(session->context());
      }
      textService_->showCandidates(session);
    } else {
      textService_->hideCandidates();
    }
  }

  const auto &candidateListVal = msg["candidateList"];
  const auto &candidateEntriesVal = msg["candidateEntries"];
  if (candidateEntriesVal.isArray()) {
    vector<CandidateUiItem> &candidates = textService_->candidates_;
    candidates.clear();
    for (const auto &candidate : candidateEntriesVal) {
      CandidateUiItem item;
      if (candidate.isObject()) {
        if (candidate["text"].isString()) {
          item.text = utf8ToUtf16(candidate["text"].asCString());
        }
        if (candidate["comment"].isString()) {
          item.comment = utf8ToUtf16(candidate["comment"].asCString());
        }
      }
      candidates.emplace_back(std::move(item));
    }
    textService_->updateCandidates(session);
    if (!showCandidatesVal.asBool()) {
      textService_->hideCandidates();
    }
  } else if (candidateListVal.isArray()) {
    // handle candidates
    // FIXME: directly access private member is dirty!!!
    vector<CandidateUiItem> &candidates = textService_->candidates_;
    candidates.clear();
    for (const auto &candidate : candidateListVal) {
      CandidateUiItem item;
      item.text = utf8ToUtf16(candidate.asCString());
      candidates.emplace_back(std::move(item));
    }
    textService_->updateCandidates(session);
    if (!showCandidatesVal.asBool()) {
      textService_->hideCandidates();
    }
  }

  const auto &candidateCursorVal = msg["candidateCursor"];
  if (candidateCursorVal.isInt()) {
    if (textService_->hasCandidateWindow()) {
      textService_->setCandidateCursor(candidateCursorVal.asInt());
      textService_->refreshCandidates();
    }
  }
}

// handlers for the text service
void Client::onActivate() {
  auto req = createRpcRequest("onActivate");
  req.set_is_keyboard_open(textService_->isKeyboardOpened());

  Json::Value ret;
  callRpcMethod(req, ret);
  if (handleRpcResponse(ret)) {
  }
  isActivated_ = true;
}

void Client::onDeactivate() {
  auto req = createRpcRequest("onDeactivate");
  Json::Value ret;
  callRpcMethod(req, ret);
  if (handleRpcResponse(ret)) {
  }
  LangBarButton::clearIconCache();
  isActivated_ = false;
}

bool Client::filterKeyDown(Ime::KeyEvent &keyEvent) {
  auto req = createRpcRequest("filterKeyDown");
  addKeyEventToRpcRequest(req, keyEvent);

  Json::Value ret;
  callRpcMethod(req, ret);
  if (handleRpcResponse(ret)) {
    return ret["return"].asBool();
  }
  return false;
}

bool Client::onKeyDown(Ime::KeyEvent &keyEvent, Ime::EditSession *session) {
  auto req = createRpcRequest("onKeyDown");
  addKeyEventToRpcRequest(req, keyEvent);

  Json::Value ret;
  callRpcMethod(req, ret);
  if (handleRpcResponse(ret, session)) {
    refreshAsyncPollTimer();
    flushPendingAsyncResponses(session);
    return ret["return"].asBool();
  }
  return false;
}

bool Client::filterKeyUp(Ime::KeyEvent &keyEvent) {
  auto req = createRpcRequest("filterKeyUp");
  addKeyEventToRpcRequest(req, keyEvent);

  Json::Value ret;
  callRpcMethod(req, ret);
  if (handleRpcResponse(ret)) {
    return ret["return"].asBool();
  }
  return false;
}

bool Client::onKeyUp(Ime::KeyEvent &keyEvent, Ime::EditSession *session) {
  auto req = createRpcRequest("onKeyUp");
  addKeyEventToRpcRequest(req, keyEvent);

  Json::Value ret;
  callRpcMethod(req, ret);
  if (handleRpcResponse(ret, session)) {
    refreshAsyncPollTimer();
    flushPendingAsyncResponses(session);
    return ret["return"].asBool();
  }
  return false;
}

bool Client::onPreservedKey(const GUID &guid) {
  auto guidStr = uuidToString(guid);
  if (!guidStr.empty()) {
    auto req = createRpcRequest("onPreservedKey");
    req.set_preserved_key_guid(guidStr);

    Json::Value ret;
    callRpcMethod(req, ret);
    if (handleRpcResponse(ret)) {
      return ret["return"].asBool();
    }
  }
  return false;
}

bool Client::onCommand(UINT id, Ime::TextService::CommandType type) {
  auto req = createRpcRequest("onCommand");
  req.set_command_id(id);
  req.set_command_type(type);

  Json::Value ret;
  callRpcMethod(req, ret);
  if (handleRpcResponse(ret)) {
    return ret["return"].asBool();
  }
  return false;
}

bool Client::sendOnMenu(std::string button_id, Json::Value &result) {
  auto req = createRpcRequest("onMenu");
  req.set_button_id(button_id);

  callRpcMethod(req, result);
  if (handleRpcResponse(result)) {
    return true;
  }
  return false;
}

static bool menuFromJson(ITfMenu *pMenu, const Json::Value &menuInfo) {
  if (pMenu != nullptr && menuInfo.isArray()) {
    for (const auto &item : menuInfo) {
      UINT id = item.get("id", 0).asUInt();
      std::wstring text = utf8ToUtf16(item.get("text", "").asCString());

      DWORD flags = 0;
      Json::Value submenuInfo;
      ITfMenu *submenu = nullptr;
      if (id == 0 && text.empty())
        flags = TF_LBMENUF_SEPARATOR;
      else {
        if (item.get("checked", false).asBool())
          flags |= TF_LBMENUF_CHECKED;
        if (!item.get("enabled", true).asBool())
          flags |= TF_LBMENUF_GRAYED;

        submenuInfo =
            item["submenu"]; // FIXME: this is a deep copy. too bad! :-(
        if (submenuInfo.isArray()) {
          flags |= TF_LBMENUF_SUBMENU;
        }
      }
      pMenu->AddMenuItem(id, flags, NULL, NULL, text.c_str(), text.length(),
                         flags & TF_LBMENUF_SUBMENU ? &submenu : nullptr);
      if (submenu != nullptr && submenuInfo.isArray()) {
        menuFromJson(submenu, submenuInfo);
      }
    }
    return true;
  }
  return false;
}

// called when a language bar button needs a menu
// virtual
bool Client::onMenu(LangBarButton *btn, ITfMenu *pMenu) {
  Json::Value result;
  if (sendOnMenu(btn->id(), result)) {
    Json::Value &menuInfo = result["return"];
    return menuFromJson(pMenu, menuInfo);
  }
  return false;
}

static HMENU menuFromJson(const Json::Value &menuInfo) {
  if (menuInfo.isArray()) {
    HMENU menu = ::CreatePopupMenu();
    for (const auto &item : menuInfo) {
      UINT id = item.get("id", 0).asUInt();
      std::wstring text = utf8ToUtf16(item.get("text", "").asCString());

      UINT flags = MF_STRING;
      if (id == 0 && text.empty())
        flags = MF_SEPARATOR;
      else {
        if (item.get("checked", false).asBool())
          flags |= MF_CHECKED;
        if (!item.get("enabled", true).asBool())
          flags |= MF_GRAYED;

        const Json::Value &subMenuValue = item.get("submenu", Json::nullValue);
        if (subMenuValue.isArray()) {
          HMENU submenu = menuFromJson(subMenuValue);
          flags |= MF_POPUP;
          id = UINT_PTR(submenu);
        }
      }
      AppendMenu(menu, flags, id, text.c_str());
    }
    return menu;
  }
  return NULL;
}

// called when a language bar button needs a menu
// virtual
HMENU Client::onMenu(LangBarButton *btn) {
  Json::Value result;
  if (sendOnMenu(btn->id(), result)) {
    Json::Value &menuInfo = result["return"];
    return menuFromJson(menuInfo);
  }
  return NULL;
}

// called when a compartment value is changed
void Client::onCompartmentChanged(const GUID &key) {
  auto guidStr = uuidToString(key);
  if (!guidStr.empty()) {
    auto req = createRpcRequest("onCompartmentChanged");
    req.set_compartment_guid(guidStr);

    Json::Value ret;
    callRpcMethod(req, ret);
    if (handleRpcResponse(ret)) {
    }
  }
}

// called when the keyboard is opened or closed
void Client::onKeyboardStatusChanged(bool opened) {
  auto req = createRpcRequest("onKeyboardStatusChanged");
  req.set_opened(opened);

  Json::Value ret;
  callRpcMethod(req, ret);
  if (handleRpcResponse(ret)) {
  }
}

// called just before current composition is terminated for doing cleanup.
void Client::onCompositionTerminated(bool forced) {
  auto req = createRpcRequest("onCompositionTerminated");
  req.set_forced(forced);

  Json::Value ret;
  callRpcMethod(req, ret);
  if (handleRpcResponse(ret)) {
  }
}

bool Client::init() {
  auto req = createRpcRequest("init");
  req.set_guid(guid_);
  req.set_is_windows8_above(::IsWindows8OrGreater());
  req.set_is_metro_app(textService_->isMetroApp());
  req.set_is_ui_less(textService_->effectiveUiLess());
  req.set_is_console(textService_->isConsole());

  Json::Value ret;
  return callRpcMethod(req, ret) && handleRpcResponse(ret);
}

moqi::protocol::ClientRequest Client::createRpcRequest(const char *methodName) {
  moqi::protocol::ClientRequest request;
  request.set_method(methodNameToProto(methodName));
  return request;
}

void CALLBACK Client::onAsyncPollTimer(HWND, UINT, UINT_PTR id, DWORD) {
  auto *client = reinterpret_cast<Client *>(id);
  if (client != nullptr) {
    client->pollAsyncResponses();
  }
}

void Client::refreshAsyncPollTimer() {
  HWND targetWindow = nullptr;
  if (textService_ != nullptr && textService_->candidateWindow_ != nullptr &&
      textService_->candidateWindow_->isWindow()) {
    targetWindow = textService_->candidateWindow_->hwnd();
  }

  if (asyncPollTimerId_ != 0 &&
      (targetWindow == nullptr || targetWindow != asyncPollTimerWindow_)) {
    ::KillTimer(asyncPollTimerWindow_, asyncPollTimerId_);
    asyncPollTimerId_ = 0;
    asyncPollTimerWindow_ = nullptr;
  }

  if (targetWindow != nullptr && asyncPollTimerId_ == 0) {
    asyncPollTimerId_ =
        ::SetTimer(targetWindow, reinterpret_cast<UINT_PTR>(this),
                   ASYNC_RPC_POLL_INTERVAL_MS, &Client::onAsyncPollTimer);
    asyncPollTimerWindow_ = asyncPollTimerId_ != 0 ? targetWindow : nullptr;
  }
}

bool Client::readPendingPipeMessage(std::string &serializedReply) {
  serializedReply.clear();
  if (pipe_ == INVALID_HANDLE_VALUE) {
    return false;
  }

  DWORD bytesAvailable = 0;
  if (!::PeekNamedPipe(pipe_, nullptr, 0, nullptr, &bytesAvailable, nullptr) ||
      bytesAvailable == 0) {
    return false;
  }

  char buf[1024];
  DWORD rlen = 0;
  bool hasMoreData = false;
  if (!::ReadFile(pipe_, buf, sizeof(buf), &rlen, nullptr)) {
    if (::GetLastError() == ERROR_MORE_DATA) {
      hasMoreData = true;
    } else {
      return false;
    }
  }
  serializedReply.append(buf, rlen);

  while (hasMoreData) {
    if (::ReadFile(pipe_, buf, sizeof(buf), &rlen, nullptr)) {
      hasMoreData = false;
    } else if (::GetLastError() != ERROR_MORE_DATA) {
      return false;
    }
    serializedReply.append(buf, rlen);
  }
  return true;
}

void Client::enqueueAsyncResponse(const moqi::protocol::ServerResponse &response) {
  pendingAsyncResponses_.push_back(responseToJson(response));
}

bool Client::applyAsyncResponse(Json::Value &msg, Ime::EditSession *session) {
  if (session != nullptr) {
    return handleRpcResponse(msg, session);
  }

  auto context = textService_->currentContext();
  if (!context) {
    return false;
  }

  Json::Value responseCopy = msg;
  auto *editSession = new Ime::EditSession(
      context, [this, responseCopy](Ime::EditSession *editSession, TfEditCookie) mutable {
        handleRpcResponse(responseCopy, editSession);
      });
  HRESULT sessionHr = E_FAIL;
  const HRESULT hr = context->RequestEditSession(
      textService_->clientId(), editSession, TF_ES_ASYNC | TF_ES_READWRITE,
      &sessionHr);
  editSession->Release();
  return SUCCEEDED(hr) && SUCCEEDED(sessionHr);
}

void Client::flushPendingAsyncResponses(Ime::EditSession *session) {
  while (!pendingAsyncResponses_.empty()) {
    Json::Value msg = pendingAsyncResponses_.front();
    if (!applyAsyncResponse(msg, session)) {
      break;
    }
    pendingAsyncResponses_.pop_front();
  }
}

void Client::pollAsyncResponses() {
  if (pipe_ == INVALID_HANDLE_VALUE) {
    return;
  }

  std::string serializedResponse;
  while (readPendingPipeMessage(serializedResponse)) {
    Proto::FrameBuffer responseBuffer;
    responseBuffer.append(serializedResponse.data(), serializedResponse.size());
    std::string payload;
    while (responseBuffer.nextFrame(payload)) {
      moqi::protocol::ServerResponse protoResponse;
      if (!Proto::parsePayload(payload, protoResponse)) {
        closeRpcConnection();
        return;
      }
      enqueueAsyncResponse(protoResponse);
    }
  }

  flushPendingAsyncResponses();
}

bool Client::callRpcPipe(HANDLE pipe, const std::string &serializedRequest,
                         std::string &serializedReply) {
  char buf[1024];
  DWORD rlen = 0;
  bool hasMoreData = false;
  if (!TransactNamedPipe(pipe, (void *)serializedRequest.data(),
                         serializedRequest.size(), buf, sizeof(buf), &rlen,
                         NULL)) {
    if (GetLastError() == ERROR_MORE_DATA) {
      hasMoreData = true;
    } else { // unknown error
      return false;
    }
  }
  serializedReply.append(buf, rlen);

  while (hasMoreData) {
    if (ReadFile(pipe, buf, sizeof(buf), &rlen, NULL)) {
      hasMoreData = false;
    } else if (::GetLastError() != ERROR_MORE_DATA) { // unknown error
      return false;
    }
    serializedReply.append(buf, rlen);
  }
  return true;
}

// send the request to the server
// a sequence number will be added to the req object automatically.
bool Client::callRpcMethod(moqi::protocol::ClientRequest &request,
                           Json::Value &response) {
  if (shouldWaitConnection_ && !waitForRpcConnection()) {
    return false;
  }

  pollAsyncResponses();

  // Add a sequence number for the request.
  auto seqNum = nextSeqNum_++;
  request.set_seq_num(seqNum);

  std::string serializedRequest;
  if (!Proto::serializeMessage(request, serializedRequest)) {
    return false;
  }

  std::string serializedResponse;
  bool success = false;
  if (callRpcPipe(pipe_, serializedRequest, serializedResponse)) {
    while (true) {
      Proto::FrameBuffer responseBuffer;
      responseBuffer.append(serializedResponse.data(), serializedResponse.size());
      std::string payload;
      moqi::protocol::ServerResponse protoResponse;
      success = responseBuffer.nextFrame(payload) &&
                Proto::parsePayload(payload, protoResponse);
      if (!success) {
        break;
      }
      if (protoResponse.seq_num() == seqNum) {
        response = responseToJson(protoResponse);
        break;
      }

      enqueueAsyncResponse(protoResponse);
      serializedResponse.clear();
      if (!readPendingPipeMessage(serializedResponse)) {
        success = false;
        break;
      }
    }
  } else {
    success = false;
  }

  flushPendingAsyncResponses();

  if (!success) {            // fail to send the request to the server
    closeRpcConnection();    // close the pipe connection since it's broken
    resetTextServiceState(); // since we lost the connection, the state is
                             // unknonw so we reset.
  }
  return success;
}

bool Client::isPipeCreatedByMoqiServer(HANDLE pipe) {
  // security check: make sure that we're connecting to the correct server
  ULONG serverPid;
  if (GetNamedPipeServerProcessId(pipe, &serverPid)) {
    // FIXME: check the command line of the server?
    // See this:
    // http://www.codeproject.com/Articles/19685/Get-Process-Info-with-NtQueryInformationProcess
    // Too bad! Undocumented Windows internal API might be needed here. :-(
  }
  return true;
}

// establish a connection to the specified pipe and returns its handle
// static
HANDLE Client::connectPipe(const wchar_t *pipeName, int timeoutMs) {
  HANDLE pipe = INVALID_HANDLE_VALUE;
  if (WaitNamedPipe(pipeName, timeoutMs)) {
    pipe = CreateFile(pipeName, GENERIC_READ | GENERIC_WRITE, 0, NULL,
                      OPEN_EXISTING, 0, NULL);
  }

  if (pipe != INVALID_HANDLE_VALUE) {
    DWORD mode = PIPE_READMODE_MESSAGE;
    // The pipe is connected; change to message-read mode.
    if (!isPipeCreatedByMoqiServer(pipe) ||
        !::SetNamedPipeHandleState(pipe, &mode, NULL, NULL)) {
      DisconnectNamedPipe(pipe);
      CloseHandle(pipe);
      pipe = INVALID_HANDLE_VALUE;
    }
  }
  return pipe;
}

bool Client::ensureLauncherRunning() {
  if (launcherStartAttempted_) {
    return false;
  }
  launcherStartAttempted_ = true;

  auto module =
      static_cast<Moqi::ImeModule *>(textService_->imeModule().operator->());
  if (module == nullptr || module->programDir().empty()) {
    return false;
  }

  std::wstring launcherPath = module->programDir();
  launcherPath += L"\\MoqiLauncher.exe";
  const DWORD attrs = ::GetFileAttributesW(launcherPath.c_str());
  if (attrs == INVALID_FILE_ATTRIBUTES || (attrs & FILE_ATTRIBUTE_DIRECTORY)) {
    return false;
  }

  HINSTANCE result =
      ::ShellExecuteW(nullptr, L"open", launcherPath.c_str(), nullptr,
                      module->programDir().c_str(), SW_SHOWNORMAL);
  return reinterpret_cast<INT_PTR>(result) > 32;
}

// Ensure that we're connected to the Moqi input method server
// If we are already connected, the method simply returns true;
// otherwise, it tries to establish the connection.
bool Client::waitForRpcConnection() {
  if (pipe_ != INVALID_HANDLE_VALUE) {
    return true;
  }

  wstring serverPipeName = getPipeName(L"Launcher");
  pipe_ = connectPipe(serverPipeName.c_str(), 0);
  if (pipe_ == INVALID_HANDLE_VALUE) {
    ensureLauncherRunning();
  }

  for (int attempt = 0; pipe_ == INVALID_HANDLE_VALUE && attempt < 10;
       ++attempt) {
    // try to connect to the server
    pipe_ = connectPipe(serverPipeName.c_str(), 3000);
  }

  if (pipe_ != INVALID_HANDLE_VALUE) {
    // send initialization info to the server for hand-shake.
    shouldWaitConnection_ =
        false; // prevent recursive call of waitForRpcConnection
    if (!init()) {
      closeRpcConnection();
      shouldWaitConnection_ = true;
      return false;
    }

    if (isActivated_) {
      // we lost connection while being activated previously
      // re-initialize the whole text service.
      // activate the text service again.
      onActivate();
    }
    shouldWaitConnection_ = true;
  }
  // if init() or onActivate() RPC fails, the pipe_ might have been closed.
  return pipe_ != INVALID_HANDLE_VALUE;
}

void Client::resetTextServiceState() {
  // we lost connection while being activated previously
  // re-initialize the whole text service.

  // cleanup for the previous instance.
  // remove all buttons

  // some language bar buttons are not unregistered properly
  if (!buttons_.empty()) {
    for (auto &item : buttons_) {
      textService_->removeButton(item.second);
    }
    buttons_.clear();
  }
}

void Client::closeRpcConnection() {
  pendingAsyncResponses_.clear();
  if (asyncPollTimerId_ != 0) {
    ::KillTimer(asyncPollTimerWindow_, asyncPollTimerId_);
    asyncPollTimerId_ = 0;
    asyncPollTimerWindow_ = nullptr;
  }
  if (pipe_ != INVALID_HANDLE_VALUE) {
    DisconnectNamedPipe(pipe_);
    CloseHandle(pipe_);
    pipe_ = INVALID_HANDLE_VALUE;
  }
}

wstring Client::getPipeName(const wchar_t *base_name) {
  wstring pipeName = L"\\\\.\\pipe\\";
  DWORD len = 0;
  ::GetUserNameW(NULL, &len); // get the required size of the buffer
  if (len <= 0)
    return wstring();
  // add username to the pipe path so it won't clash with the other users' pipes
  unique_ptr<wchar_t[]> username(new wchar_t[len]);
  if (!::GetUserNameW(username.get(), &len))
    return wstring();
  pipeName += username.get();
  pipeName += L"\\MoqiIM\\";
  pipeName += base_name;
  return pipeName;
}

} // namespace Moqi
