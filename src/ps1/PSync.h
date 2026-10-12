#pragma once

#include <array>
#include <cstdint>
#include <glm/glm.hpp>

namespace MipsyncEngine {

// Current export target: main.c uses MODE_NTSC, 320 x 240, non-interlaced.
// Hardware timings and software assumptions are documented in docs/PSync.md.
struct PSyncWorkload {
    uint64_t objects = 0, colliders = 0, colliderTriangles = 0, movingBodies = 0;
    uint64_t animationVertices = 0, animatedBones = 0, audioSources = 0;
    uint64_t submittedTriangles = 0, drawnTriangles = 0, projectedVertices = 0;
    uint64_t litVertices = 0, lights = 0, dmaWords = 2048;
    uint64_t litFaces = 0, pointLights = 0, spotLights = 0;
    uint64_t uiPrimitives = 0;
    uint64_t colorGradingPixels = 0;
    double gpuCycles = 320.0 * 240.0 / 2.0; // background clear, TILE
    double coveredPixels = 0.0;
    std::array<uint64_t, 256> opcodes{}, hostCalls{};

    // Clip to the PS1 drawing area BEFORE counting coverage. Deliberately no
    // depth buffer: hidden surfaces still cost fill-rate on PS1.
    void Triangle(const glm::vec4& a, const glm::vec4& b, const glm::vec4& c,
                  bool textured, bool gouraud = true, bool transparent = false,
                  bool doubleSided = true, int textureBits = 16);
    void UIQuad(const glm::vec4& a, const glm::vec4& b, const glm::vec4& c,
                const glm::vec4& d, bool textured);
};

struct PSyncGeometryCacheEntry {
    uint64_t projection = 0, lighting = 0;
};
struct PSyncDrawOptions {
    bool cacheable = true;
    bool flatShading = true;
    bool plane = false;
    uint8_t subdivisionLevels = 0;
};

struct PSyncEstimate {
    static constexpr double cpuHz = 33868800.0;
    static constexpr double videoHz = 53693175.0;
    static constexpr double refreshHz = videoHz / (3412.5 * 263.0);
    double cpuMs = 0.0, gpuMs = 0.0, dmaMs = 0.0;
    double fps = refreshHz, frameSeconds = 1.0 / refreshHz;
    uint32_t vblanks = 1;
};

PSyncEstimate EstimatePSync(const PSyncWorkload& workload);
float PSyncSimulationDeltaTime(uint32_t elapsedVblanks);

// Slow down immediately; only speed up after sustained lighter frames. This
// keeps a borderline workload from feeding alternating rates back into itself.
class PSyncCadence {
public:
    PSyncEstimate Update(PSyncEstimate sample, double now);
    void Reset();
    static constexpr double recoverySeconds = 0.5;
private:
    uint32_t m_Vblanks = 0, m_RecoveryVblanks = 0;
    double m_RecoverySince = -1.0, m_LastTime = -1.0;
};

// No sleeping or global editor FPS cap. Only the simulated game and its FBO
// advance at this cadence. Excess PC delay is consumed once, not replayed.
// Logic uses the export's nominal-60-Hz Q16.16 delta, capped at four fields.
class PSyncClock {
public:
    bool Advance(double elapsed, double interval);
    void Reset();
    double DeltaTime() const { return m_Delta; }
    float SimulationDeltaTime() const { return m_SimulationDelta; }
private:
    bool m_Started = false;
    double m_Elapsed = 0.0, m_Delta = 0.0;
    float m_SimulationDelta = 0.0f;
};

struct PSyncState {
    PSyncEstimate estimate;
    PSyncWorkload workload;
    PSyncClock clock;
    PSyncCadence cadence;
    bool frameDue = true;
    bool sampleValid = false;
};

} // namespace MipsyncEngine
