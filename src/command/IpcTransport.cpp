#include "IpcTransport.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <thread>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#endif

namespace MipsyncEngine::Command {
namespace fs = std::filesystem;
namespace {

std::string NormalizePath(const std::string& input) {
    if (input.empty()) return {};
    std::error_code ec;
    fs::path path = fs::weakly_canonical(fs::path(input), ec);
    if (ec) path = fs::absolute(fs::path(input), ec);
    std::string value = ec ? input : path.string();
#ifdef _WIN32
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
#endif
    return value;
}

fs::path RegistryFile(const std::string& instanceId) {
    return fs::path(InstanceRegistry::RegistryDirectory()) / (instanceId + ".json");
}

CommandResult ParseResponse(const std::string& payload, const std::string& requestId) {
    try {
        const Json value = Json::parse(payload);
        CommandResult result;
        result.success = value.value("success", false);
        result.status = value.value("status", result.success ? "completed" : "failed");
        if (value.contains("result")) result.value = value["result"];
        if (value.contains("diagnostics") && value["diagnostics"].is_array()) {
            for (const auto& item : value["diagnostics"]) {
                Diagnostic diagnostic;
                diagnostic.code = item.value("code", "MIPSYNC_IPC_ERROR");
                diagnostic.severity = item.value("severity", "error");
                diagnostic.message = item.value("message", "Unknown IPC error");
                if (item.contains("location")) diagnostic.location = item["location"];
                diagnostic.symbol = item.value("symbol", std::string{});
                if (item.contains("expected")) diagnostic.expected = item["expected"];
                if (item.contains("received")) diagnostic.received = item["received"];
                if (item.contains("suggestions")) diagnostic.suggestions = item["suggestions"];
                result.diagnostics.push_back(std::move(diagnostic));
            }
        }
        return result;
    } catch (const std::exception& ex) {
        return CommandResult::Fail("MIPSYNC_IPC_RESPONSE", "Invalid Editor response for " + requestId + ": " + ex.what());
    }
}

} // namespace

Json EditorInstanceInfo::ToJson() const {
    return {
        {"instanceId", instanceId}, {"processId", processId}, {"projectPath", projectPath},
        {"projectId", projectId}, {"engineVersion", engineVersion},
        {"executablePath", executablePath}, {"endpoint", endpoint},
        {"protocolVersion", protocolVersion},
    };
}

EditorInstanceInfo EditorInstanceInfo::FromJson(const Json& value) {
    EditorInstanceInfo instance;
    instance.instanceId = value.value("instanceId", std::string{});
    instance.processId = value.value("processId", uint64_t{});
    instance.projectPath = value.value("projectPath", std::string{});
    instance.projectId = value.value("projectId", std::string{});
    instance.engineVersion = value.value("engineVersion", std::string{});
    instance.executablePath = value.value("executablePath", std::string{});
    instance.endpoint = value.value("endpoint", std::string{});
    instance.protocolVersion = value.value("protocolVersion", 0);
    return instance;
}

std::string InstanceRegistry::RegistryDirectory() {
    return (fs::temp_directory_path() / "mipsync" / "instances").string();
}

bool InstanceRegistry::Register(const EditorInstanceInfo& instance, std::string& outError) {
    try {
        fs::create_directories(RegistryDirectory());
        const fs::path target = RegistryFile(instance.instanceId);
        const fs::path temporary = target.string() + ".tmp";
        std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
        if (!out.is_open()) { outError = "failed to write instance registry"; return false; }
        out << instance.ToJson().dump(2);
        out.close();
        std::error_code ec;
        fs::remove(target, ec);
        fs::rename(temporary, target, ec);
        if (ec) { outError = ec.message(); return false; }
        return true;
    } catch (const std::exception& ex) {
        outError = ex.what();
        return false;
    }
}

void InstanceRegistry::Unregister(const std::string& instanceId) {
    std::error_code ec;
    fs::remove(RegistryFile(instanceId), ec);
}

std::vector<EditorInstanceInfo> InstanceRegistry::List() {
    std::vector<EditorInstanceInfo> result;
    std::error_code ec;
    fs::create_directories(RegistryDirectory(), ec);
    for (const auto& entry : fs::directory_iterator(RegistryDirectory(), ec)) {
        if (ec || !entry.is_regular_file(ec) || entry.path().extension() != ".json") continue;
        try {
            std::ifstream input(entry.path(), std::ios::binary);
            Json json;
            input >> json;
            auto instance = EditorInstanceInfo::FromJson(json);
            // The registry is removed during normal Editor shutdown. Do not
            // reject an entry solely because process enumeration is isolated
            // by a sandbox or elevation boundary; the IPC connection remains
            // the authoritative liveness check.
            if (instance.instanceId.empty() || instance.endpoint.empty()) {
                fs::remove(entry.path(), ec);
                continue;
            }
            result.push_back(std::move(instance));
        } catch (...) {
            fs::remove(entry.path(), ec);
        }
    }
    std::sort(result.begin(), result.end(), [](const auto& a, const auto& b) {
        return a.instanceId < b.instanceId;
    });
    return result;
}

std::vector<EditorInstanceInfo> InstanceRegistry::FindForProject(const std::string& projectPath) {
    const std::string wanted = NormalizePath(projectPath);
    std::vector<EditorInstanceInfo> result;
    for (auto& instance : List())
        if (NormalizePath(instance.projectPath) == wanted) result.push_back(std::move(instance));
    return result;
}

uint64_t CurrentProcessId() {
#ifdef _WIN32
    return static_cast<uint64_t>(GetCurrentProcessId());
#else
    return static_cast<uint64_t>(::getpid());
#endif
}

std::string MakeEditorEndpoint(uint64_t processId) {
#ifdef _WIN32
    return R"(\\.\pipe\mipsync.)" + std::to_string(processId);
#else
    return (fs::temp_directory_path() / ("mipsync." + std::to_string(processId) + ".sock")).string();
#endif
}

struct IpcServer::Impl {
    std::atomic<bool> running{false};
    std::string endpoint;
    Handler handler;
    std::thread thread;

