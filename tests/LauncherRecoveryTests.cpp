// Focused, non-installing Windows regression tests. No TSF registration or tray.
#include <Windows.h>
#include <filesystem>
#include <iostream>
#include <fstream>
#include <stdexcept>
#include "BackendServer.h"
#include "PipeClient.h"
#include "PipeServer.h"
#include "Utils.h"

namespace fs = std::filesystem;

static void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

static void drainLoop() {
    // Close callbacks release pipes/process handles on the loop, not inline.
    uv_run(uv_default_loop(), UV_RUN_NOWAIT);
}

static moqi::protocol::ServerResponse exchangeRequest(HANDLE pipe,
        const moqi::protocol::ClientRequest& request) {
    std::string framed;
    require(Moqi::Proto::serializeMessage(request, framed), "serialize request");
    DWORD written = 0;
    require(::WriteFile(pipe, framed.data(), static_cast<DWORD>(framed.size()), &written, nullptr) &&
            written == framed.size(), "write test request");
    const ULONGLONG deadline = ::GetTickCount64() + 2000;
    DWORD available = 0;
    do {
        drainLoop();
        require(::PeekNamedPipe(pipe, nullptr, 0, nullptr, &available, nullptr) != FALSE, "peek response");
        if (available != 0) break;
        ::Sleep(1);
    } while (::GetTickCount64() < deadline);
    require(available != 0, "launcher must reply to configuration/start failures without timeout");
    std::string bytes(available, '\0');
    DWORD read = 0;
    require(::ReadFile(pipe, bytes.data(), available, &read, nullptr) != FALSE, "read response");
    Moqi::Proto::FrameBuffer buffer;
    buffer.append(bytes.data(), read);
    std::string payload;
    moqi::protocol::ServerResponse response;
    require(buffer.nextFrame(payload) && Moqi::Proto::parsePayload(payload, response), "parse response");
    require(response.seq_num() == request.seq_num(), "error reply must preserve sequence number");
    require(!response.success() && !response.error().empty(), "failure must have an explicit error reply");
    return response;
}

static Json::Value backendInfo(const fs::path& executable, const fs::path& cwd) {
    Json::Value info;
    info["name"] = "recovery-test";
    info["command"] = executable.u8string();
    info["workingDir"] = cwd.u8string();
    info["params"] = "--child";
    return info;
}

