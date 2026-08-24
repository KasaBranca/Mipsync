#pragma once
// ─────────────────────────────────────────────────
// Mipsync Engine — fluid icon-trace boot animation
// ─────────────────────────────────────────────────

namespace MipsyncEngine {

class BootSplash {
public:
    /// Blocks until the complete Mipsync icon trace has finished.
    static void Play();
};

} // namespace MipsyncEngine