    void Run() {
#ifdef _WIN32
        while (running.load()) {
            HANDLE pipe = CreateNamedPipeA(endpoint.c_str(), PIPE_ACCESS_DUPLEX,
                PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
                1, 1024 * 1024, 1024 * 1024, 0, nullptr);
            if (pipe == INVALID_HANDLE_VALUE) break;
            const BOOL connected = ConnectNamedPipe(pipe, nullptr) ? TRUE :
                (GetLastError() == ERROR_PIPE_CONNECTED);
            if (!connected) { CloseHandle(pipe); continue; }
            std::string payload;
            char buffer[8192];
            DWORD read = 0;
            do {
                const BOOL ok = ReadFile(pipe, buffer, sizeof(buffer), &read, nullptr);
                if (read) payload.append(buffer, read);
                if (ok || GetLastError() != ERROR_MORE_DATA) break;
            } while (true);
            if (running.load() && !payload.empty()) {
                const std::string response = handler(payload);
                DWORD written = 0;
                WriteFile(pipe, response.data(), static_cast<DWORD>(response.size()), &written, nullptr);
                FlushFileBuffers(pipe);
            }
            DisconnectNamedPipe(pipe);
            CloseHandle(pipe);
        }
#else
        const int server = ::socket(AF_UNIX, SOCK_STREAM, 0);
        if (server < 0) { running = false; return; }
        ::unlink(endpoint.c_str());
        sockaddr_un address{};
        address.sun_family = AF_UNIX;
        if (endpoint.size() >= sizeof(address.sun_path)) { ::close(server); running = false; return; }
        std::copy(endpoint.begin(), endpoint.end(), address.sun_path);
        if (::bind(server, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0 ||
            ::listen(server, 4) != 0) {
            ::close(server); running = false; return;
        }
        while (running.load()) {
            const int client = ::accept(server, nullptr, nullptr);
            if (client < 0) continue;
            std::string payload;
            char buffer[8192];
            for (;;) {
                const ssize_t count = ::recv(client, buffer, sizeof(buffer), 0);
                if (count <= 0) break;
                payload.append(buffer, static_cast<size_t>(count));
            }
            if (running.load() && !payload.empty()) {
                const std::string response = handler(payload);
                ::send(client, response.data(), response.size(), 0);
            }
            ::close(client);
        }
        ::close(server);
        ::unlink(endpoint.c_str());
#endif
        running = false;
    }
};

IpcServer::IpcServer() : m_Impl(std::make_unique<Impl>()) {}
IpcServer::~IpcServer() { Stop(); }

bool IpcServer::Start(const std::string& endpoint, Handler handler, std::string& outError) {
    if (m_Impl->running.load()) { outError = "IPC server is already running"; return false; }
    if (endpoint.empty() || !handler) { outError = "IPC endpoint and handler are required"; return false; }
    m_Impl->endpoint = endpoint;
    m_Impl->handler = std::move(handler);
    m_Impl->running = true;
    m_Impl->thread = std::thread([impl = m_Impl.get()] { impl->Run(); });
    return true;
}

void IpcServer::Stop() {
    if (!m_Impl || !m_Impl->running.exchange(false)) {
        if (m_Impl && m_Impl->thread.joinable()) m_Impl->thread.join();
        return;
    }
#ifdef _WIN32
    HANDLE wake = CreateFileA(m_Impl->endpoint.c_str(), GENERIC_READ | GENERIC_WRITE,
        0, nullptr, OPEN_EXISTING, 0, nullptr);
    if (wake != INVALID_HANDLE_VALUE) CloseHandle(wake);
#else
    const int wake = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (wake >= 0) {
        sockaddr_un address{};
        address.sun_family = AF_UNIX;
        std::copy(m_Impl->endpoint.begin(), m_Impl->endpoint.end(), address.sun_path);
        ::connect(wake, reinterpret_cast<sockaddr*>(&address), sizeof(address));
        ::close(wake);
    }
#endif
    if (m_Impl->thread.joinable()) m_Impl->thread.join();
}

bool IpcServer::IsRunning() const { return m_Impl && m_Impl->running.load(); }

bool IpcClient::IsAvailable(const EditorInstanceInfo& instance, int timeoutMilliseconds) {
    if (instance.endpoint.empty()) return false;
#ifdef _WIN32
    if (WaitNamedPipeA(instance.endpoint.c_str(), static_cast<DWORD>(timeoutMilliseconds)))
        return true;
    return GetLastError() == ERROR_PIPE_BUSY;
#else
    std::error_code ec;
    return fs::is_socket(fs::path(instance.endpoint), ec);
#endif
}

CommandResult IpcClient::Execute(const EditorInstanceInfo& instance,
                                 const CommandRequest& request,
                                 std::string& outError,
                                 int timeoutMilliseconds) {
    const std::string payload = request.ToJson().dump();
    std::string response;
#ifdef _WIN32
    if (!WaitNamedPipeA(instance.endpoint.c_str(), static_cast<DWORD>(timeoutMilliseconds))) {
        outError = "Editor IPC endpoint did not become available";
        return CommandResult::Fail("MIPSYNC_IPC_CONNECT", outError);
    }
    HANDLE pipe = CreateFileA(instance.endpoint.c_str(), GENERIC_READ | GENERIC_WRITE,
        0, nullptr, OPEN_EXISTING, 0, nullptr);
    if (pipe == INVALID_HANDLE_VALUE) {
        outError = "Unable to connect to Editor named pipe";
        return CommandResult::Fail("MIPSYNC_IPC_CONNECT", outError);
    }
    DWORD mode = PIPE_READMODE_MESSAGE;
    SetNamedPipeHandleState(pipe, &mode, nullptr, nullptr);
    DWORD written = 0;
    if (!WriteFile(pipe, payload.data(), static_cast<DWORD>(payload.size()), &written, nullptr)) {
        CloseHandle(pipe);
        outError = "Unable to write Editor request";
        return CommandResult::Fail("MIPSYNC_IPC_WRITE", outError);
    }
    char buffer[8192];
    DWORD read = 0;
    do {
        const BOOL ok = ReadFile(pipe, buffer, sizeof(buffer), &read, nullptr);
        if (read) response.append(buffer, read);
        if (ok || GetLastError() != ERROR_MORE_DATA) break;
    } while (true);
    CloseHandle(pipe);
#else
    const int client = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (client < 0) return CommandResult::Fail("MIPSYNC_IPC_CONNECT", "Unable to create Unix socket");
    timeval timeout{timeoutMilliseconds / 1000, (timeoutMilliseconds % 1000) * 1000};
    setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    if (instance.endpoint.size() >= sizeof(address.sun_path)) {
        ::close(client);
        return CommandResult::Fail("MIPSYNC_IPC_CONNECT", "Editor socket path is too long");
    }
    std::copy(instance.endpoint.begin(), instance.endpoint.end(), address.sun_path);
    if (::connect(client, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) {
        ::close(client);
        return CommandResult::Fail("MIPSYNC_IPC_CONNECT", "Unable to connect to Editor Unix socket");
    }
    ::send(client, payload.data(), payload.size(), 0);
    ::shutdown(client, SHUT_WR);
    char buffer[8192];
    for (;;) {
        const ssize_t count = ::recv(client, buffer, sizeof(buffer), 0);
        if (count <= 0) break;
        response.append(buffer, static_cast<size_t>(count));
    }
    ::close(client);
#endif
    if (response.empty()) {
        outError = "Editor returned an empty IPC response";
        return CommandResult::Fail("MIPSYNC_IPC_RESPONSE", outError);
    }
    return ParseResponse(response, request.requestId);
}

} // namespace MipsyncEngine::Command
