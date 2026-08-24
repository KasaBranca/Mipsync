#pragma once
// ─────────────────────────────────────────────────
// Mipsync Engine — Time Management
// ─────────────────────────────────────────────────

namespace MipsyncEngine {

class Time {
public:
    static void Init();
    static void Update();

    static float GetDeltaTime()  { return s_DeltaTime; }
    static float GetTime()       { return s_Time; }
    static float GetFPS()        { return s_FPS; }
    static int   GetFrameCount() { return s_FrameCount; }

private:
    static float s_DeltaTime;
    static float s_Time;
    static float s_LastFrameTime;
    static float s_FPS;
    static int   s_FrameCount;
    static float s_FPSTimer;
    static int   s_FPSFrameCount;
};

} // namespace MipsyncEngine
