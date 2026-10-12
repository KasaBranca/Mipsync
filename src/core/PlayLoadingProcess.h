#pragma once
#include <memory>
#include <string>
#include <cstdint>

struct GLFWwindow;
namespace MipsyncEngine {

// Independent message loop: the progress window never waits for asset/physics
// work or shares an OpenGL context with the editor.
class PlayLoadingProcess {
public:
    PlayLoadingProcess();
    ~PlayLoadingProcess();
    bool Start(GLFWwindow* owner);
    void SetStage(int stage);
    void RequestCancel();
    bool CancelRequested() const;
    void Stop();
    bool Running() const;
    uint32_t ProcessId() const;
private:
    struct Impl;
    std::unique_ptr<Impl> m_Impl;
};

// Invoked before normal CLI/project/graphics initialization in the child.
int RunPlayLoadingProcess(const std::string& mappingName);
} // namespace MipsyncEngine
