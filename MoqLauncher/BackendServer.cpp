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
#include <ShlObj.h>
#include <Shellapi.h>
#include <Lmcons.h> // for UNLEN
#include <Wincrypt.h>  // for CryptBinaryToString (used for base64 encoding)
#include <cstring>
#include <cassert>
#include <chrono>  // C++ 11 clock functions
#include <string>
#include <vector>
#include <map>
#include <fstream>
#include <algorithm>
#include <codecvt>  // for utf8 conversion
#include <locale>  // for wstring_convert
#include <sstream>

#include <json/json.h>

#include "BackendServer.h"
#include "PipeServer.h"
#include "PipeClient.h"

using namespace std;

namespace MoqiIME {

static wstring_convert<codecvt_utf8<wchar_t>> utf8Codec;
static constexpr auto MAX_RESPONSE_WAITING_TIME = 30;  // if a backend is non-responsive for 30 seconds, it's considered dead
static constexpr size_t MAX_LOG_PREVIEW_LEN = 256;

static std::string previewForLog(const char* data, size_t len) {
    std::string text(data, len);
    for (char& ch : text) {
        if (ch == '\r' || ch == '\n') {
            ch = ' ';
        }
    }
    if (text.size() > MAX_LOG_PREVIEW_LEN) {
        text.resize(MAX_LOG_PREVIEW_LEN);
        text += "...";
    }
    return text;
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

BackendServer::BackendServer(PipeServer* pipeServer, const Json::Value& info) :
	pipeServer_{pipeServer},
	process_{ nullptr },
	stdinPipe_{nullptr},
	stdoutPipe_{nullptr},
	stderrPipe_{nullptr},
	name_(info["name"].asString()),
	command_(info["command"].asString()),
	workingDir_(info["workingDir"].asString()),
	params_(info["params"].asString()) {
}

BackendServer::~BackendServer() {
	terminateProcess();
}

std::shared_ptr<spdlog::logger>& BackendServer::logger() {
	return pipeServer_->logger();
}

void BackendServer::handleClientMessage(PipeClient * client, const char * readBuf, size_t len) {
	if (!isProcessRunning()) {
        logger()->warn("BackendServer::handleClientMessage process not running name={} clientId={}",
                       name_, client ? client->clientId_ : std::string("<null>"));
		startProcess();
	}

	// message format: <client_id>|<json string>\n
	string msg = string{ client->clientId_ };
	msg += "|";
	msg.append(readBuf, len);
	msg += "\n";

	logger()->warn("BackendServer::handleClientMessage name={} clientId={} len={} preview={}",
                   name_,
                   client ? client->clientId_ : std::string("<null>"),
                   len,
                   previewForLog(readBuf, len));

	// write the message to the backend server
    if (stdinPipe_ == nullptr) {
        logger()->error("BackendServer::handleClientMessage stdinPipe is null name={} clientId={}",
                        name_, client ? client->clientId_ : std::string("<null>"));
        return;
    }
    stdinPipe_->write(std::move(msg));
    logger()->warn("BackendServer::handleClientMessage wrote-to-stdin name={} clientId={}",
                   name_, client ? client->clientId_ : std::string("<null>"));
}

uv::Pipe* BackendServer::createStdinPipe() {
    auto stdinPipe = new uv::Pipe();
    stdinPipe->setCloseCallback([stdinPipe]() {delete stdinPipe; });
    return stdinPipe;
}

uv::Pipe* BackendServer::createStdoutPipe() {
    auto stdoutPipe = new uv::Pipe();
    stdoutPipe->setReadCallback(
        [this](const char* buf, size_t len) {
            onStdoutRead(buf, len);
        }
    );
    stdoutPipe->setReadErrorCallback(
        [this](int error) {
            onReadError(error);
        }
    );
    stdoutPipe->setCloseCallback([stdoutPipe]() {delete stdoutPipe; });
    return stdoutPipe;
}

uv::Pipe* BackendServer::createStderrPipe() {
    auto stderrPipe = new uv::Pipe();
    stderrPipe->setReadCallback(
        [this](const char* buf, size_t len) {
            onStderrRead(buf, len);
        }
    );
    stderrPipe->setReadErrorCallback(
        [this](int error) {
            onReadError(error);
        }
    );
    stderrPipe->setCloseCallback([this, stderrPipe]() {delete stderrPipe; });
    return stderrPipe;
}

void BackendServer::startProcess() {
	logger()->warn("BackendServer::startProcess name={} command={} workingDir={}", name_, command_, workingDir_);
	process_ = new uv_process_t{};
	// create pipes for stdio of the child process
    stdoutPipe_ = createStdoutPipe();
    stdoutReadBuf_.clear();
    stdinPipe_ = createStdinPipe();
    stderrPipe_ = createStderrPipe();

    constexpr auto pipeFlags = uv_stdio_flags(UV_CREATE_PIPE | UV_READABLE_PIPE | UV_WRITABLE_PIPE);
	uv_stdio_container_t stdio_containers[3];
    stdio_containers[0].data.stream = stdinPipe_->streamHandle();
	stdio_containers[0].flags = pipeFlags;
    stdio_containers[1].data.stream = stdoutPipe_->streamHandle();
	stdio_containers[1].flags = pipeFlags;
    stdio_containers[2].data.stream = stderrPipe_->streamHandle();
	stdio_containers[2].flags = pipeFlags;

    auto utf8CurrentDirPath = getUtf8CurrentDir();
    auto executablePath = utf8CurrentDirPath + '\\' + command_;
	const char* argv[] = {
        executablePath.c_str(),
		params_.c_str(),
		nullptr
	};
	uv_process_options_t options = { 0 };
	options.flags = UV_PROCESS_WINDOWS_HIDE; //  UV_PROCESS_WINDOWS_VERBATIM_ARGUMENTS;
    options.file = executablePath.c_str();
	options.args = const_cast<char**>(argv);

    auto backendWorkingDirPath = utf8CurrentDirPath + '\\' + workingDir_;
	options.cwd = backendWorkingDirPath.c_str();

	// build our own new environments
    auto utf8EnvVars = getUtf8EnvironmentVariables();
	// add our own environment variables
	// NOTE: Force python to output UTF-8 encoded strings
	// Reference: https://docs.python.org/3/using/cmdline.html#envvar-PYTHONIOENCODING
	// By default, python uses ANSI encoding in Windows and this breaks our unicode support.
	// FIXME: makes this configurable from backend.json.
	utf8EnvVars.emplace_back("PYTHONIOENCODING=utf-8:ignore");

    // convert to a null terminated char* array.
	std::vector<const char*> env;
	for (auto& v : utf8EnvVars) {
		env.emplace_back(v.c_str());
	}
	env.emplace_back(nullptr);
	options.env = const_cast<char**>(env.data());

	options.stdio_count = 3;
	options.stdio = stdio_containers;
	int ret = uv_spawn(uv_default_loop(), process_, &options);
	if (ret < 0) {
        logger()->error("uv_spawn failed name={} ret={} err={}", name_, ret, uv_strerror(ret));
		delete process_;
		process_ = nullptr;
		closeStdioPipes();
		return;
	}
    logger()->warn("uv_spawn success name={} pid={}", name_, process_->pid);

	// start receiving data from the backend server
    stdoutPipe_->startRead();
    stderrPipe_->startRead();
}

void BackendServer::restartProcess() {
	terminateProcess();
    startProcess();
}

void BackendServer::terminateProcess() {
	if (process_) {
		closeStdioPipes();

		uv_process_kill(process_, SIGTERM);
        uv_close(reinterpret_cast<uv_handle_t*>(process_),
            [](uv_handle_t* handle) {
                delete reinterpret_cast<uv_process_t*>(handle);
            }
        );

        process_ = nullptr;
	}
    pipeServer_->onBackendClosed(this);
}

// check if the backend server process is running
bool BackendServer::isProcessRunning() {
	return process_ != nullptr;
}

void BackendServer::onStdoutRead(const char* buf, size_t len) {
    logger()->warn("BackendServer::onStdoutRead name={} len={} preview={}",
                   name_, len, previewForLog(buf, len));
    stdoutReadBuf_.write(buf, len);
    handleBackendReply();
}

void BackendServer::onReadError(int status) {
    logger()->error("BackendServer::onReadError name={} status={} err={}",
                    name_, status, uv_strerror(status));
    // the backend server is broken, restart it.
    restartProcess();
}

void BackendServer::onStderrRead(const char* buf, size_t len) {
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
        stdoutReadBuf_ = std::stringstream();
    }

	if (stderrPipe_ != nullptr) {
        stderrPipe_->close();
		stderrPipe_ = nullptr;
	}
}

void BackendServer::handleBackendReply() {
	// each output message should be a full line ends with \n or \r\n, so we need to do buffering and
	// handle the messages line by line.
    std::string line;
	for (;;) {
        std::getline(stdoutReadBuf_, line);
        if (stdoutReadBuf_.eof()) {
            // getline() reached end of buffer before finding a '\n'. The current line is incomplete.
            // Put remaining data back to the buffer and wait for the next \n so it becomes a full line.
            if (!line.empty()) {
                logger()->warn("BackendServer::handleBackendReply pending-partial-line name={} preview={}",
                               name_, previewForLog(line.data(), line.size()));
            }
            stdoutReadBuf_.clear();
            stdoutReadBuf_.str(line);
            break;
        }

        logger()->warn("BackendServer::handleBackendReply line name={} preview={}",
                       name_, previewForLog(line.data(), line.size()));

		// only handle lines prefixed with "MOQI_MSG|" since other lines
		// might be debug messages printed by the backend.
		// Format of each message: "MOQI_MSG|<client_id>|<reply JSON string>\n"
        constexpr char moqiMsgPrefix[] = "MOQI_MSG|";
        constexpr size_t moqiMsgPrefixLen = 9;
		if (line.compare(0, moqiMsgPrefixLen, moqiMsgPrefix) == 0) {
            // because Windows uses CRLF "\r\n" for new lines, python and node.js
            // try to convert "\n" to "\r\n" sometimes. Let's remove the additional '\r'
            if (line.back() == '\r') {
                line.pop_back();
            }

            auto sep = line.find('|', moqiMsgPrefixLen);  // Find the next "|".
			if (sep != line.npos) {
				// split the client_id from the remaining json reply
				string clientId = line.substr(moqiMsgPrefixLen, sep - moqiMsgPrefixLen);
                // send the reply message back to the client
                auto msgStart = sep + 1;
                auto msg = line.c_str() + msgStart;
				auto msgLen = line.length() - msgStart;
				if (auto client = pipeServer_->clientFromId(clientId)) {
                    logger()->warn("BackendServer::handleBackendReply deliver name={} clientId={} msgLen={} preview={}",
                                   name_, clientId, msgLen, previewForLog(msg, msgLen));
					client->writePipe(msg, msgLen);
				} else {
                    logger()->error("BackendServer::handleBackendReply client not found name={} clientId={} msgLen={}",
                                    name_, clientId, msgLen);
				}
			} else {
                logger()->error("BackendServer::handleBackendReply missing client separator name={} line={}",
                                name_, previewForLog(line.data(), line.size()));
			}
		} else {
            logger()->warn("BackendServer::handleBackendReply ignored-nonmoqi-line name={} line={}",
                           name_, previewForLog(line.data(), line.size()));
        }
	}
}

} // namespace MoqiIME
