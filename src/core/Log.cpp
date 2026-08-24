#include "Log.h"
#include "EngineVersion.h"
#include <spdlog/sinks/basic_file_sink.h>

namespace MipsyncEngine {

std::shared_ptr<spdlog::logger> Log::s_EngineLogger;
std::shared_ptr<spdlog::logger> Log::s_EditorLogger;
std::shared_ptr<spdlog::sinks::ringbuffer_sink_mt> Log::s_RingBuffer;
std::mutex Log::s_Mutex;

void Log::Init() {
    // Ring buffer to hold last 256 messages for console panel
    s_RingBuffer = std::make_shared<spdlog::sinks::ringbuffer_sink_mt>(256);
    s_RingBuffer->set_pattern("[%T] [%l] %v");

    auto consoleSink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    consoleSink->set_pattern("%^[%T] [%n] %v%$");

    // Engine logger
    std::vector<spdlog::sink_ptr> engineSinks = { consoleSink, s_RingBuffer };
    s_EngineLogger = std::make_shared<spdlog::logger>("MIPSYNC", engineSinks.begin(), engineSinks.end());
    s_EngineLogger->set_level(spdlog::level::trace);
    s_EngineLogger->flush_on(spdlog::level::trace);
    spdlog::register_logger(s_EngineLogger);

    // Editor logger
    std::vector<spdlog::sink_ptr> editorSinks = { consoleSink, s_RingBuffer };
    s_EditorLogger = std::make_shared<spdlog::logger>("EDITOR", editorSinks.begin(), editorSinks.end());
    s_EditorLogger->set_level(spdlog::level::trace);
    s_EditorLogger->flush_on(spdlog::level::trace);
    spdlog::register_logger(s_EditorLogger);

    MIPSYNC_INFO("Mipsync Engine v{} — Logging initialized", MIPSYNC_ENGINE_VERSION);
}

void Log::Shutdown() {
    Flush();
    spdlog::shutdown();
}

void Log::AddFileSink(const std::string& absolutePath) {
    if (absolutePath.empty() || !s_EngineLogger)
        return;
    static std::string s_FileSinkPath;
    if (s_FileSinkPath == absolutePath)
        return;
    s_FileSinkPath = absolutePath;
    auto fileSink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(absolutePath, true);
    fileSink->set_pattern("[%Y-%m-%d %T] [%l] %v");
    s_EngineLogger->sinks().push_back(fileSink);
    if (s_EditorLogger)
        s_EditorLogger->sinks().push_back(fileSink);
    Flush();
    MIPSYNC_INFO("File log: {}", absolutePath);
}

void Log::Flush() {
    if (s_EngineLogger)
        s_EngineLogger->flush();
    if (s_EditorLogger)
        s_EditorLogger->flush();
}

std::vector<LogEntry> Log::GetRecentEntries() {
    std::lock_guard<std::mutex> lock(s_Mutex);
    std::vector<LogEntry> entries;
    auto raw = s_RingBuffer->last_formatted();
    // Parse raw formatted strings into LogEntry
    for (auto& msg : raw) {
        LogEntry entry;
        // Detect level from formatted string
        if (msg.find("[trace]") != std::string::npos)      entry.level = spdlog::level::trace;
        else if (msg.find("[debug]") != std::string::npos) entry.level = spdlog::level::debug;
        else if (msg.find("[info]") != std::string::npos)  entry.level = spdlog::level::info;
        else if (msg.find("[warning]") != std::string::npos) entry.level = spdlog::level::warn;
        else if (msg.find("[error]") != std::string::npos) entry.level = spdlog::level::err;
        else if (msg.find("[critical]") != std::string::npos) entry.level = spdlog::level::critical;
        else entry.level = spdlog::level::info;
        entry.message = msg;
        entries.push_back(std::move(entry));
    }
    return entries;
}

void Log::ClearEntries() {
    // Ring buffer doesn't support clear, but we can create a new one
    // For now, this is a no-op since the ring buffer overwrites old entries
}

} // namespace MipsyncEngine
