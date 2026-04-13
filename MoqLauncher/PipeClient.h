//
//	Copyright (C) 2015 - 2018 Hong Jen Yee (PCMan) <pcman.tw@gmail.com>
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

#ifndef _Moqi_PIPE_CLIENT_H_
#define _Moqi_PIPE_CLIENT_H_

#include <Windows.h>
#include <memory>
#include <cstdint>
#include "BackendServer.h"
#include "../proto/moqi.pb.h"
#include "../proto/ProtoFraming.h"

#include <spdlog/spdlog.h>

#include <UvPipe.h>


namespace Moqi {

class PipeServer;
class BackendServer;

class PipeClient {
public:
	BackendServer* backend_;
	std::string textServiceGuid_;
	std::string clientId_;

	PipeClient(PipeServer* server, DWORD pipeMode, SECURITY_ATTRIBUTES* securityAttributes);

    ~PipeClient();

	uv_stream_t* stream() {
        return pipe_.streamHandle();
	}

	std::shared_ptr<spdlog::logger>& logger();

    void startRead() {
        pipe_.startRead();
    }

    void writePipe(const char* data, size_t len) {
        // we got a response before request timeout, so stop the timer
        stopRequestTimeoutTimer();
        pipe_.write(data, len);
    }

	bool initBackend(const moqi::protocol::ClientRequest& params);

	void disconnectFromBackend();

	void close();

private:
    void onReadError(int error);

    void handleClientMessage(const char* readBuf, size_t len);

    void startRequestTimeoutTimer(std::uint64_t timeoutMs);

    void stopRequestTimeoutTimer();

    void onRequestTimeout();

private:
    uv::Pipe pipe_;
	PipeServer* server_;

	// timer used to wait for response from backend server
	std::unique_ptr<uv_timer_t> waitResponseTimer_;
    Proto::FrameBuffer readBuffer_;
};

} // namespace Moqi

#endif // _Moqi_PIPE_CLIENT_H_
