#include "OsFileDrop.h"
#include "Log.h"
#include <GLFW/glfw3.h>
#include <mutex>

namespace MipsyncEngine {

namespace {

std::mutex s_Mutex;
std::vector<std::string> s_Pending;

} // namespace

void OsFileDrop::Init(GLFWwindow* window) {
    if (!window) return;
    glfwSetDropCallback(window, DropCallback);
}

const std::vector<std::string>& OsFileDrop::Peek() {
    return s_Pending;
}

std::vector<std::string> OsFileDrop::Consume() {
    std::lock_guard lock(s_Mutex);
    std::vector<std::string> out = std::move(s_Pending);
    s_Pending.clear();
    return out;
}

void OsFileDrop::Clear() {
    std::lock_guard lock(s_Mutex);
    s_Pending.clear();
}

void OsFileDrop::DropCallback(GLFWwindow* /*window*/, int count, const char** paths) {
    if (!paths || count <= 0) return;

    std::lock_guard lock(s_Mutex);
    s_Pending.reserve(s_Pending.size() + static_cast<size_t>(count));
    for (int i = 0; i < count; ++i) {
        if (paths[i] && paths[i][0])
            s_Pending.emplace_back(paths[i]);
    }
    MIPSYNC_DEBUG("OS file drop: {} path(s) queued", count);
}

} // namespace MipsyncEngine
