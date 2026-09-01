#pragma once

#include "CommandTypes.h"
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace MipsyncEngine::Command {

struct EditorInstanceInfo {
    std::string instanceId;
    uint64_t processId = 0;
    std::string projectPath;
    std::string projectId;
    std::string engineVersion;
    std::string executablePath;
    std::string endpoint;
    int protocolVersion = kProtocolVersion;

    Json ToJson() const;
    static EditorInstanceInfo FromJson(const Json& value);
};

class InstanceRegistry {
public:
    static bool Register(const EditorInstanceInfo& instance, std::string& outError);
    static void Unregister(const std::string& instanceId);
    static std::vector<EditorInstanceInfo> List();
    static std::vector<EditorInstanceInfo> FindForProject(const std::string& projectPath);
    static std::string RegistryDirectory();
};

class IpcServer {
public:
    using Handler = std::function<std::string(const std::string&)>;

    IpcServer();
    ~IpcServer();
    IpcServer(const IpcServer&) = delete;
    IpcServer& operator=(const IpcServer&) = delete;

    bool Start(const std::string& endpoint, Handler handler, std::string& outError);
    void Stop();
    bool IsRunning() const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_Impl;
};

class IpcClient {
public:
    static bool IsAvailable(const EditorInstanceInfo& instance,
                            int timeoutMilliseconds = 50);
    static CommandResult Execute(const EditorInstanceInfo& instance,
                                 const CommandRequest& request,
                                 std::string& outError,
                                 int timeoutMilliseconds = 5000);
};

uint64_t CurrentProcessId();
std::string MakeEditorEndpoint(uint64_t processId);

} // namespace MipsyncEngine::Command
