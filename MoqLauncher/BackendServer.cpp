//
//	Copyright (C) 2015 - 2016 Hong Jen Yee (PCMan) <pcman.tw@gmail.com>
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

#include <Windows.h>
#include <Lmcons.h> // for UNLEN
#include <Shellapi.h>
#include <ShlObj.h>
#include <Wincrypt.h> // for CryptBinaryToString (used for base64 encoding)
#include <algorithm>
#include <cassert>
#include <chrono>  // C++ 11 clock functions
#include <codecvt> // for utf8 conversion
#include <cstring>
#include <fstream>
#include <locale> // for wstring_convert
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include <json/json.h>

#include "BackendServer.h"
#include "PipeClient.h"
#include "PipeServer.h"
#include "../proto/ProtoFraming.h"
#include "proto/moqi.pb.h"

using namespace std;

namespace Moqi {

static wstring_convert<codecvt_utf8<wchar_t>> utf8Codec;
static constexpr auto MAX_RESPONSE_WAITING_TIME =
    30; // if a backend is non-responsive for 30 seconds, it's considered dead
static constexpr uint32_t RIME_DEPLOY_COMMAND_ID = 10;
// Minimum lifetime a backend must have before a pipe read error triggers an
// automatic restart. If the process dies sooner (startup crash, session being
// torn down at logon/logoff), restarting it immediately only spawns a hot loop
// (the launcher was observed respawning ~100x per second while logging the
// warmup). In that case the backend is left stopped and the next client
// message respawns it lazily.
static constexpr ULONGLONG MIN_BACKEND_RESTART_INTERVAL_MS = 2000;

static DWORD trayNotificationInfoFlags(moqi::protocol::TrayNotificationIcon icon) {
  switch (icon) {
  case moqi::protocol::TRAY_NOTIFICATION_ICON_WARNING:
    return NIIF_WARNING;
  case moqi::protocol::TRAY_NOTIFICATION_ICON_ERROR:
    return NIIF_ERROR;
  case moqi::protocol::TRAY_NOTIFICATION_ICON_INFO:
  case moqi::protocol::TRAY_NOTIFICATION_ICON_UNSPECIFIED:
  default:
    return NIIF_INFO;
  }
}

static std::string getUtf8CurrentDir() {
  char dirPath[MAX_PATH];
  size_t len = MAX_PATH;
  uv_cwd(dirPath, &len);
  return dirPath;
}

static std::vector<std::string> getUtf8EnvironmentVariables() {
  // build our own new environments
  auto env_strs = GetEnvironmentStringsW();
  vector<string> utf8Environ;
  for (auto penv = env_strs; *penv; penv += wcslen(penv) + 1) {
    utf8Environ.emplace_back(utf8Codec.to_bytes(penv));
  }
  FreeEnvironmentStringsW(env_strs);
  return utf8Environ;
}

BackendServer::BackendServer(PipeServer *pipeServer, const Json::Value &info)
    : pipeServer_{pipeServer}, process_{nullptr}, stdinPipe_{nullptr},
      stdoutPipe_{nullptr}, stderrPipe_{nullptr},
      name_(info["name"].asString()), command_(info["command"].asString()),
      workingDir_(info["workingDir"].asString()),
      params_(info["params"].asString()) {}

BackendServer::~BackendServer() { terminateProcess(); }

std::shared_ptr<spdlog::logger> &BackendServer::logger() {
  return pipeServer_->logger();
}

void BackendServer::handleClientMessage(PipeClient *client,
                                        const moqi::protocol::ClientRequest &request) {
  if (!isProcessRunning()) {
    startProcess();
  }

  if (request.method() == moqi::protocol::METHOD_INIT &&
      !request.guid().empty()) {
    // Remember the language profile GUID so we can warm up the backend.
    lastInitGuid_ = request.guid();
  }

  if (name_ == "moqi-ime" &&
      request.method() == moqi::protocol::METHOD_ON_COMMAND &&
      request.command_id() == RIME_DEPLOY_COMMAND_ID) {
    pipeServer_->enqueueTrayNotification(L"Rime", L"重新部署中...", NIIF_INFO);
  }

  moqi::protocol::ClientRequest backendRequest = request;
  backendRequest.set_client_id(client->clientId_);
  std::string framedMessage;
  if (!Proto::serializeMessage(backendRequest, framedMessage)) {
    logger()->error("Failed to serialize backend request for client {}",
                    client->clientId_);
    return;
  }

  // write the message to the backend server
  stdinPipe_->write(std::move(framedMessage));
}

void BackendServer::uploadCloudClipboardText(const std::string& utf8Text) {
  if (utf8Text.empty()) {
    return;
  }
  if (!isProcessRunning()) {
    startProcess();
  }
  if (stdinPipe_ == nullptr) {
    logger()->warn("Skip cloud clipboard upload: backend stdin is unavailable");
    return;
  }

  moqi::protocol::ClientRequest request;
  request.set_method(moqi::protocol::METHOD_CLOUD_CLIPBOARD_UPLOAD);
  request.set_client_id("clipboard");
  request.set_cloud_clipboard_text(utf8Text);
  std::string framedMessage;
  if (!Proto::serializeMessage(request, framedMessage)) {
    logger()->error("Failed to serialize cloud clipboard upload");
    return;
  }
  stdinPipe_->write(std::move(framedMessage));
}

uv::Pipe *BackendServer::createStdinPipe() {
  auto stdinPipe = new uv::Pipe();
  stdinPipe->setCloseCallback([stdinPipe]() { delete stdinPipe; });
  return stdinPipe;
}

uv::Pipe *BackendServer::createStdoutPipe() {
  auto stdoutPipe = new uv::Pipe();
  const unsigned int generation = pipeGeneration_;
  stdoutPipe->setReadCallback(
      [this](const char *buf, size_t len) { onStdoutRead(buf, len); });
  stdoutPipe->setReadErrorCallback(
      [this, generation](int error) { onReadError(error, generation); });
  stdoutPipe->setCloseCallback([stdoutPipe]() { delete stdoutPipe; });
  return stdoutPipe;
}

uv::Pipe *BackendServer::createStderrPipe() {
  auto stderrPipe = new uv::Pipe();
  const unsigned int generation = pipeGeneration_;
  stderrPipe->setReadCallback(
      [this](const char *buf, size_t len) { onStderrRead(buf, len); });
  stderrPipe->setReadErrorCallback(
      [this, generation](int error) { onReadError(error, generation); });
  stderrPipe->setCloseCallback([this, stderrPipe]() { delete stderrPipe; });
  return stderrPipe;
}

void BackendServer::startProcess() {
  process_ = new uv_process_t{};
  // Bump the generation before creating the pipes so their read-error
  // callbacks are tagged with the new generation (stale errors from a previous
  // process's pipes will then be ignored).
  ++pipeGeneration_;
  // create pipes for stdio of the child process
  stdoutPipe_ = createStdoutPipe();
  stdoutFrameBuf_.clear();
  stdinPipe_ = createStdinPipe();
  stderrPipe_ = createStderrPipe();

  constexpr auto pipeFlags =
      uv_stdio_flags(UV_CREATE_PIPE | UV_READABLE_PIPE | UV_WRITABLE_PIPE);
  uv_stdio_container_t stdio_containers[3];
  stdio_containers[0].data.stream = stdinPipe_->streamHandle();
  stdio_containers[0].flags = pipeFlags;
  stdio_containers[1].data.stream = stdoutPipe_->streamHandle();
  stdio_containers[1].flags = pipeFlags;
  stdio_containers[2].data.stream = stderrPipe_->streamHandle();
  stdio_containers[2].flags = pipeFlags;

  auto utf8CurrentDirPath = getUtf8CurrentDir();
  auto executablePath = utf8CurrentDirPath + '\\' + command_;
  const char *argv[] = {executablePath.c_str(), params_.c_str(), nullptr};
  uv_process_options_t options = {0};
  options.flags =
      UV_PROCESS_WINDOWS_HIDE; //  UV_PROCESS_WINDOWS_VERBATIM_ARGUMENTS;
  options.file = executablePath.c_str();
  options.args = const_cast<char **>(argv);

  auto backendWorkingDirPath = utf8CurrentDirPath + '\\' + workingDir_;
  options.cwd = backendWorkingDirPath.c_str();

  // build our own new environments
  auto utf8EnvVars = getUtf8EnvironmentVariables();
  // add our own environment variables
  // NOTE: Force python to output UTF-8 encoded strings
  // Reference:
  // https://docs.python.org/3/using/cmdline.html#envvar-PYTHONIOENCODING By
  // default, python uses ANSI encoding in Windows and this breaks our unicode
  // support.
  // FIXME: makes this configurable from backend.json.
  utf8EnvVars.emplace_back("PYTHONIOENCODING=utf-8:ignore");

  // convert to a null terminated char* array.
  std::vector<const char *> env;
  for (auto &v : utf8EnvVars) {
    env.emplace_back(v.c_str());
  }
  env.emplace_back(nullptr);
  options.env = const_cast<char **>(env.data());

  options.stdio_count = 3;
  options.stdio = stdio_containers;
  int ret = uv_spawn(uv_default_loop(), process_, &options);
  if (ret < 0) {
    delete process_;
    process_ = nullptr;
    closeStdioPipes();
    return;
  }
  lastProcessStartTick_ = ::GetTickCount64();

  // start receiving data from the backend server
  stdoutPipe_->startRead();
  stderrPipe_->startRead();

  // Warm the backend up right away: the first request of a fresh backend
  // (Rime engine creation, dictionary load, deferred UI init) can take many
  // seconds when the OS cache is cold after a reboot. Doing it here, in the
  // background at logon, means the user's first keystroke after boot is fast
  // instead of blocking for several seconds (and instead of the client
  // timing out and reconnecting, which made it worse).
  sendWarmupRequests();
}

// Send synthetic init/onActivate/filterKeyDown requests to a freshly spawned
// backend so its expensive first-request work happens in the background. The
// backend treats "warmup" like any other client; its responses are dropped by
// the launcher (no PipeClient with that id exists).
void BackendServer::sendWarmupRequests() {
  if (stdinPipe_ == nullptr || lastInitGuid_.empty()) {
    return;
  }
  logger()->info("Warming up backend {} (guid={})", name_, lastInitGuid_);

  moqi::protocol::ClientRequest request;
  request.set_client_id("warmup");

  request.set_method(moqi::protocol::METHOD_INIT);
  request.set_guid(lastInitGuid_);
  request.set_is_windows8_above(true);
  request.set_is_metro_app(false);
  request.set_is_ui_less(true);
  request.set_is_console(false);
  std::string framedMessage;
  if (!Proto::serializeMessage(request, framedMessage)) {
    logger()->error("Failed to serialize warmup init request");
    return;
  }
  stdinPipe_->write(std::move(framedMessage));

  request.set_method(moqi::protocol::METHOD_ON_ACTIVATE);
  request.set_is_keyboard_open(true);
  framedMessage.clear();
  if (!Proto::serializeMessage(request, framedMessage)) {
    logger()->error("Failed to serialize warmup activate request");
    return;
  }
  stdinPipe_->write(std::move(framedMessage));

  // One harmless key press to trigger lazy dictionary loading / deferred UI.
  request.set_method(moqi::protocol::METHOD_FILTER_KEY_DOWN);
  auto *keyEvent = request.mutable_key_event();
  keyEvent->set_char_code('a');
  keyEvent->set_key_code(0x41);
  keyEvent->set_scan_code(0);
  keyEvent->set_repeat_count(1);
  keyEvent->set_is_extended(false);
  for (int i = 0; i < 256; ++i) {
    keyEvent->add_key_states(0);
  }
  framedMessage.clear();
  if (!Proto::serializeMessage(request, framedMessage)) {
    logger()->error("Failed to serialize warmup key request");
    return;
  }
  stdinPipe_->write(std::move(framedMessage));
}

void BackendServer::restartProcess() {
  if (process_ == nullptr) {
    return;
  }
  terminateProcess();
  startProcess();
}

void BackendServer::terminateProcess() {
  if (process_) {
    closeStdioPipes();

    uv_process_kill(process_, SIGTERM);
    uv_close(reinterpret_cast<uv_handle_t *>(process_),
             [](uv_handle_t *handle) {
               delete reinterpret_cast<uv_process_t *>(handle);
             });

    process_ = nullptr;
  }
  pipeServer_->onBackendClosed(this);
}

// check if the backend server process is running
bool BackendServer::isProcessRunning() { return process_ != nullptr; }

void BackendServer::onStdoutRead(const char *buf, size_t len) {
  stdoutFrameBuf_.append(buf, len);
  handleBackendReply();
}

void BackendServer::onReadError(int status, unsigned int generation) {
  // Ignore read errors from a previous process's pipes. When a backend dies,
  // both its stdout and stderr pipes report an error; without this guard the
  // second callback would kill/restart the freshly spawned replacement.
  if (generation != pipeGeneration_ || process_ == nullptr) {
    return;
  }

  // The backend exited or its pipes broke. Restart it, but throttled: if it
  // died almost immediately after being spawned (e.g. a startup crash, or the
  // session being torn down at logon/logoff), respawning right away only
  // creates a restart storm (the launcher was observed respawning ~100x per
  // second while logging the warmup). In that case just clean up and let the
  // next client message respawn the backend lazily.
  const ULONGLONG now = ::GetTickCount64();
  if (now - lastProcessStartTick_ < MIN_BACKEND_RESTART_INTERVAL_MS) {
    terminateProcess();
    return;
  }
  restartProcess();
}

void BackendServer::onStderrRead(const char *buf, size_t len) {
  // FIXME: need to do output buffering since we might not receive a full line
  // log the error message
  logger()->error("[Backend error] {}", std::string(buf, len));
}

void BackendServer::closeStdioPipes() {
  if (stdinPipe_ != nullptr) {
    stdinPipe_->close();
    stdinPipe_ = nullptr;
  }

  if (stdoutPipe_ != nullptr) {
    stdoutPipe_->close();
    stdoutPipe_ = nullptr;
    stdoutFrameBuf_.clear();
  }

  if (stderrPipe_ != nullptr) {
    stderrPipe_->close();
    stderrPipe_ = nullptr;
  }
}

void BackendServer::handleBackendReply() {
  std::string payload;
  while (stdoutFrameBuf_.nextFrame(payload)) {
    moqi::protocol::ServerResponse response;
    if (!Proto::parsePayload(payload, response)) {
      logger()->error("Failed to parse protobuf response from backend {}", name_);
      continue;
    }

    if (response.has_tray_notification()) {
      const auto &notification = response.tray_notification();
      pipeServer_->enqueueTrayNotification(
          utf8Codec.from_bytes(notification.title()),
          utf8Codec.from_bytes(notification.message()),
          trayNotificationInfoFlags(notification.icon()));
    }

    if (response.client_id().empty()) {
      logger()->warn("Ignoring backend response without client_id from {}", name_);
      continue;
    }

    if (response.client_id() == "clipboard") {
      continue;
    }

    if (auto client = pipeServer_->clientFromId(response.client_id())) {
      const auto framedPayload = Proto::framePayload(payload);
      client->writePipe(framedPayload.data(), framedPayload.size());
    }
  }
}

} // namespace Moqi
