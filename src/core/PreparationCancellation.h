#pragma once
#include <atomic>
#include <exception>

namespace MipsyncEngine {
// Only the Play preparation worker installs this token. Normal runtime/editor
// calls have no token, including teardown and restoring the original scene.
inline thread_local const std::atomic<bool>* g_PreparationCancellation = nullptr;

class PreparationCancelled : public std::exception {
public:
    const char* what() const noexcept override { return "Play preparation cancelled"; }
};

class ScopedPreparationCancellation {
public:
    explicit ScopedPreparationCancellation(const std::atomic<bool>& token)
        : m_Previous(g_PreparationCancellation) { g_PreparationCancellation = &token; }
    ~ScopedPreparationCancellation() { g_PreparationCancellation = m_Previous; }
    ScopedPreparationCancellation(const ScopedPreparationCancellation&) = delete;
    ScopedPreparationCancellation& operator=(const ScopedPreparationCancellation&) = delete;
private:
    const std::atomic<bool>* m_Previous;
};

inline void CheckPreparationCancelled() {
    if (g_PreparationCancellation && g_PreparationCancellation->load(std::memory_order_relaxed))
        throw PreparationCancelled();
}
} // namespace MipsyncEngine