int main(int argc, char** argv) {
    if (argc > 1 && std::string(argv[1]) == "--child") {
        // A real process with stdin/stdout/stderr, without any engine dependency.
        ::Sleep(100);
        return 0;
    }
    const fs::path root = fs::temp_directory_path() /
        fs::path(L"Moqi recovery 空格 " + fs::path(Moqi::generateUuidStr()).wstring());
    try {
        fs::create_directories(root);
        {
            uv::Pipe unconnected;
            int writeStatus = 0;
            unconnected.setWriteCallback([&writeStatus](int status) { writeStatus = status; });
            unconnected.write(std::string("probe"));
            require(writeStatus < 0, "synchronous write failure must notify its owner");
            unconnected.close();
            drainLoop();
        }
        {
            fs::create_directories(root / L"data");
            // A syntactically valid but wrong-typed user config must not abort
            // construction before the launcher can show a recovery notification.
            { std::ofstream config(root / L"data" / L"MoqiLauncher.json");
              config << "{\"logLevel\":123}"; }
            Moqi::PipeServer server((root / L"data").wstring());
            const auto child = root / L"算法测试.exe";
            Moqi::BackendServer backend(&server, backendInfo(child, root));
            require(!backend.startProcess(), "missing executable must fail");
            require(!backend.isProcessRunning(), "failed spawn must not count as running");
            drainLoop();

            // Exercise real framed IPC: both a missing GUID mapping and a
            // failed spawn must reply immediately, not hang or dereference null.
            struct Connection {
                Moqi::PipeServer* server;
                Moqi::PipeClient* client = nullptr;
            } connection{&server};
            uv_pipe_t listener{};
            require(uv_pipe_init_windows_named_pipe(uv_default_loop(), &listener, 0,
                PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE, nullptr) == 0, "init test listener");
            listener.data = &connection;
            const std::string name = "\\\\.\\pipe\\MoqiRecovery-" + Moqi::generateUuidStr();
            require(uv_pipe_bind(&listener, name.c_str()) == 0, "bind test listener");
            require(uv_listen(reinterpret_cast<uv_stream_t*>(&listener), 4,
                [](uv_stream_t* stream, int status) {
                    auto* context = static_cast<Connection*>(stream->data);
                    if (status < 0) return;
                    context->client = new Moqi::PipeClient(context->server,
                        PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE, nullptr);
                    uv_accept(stream, context->client->stream());
                    context->client->startRead();
                }) == 0, "listen for test client");
            HANDLE pipe = ::CreateFileA(name.c_str(), GENERIC_READ | GENERIC_WRITE,
                0, nullptr, OPEN_EXISTING, 0, nullptr);
            require(pipe != INVALID_HANDLE_VALUE, "connect test client");
            const ULONGLONG acceptDeadline = ::GetTickCount64() + 2000;
            while (!connection.client && ::GetTickCount64() < acceptDeadline) {
                drainLoop();
                ::Sleep(1);
            }
            require(connection.client != nullptr, "accept test client");
            moqi::protocol::ClientRequest init;
            init.set_method(moqi::protocol::METHOD_INIT);
            init.set_guid("{00000000-0000-0000-0000-000000000000}");
            init.set_seq_num(42);
            exchangeRequest(pipe, init);
            connection.client->backend_ = &backend;
            init.set_seq_num(43);
            exchangeRequest(pipe, init);
            ::CloseHandle(pipe);
            connection.client->close();
            uv_close(reinterpret_cast<uv_handle_t*>(&listener), nullptr);
            drainLoop();
            drainLoop();

            moqi::protocol::ClientRequest close;
            close.set_method(moqi::protocol::METHOD_CLOSE);
            backend.handleClientMessage(nullptr, close);
            require(!backend.isProcessRunning(), "disconnect must not start an engine");

            // Repair the executable during the retry cooldown. Explicit restart
            // must bypass the cooldown and start a previously stopped backend.
            const fs::path self = fs::path(server.programDir()) / L"LauncherRecoveryTests.exe";
            fs::copy_file(self, child);
            require(!backend.startProcess(), "failed spawns must be throttled");
            backend.restartProcess();
            require(backend.isProcessRunning(), "restart must start stopped engine");
            const ULONGLONG deadline = ::GetTickCount64() + 5000;
            while (backend.isProcessRunning() && ::GetTickCount64() < deadline) {
                drainLoop();
                ::Sleep(5);
            }
            require(!backend.isProcessRunning(), "early child exit must be reaped without restart storm");
            drainLoop();

            require(backend.startProcess(true), "explicit retry must recover after early exit");
            backend.terminateProcess();
            drainLoop();
            require(!backend.isProcessRunning(), "terminate must clear running state");

            Moqi::BackendServer missingCwd(&server, backendInfo(child, root / L"missing-cwd"));
            require(!missingCwd.startProcess(), "missing working directory must fail safely");
            drainLoop();

            // Invalid JSON must be a recoverable configuration error, not an exception.
            const auto malformed = root / L"broken.json";
            { std::ofstream out(malformed); out << "{broken"; }
            Json::Value parsed;
            require(!Moqi::loadJsonFile(malformed.wstring(), parsed), "invalid JSON must fail cleanly");
            drainLoop();
        }
        require(uv_run(uv_default_loop(), UV_RUN_NOWAIT) == 0, "all libuv handles must be released");
        fs::remove_all(root);
        std::cout << "PASS: synchronous write failure, wrong-typed config, spawn failure, error response, cooldown, stopped restart, early exit, unicode path, missing cwd, malformed JSON\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        std::error_code ignored;
        fs::remove_all(root, ignored);
        return 1;
    }
}
