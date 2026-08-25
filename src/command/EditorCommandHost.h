#pragma once

#include "CommandExecutor.h"
#include "CoreCommands.h"
#include "IpcTransport.h"
#include "SymbolRegistry.h"

#include <future>
#include <memory>
#include <mutex>
#include <queue>
#include <string>

namespace MipsyncEngine {
class Engine;
class Entity;
}

namespace MipsyncEngine::Command {

class EditorCommandHost final : public IEditorCommandService {
public:
    explicit EditorCommandHost(Engine& engine);
    ~EditorCommandHost();

    bool Start(std::string& outError);
    void Stop();
    void Pump();
    std::string ExecuteConsoleLine(const std::string& line);

    CommandResult ExecuteEditorCommand(const CommandRequest& request) override;

private:
    struct PendingRequest {
        CommandRequest request;
        std::promise<std::string> response;
    };

    std::string HandleIpcPayload(const std::string& payload);
    Json DescribeEntity(Entity& entity) const;

    Engine& m_Engine;
    CommandRegistry m_Commands;
    SymbolRegistry m_Symbols;
    CommandExecutor m_Executor;
    CommandContext m_Context;
    IpcServer m_Server;
    EditorInstanceInfo m_Instance;
    std::mutex m_QueueMutex;
    std::queue<std::shared_ptr<PendingRequest>> m_Pending;
};

} // namespace MipsyncEngine::Command
