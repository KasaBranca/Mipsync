#include "Time.h"
#include <GLFW/glfw3.h>

namespace MipsyncEngine {

float Time::s_DeltaTime = 0.0f;
float Time::s_Time = 0.0f;
float Time::s_LastFrameTime = 0.0f;
float Time::s_FPS = 0.0f;
int   Time::s_FrameCount = 0;
float Time::s_FPSTimer = 0.0f;
int   Time::s_FPSFrameCount = 0;

void Time::Init() {
    s_LastFrameTime = static_cast<float>(glfwGetTime());
}

void Time::Update() {
    float currentTime = static_cast<float>(glfwGetTime());
    s_DeltaTime = currentTime - s_LastFrameTime;
    s_LastFrameTime = currentTime;
    s_Time = currentTime;
    s_FrameCount++;

    // FPS counter (update every 0.5 seconds)
    s_FPSTimer += s_DeltaTime;
    s_FPSFrameCount++;
    if (s_FPSTimer >= 0.5f) {
        s_FPS = static_cast<float>(s_FPSFrameCount) / s_FPSTimer;
        s_FPSTimer = 0.0f;
        s_FPSFrameCount = 0;
    }
}

} // namespace MipsyncEngine
