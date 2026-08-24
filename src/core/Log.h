#pragma once
// ─────────────────────────────────────────────────
// Mipsync Engine — Structured Logging
// Wraps spdlog for engine + editor console output
// ─────────────────────────────────────────────────

#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/ringbuffer_sink.h>
#include <memory>
#include <string>
#include <vector>
#include <mutex>

namespace MipsyncEngine {

struct LogEntry {
    spdlog::level::level_enum level;
    std::string message;
};

class Log {
public:
    static void Init();
    static void Shutdown();

    /// Append engine logs to a file (flushed every message). Safe to call once after project path is known.
    static void AddFileSink(const std::string& absolutePath);
    static void Flush();

    static std::shared_ptr<spdlog::logger>& GetEngineLogger()  { return s_EngineLogger; }
    static std::shared_ptr<spdlog::logger>& GetEditorLogger()  { return s_EditorLogger; }

    // Get recent log entries for the console panel
    static std::vector<LogEntry> GetRecentEntries();
    static void ClearEntries();

private:
    static std::shared_ptr<spdlog::logger> s_EngineLogger;
    static std::shared_ptr<spdlog::logger> s_EditorLogger;
    static std::shared_ptr<spdlog::sinks::ringbuffer_sink_mt> s_RingBuffer;
    static std::mutex s_Mutex;
};

} // namespace MipsyncEngine

// Engine logging macros
#define MIPSYNC_TRACE(...)    ::MipsyncEngine::Log::GetEngineLogger()->trace(__VA_ARGS__)
#define MIPSYNC_DEBUG(...)    ::MipsyncEngine::Log::GetEngineLogger()->debug(__VA_ARGS__)
#define MIPSYNC_INFO(...)     ::MipsyncEngine::Log::GetEngineLogger()->info(__VA_ARGS__)
#define MIPSYNC_WARN(...)     ::MipsyncEngine::Log::GetEngineLogger()->warn(__VA_ARGS__)
#define MIPSYNC_ERROR(...)    ::MipsyncEngine::Log::GetEngineLogger()->error(__VA_ARGS__)
#define MIPSYNC_FATAL(...)    ::MipsyncEngine::Log::GetEngineLogger()->critical(__VA_ARGS__)

// Editor logging macros
#define EDITOR_TRACE(...)     ::MipsyncEngine::Log::GetEditorLogger()->trace(__VA_ARGS__)
#define EDITOR_INFO(...)      ::MipsyncEngine::Log::GetEditorLogger()->info(__VA_ARGS__)
#define EDITOR_WARN(...)      ::MipsyncEngine::Log::GetEditorLogger()->warn(__VA_ARGS__)
#define EDITOR_ERROR(...)     ::MipsyncEngine::Log::GetEditorLogger()->error(__VA_ARGS__)
