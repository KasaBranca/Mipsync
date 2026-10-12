#include "PSync.h"
#include "../mips/Bytecode.h"
#include "../../templates/ps1/starter/runtime/frame_timing.h"
#include <algorithm>
#include <cmath>

namespace MipsyncEngine {

void PSyncWorkload::Triangle(const glm::vec4& a, const glm::vec4& b, const glm::vec4& c,
                            bool textured, bool gouraud, bool transparent,
                            bool doubleSided, int textureBits) {
    std::array<glm::dvec4, 16> polygon{}, clipped{};
    polygon[0] = a; polygon[1] = b; polygon[2] = c;
    int count = 3;
    for (const auto& v : {a, b, c})
        for (int i = 0; i < 4; ++i) if (!std::isfinite(v[i])) return;
    for (int plane = 0; plane < 7 && count; ++plane) {
        const auto distance = [plane](const glm::dvec4& p) {
            if (plane == 6) return p.w - 1e-8;
            return p.w + (plane % 2 ? -p[plane / 2] : p[plane / 2]);
        };
        int n = 0;
        glm::dvec4 previous = polygon[count - 1];
        double dp = distance(previous);
        for (int i = 0; i < count; ++i) {
            const glm::dvec4 current = polygon[i];
            const double dc = distance(current);
            if ((dp >= 0.0) != (dc >= 0.0))
                clipped[n++] = previous + (current - previous) * (dp / (dp - dc));
            if (dc >= 0.0) clipped[n++] = current;
            previous = current; dp = dc;
        }
        polygon = clipped; count = n;
    }
    if (count < 3) return;
    double area = 0.0;
    for (int i = 0; i < count; ++i) {
        const auto p = glm::dvec2(polygon[i]) / polygon[i].w;
        const auto q = glm::dvec2(polygon[(i + 1) % count]) / polygon[(i + 1) % count].w;
        area += p.x * q.y - p.y * q.x;
    }
    if (!doubleSided && area <= 0.0) return;
    const double pixels = std::abs(area) * (320.0 * 240.0 / 8.0);
    if (pixels < 1e-8) return;
    ++drawnTriangles;
    coveredPixels += pixels;
    // Psy-Q 4.7, chapter 8-28: flat writes 2 pixels/cycle; Gouraud writes
    // 1. Texture cache misses add one read per VRAM halfword (4/8/16 bit).
    // Use the cache-miss side of the documented range, not an invented
    // polygons/second cap. Transparent primitives also read the destination.
    gpuCycles += pixels * ((gouraud || textured ? 1.0 : 0.5) +
        (textured ? std::clamp(textureBits, 4, 16) / 16.0 : 0.0) +
        (transparent ? 1.0 : 0.0));
    dmaWords += textured ? (gouraud ? 10 : 8) : (gouraud ? 7 : 5);
}

void PSyncWorkload::UIQuad(const glm::vec4& a, const glm::vec4& b, const glm::vec4& c,
                         const glm::vec4& d, bool textured) {
    const uint64_t previousTriangles = drawnTriangles, previousDma = dmaWords;
    Triangle(a, b, c, textured, false);
    Triangle(a, c, d, textured, false);
    const bool visible = drawnTriangles != previousTriangles;
    drawnTriangles = previousTriangles;
    dmaWords = previousDma;
    if (visible) {
        ++uiPrimitives;
        dmaWords += textured ? 10 : 4; // FT4 or TILE, including linked-list tag
    }
}

PSyncEstimate EstimatePSync(const PSyncWorkload& w) {
    using Mips::OpCode;
    using Mips::HostFunc;
    // Software instruction budgets, not hardware constants. Includes VM
    // dispatch/tag checks, loads and branch delay slots; see documentation.
    double cycles = 10000.0 + w.objects * 1200.0 + w.submittedTriangles * 650.0 +
        w.drawnTriangles * 1000.0 + w.projectedVertices * 160.0 +
        (w.projectedVertices / 3) * 23.0 + (w.projectedVertices % 3) * 15.0 +
        w.animationVertices * 48.0 + w.animatedBones * 140.0 + w.audioSources * 180.0 +
        w.uiPrimitives * 60.0;
    // Compiled runtime shade_scene_normal includes reciprocal-scale normal
    // correction, world-space transform and software lighting, not just NCS.
    // Flat props call it per face; point/spot lights include normalization,
    // integer square root, division and attenuation. Calibrated against the
    // standard-clock exported renderer; measurements are in docs/PSync.md.
    const uint64_t localLights = std::min<uint64_t>(w.pointLights + w.spotLights, 8);
    const uint64_t directionalLights = std::min<uint64_t>(w.lights, 8) -
        std::min<uint64_t>(std::min<uint64_t>(w.lights, 8), localLights);
    const double lighting = w.lights ? 900.0 + directionalLights * 150.0 +
        localLights * 1500.0 + std::min<uint64_t>(w.spotLights, 8) * 350.0 : 80.0;
    cycles += w.litVertices * lighting + w.litFaces * (lighting + 400.0);
    const double colliderTest = w.colliders * 120.0 + w.colliderTriangles * 90.0;
    cycles += w.movingBodies * (700.0 + colliderTest * 4.0);
    for (size_t i = 0; i < w.opcodes.size(); ++i) {
        double cost = 36.0;
        switch (static_cast<OpCode>(i)) {
            case OpCode::Mul: cost = 62; break;
            case OpCode::Div: case OpCode::Mod: cost = 100; break;
            case OpCode::GetComponent: case OpCode::GetGlobal: cost = 90; break;
            case OpCode::GetMember: case OpCode::SetVec3Axis:
            case OpCode::SetVec3FromValue: cost = 110; break;
            case OpCode::CallHost: cost = 80; break;
            case OpCode::NewArray: case OpCode::NewArraySized:
            case OpCode::ArrayRemoveAt: cost = 256; break;
            default: break;
        }
        cycles += w.opcodes[i] * cost;
    }
    for (size_t i = 0; i < w.hostCalls.size(); ++i) {
        double cost = 70.0;
        switch (static_cast<HostFunc>(i)) {
            case HostFunc::Physics_Raycast: case HostFunc::Physics_IsGrounded:
            case HostFunc::Physics_IsGroundedWithin: cost += colliderTest; break;
            case HostFunc::Physics_Move: case HostFunc::Physics_MoveKinematic:
                cost += colliderTest * 4.0; break;
            case HostFunc::Vector3_Normalize: case HostFunc::Vector3_Length:
            case HostFunc::Mathf_Sqrt: case HostFunc::Mathf_Atan2: cost += 350; break;
            case HostFunc::Mathf_Sin: case HostFunc::Mathf_Cos: cost += 100; break;
            default: break;
        }
        cycles += w.hostCalls[i] * cost;
    }
    PSyncEstimate out;
    out.cpuMs = cycles / PSyncEstimate::cpuHz * 1000.0;
    out.gpuMs = w.gpuCycles / PSyncEstimate::cpuHz * 1000.0;
    // GPU/OTC DMA: one word/cycle plus DRAM row changes (~17/16).
    out.dmaMs = w.dmaWords * (17.0 / 16.0) / PSyncEstimate::cpuHz * 1000.0;
    // PresentFrame builds next buffer while GPU draws the previous buffer,
    // waits at DrawSync and presents at VSync. CPU and GPU aren't summed.
    // Grading runs after DrawSync, before UI: this work cannot overlap the
    // world's GPU pass. Budget the RGB555 lookup loop plus read/write DMA.
    // Packed word I/O + identical-pixel/pair reuse: ~340-350 HBlanks for
    // the native-clock ReviewDemo 320x240 pass (previous scalar loop ~800).
    // Calibrated scene average; texture/colour entropy can change cache reuse.
    const double gradingCpuMs = w.colorGradingPixels * 10.0 / PSyncEstimate::cpuHz * 1000.0;
    // Unlike linked-list DMA, VRAM readback/upload must wait for GPU transfer
    // readiness on every strip. 120 HBlanks for 76800 pixels at native clocks.
    const double gradingDmaMs = w.colorGradingPixels * 3.4 / PSyncEstimate::cpuHz * 1000.0;
    // With grading, PresentFrame submits the current world's OT then waits
    // for it before reading pixels. That GPU work cannot overlap the CPU
    // which built this same OT. The ungraded asynchronous path still can.
    const double worldMs = w.colorGradingPixels
        ? out.cpuMs + out.dmaMs + out.gpuMs
        : std::max(out.cpuMs + out.dmaMs, out.gpuMs);
    const double workSeconds = (worldMs + gradingCpuMs + gradingDmaMs) / 1000.0;
    out.cpuMs += gradingCpuMs;
    out.dmaMs += gradingDmaMs;
    const double slots = std::ceil(workSeconds * PSyncEstimate::refreshHz - 1e-10);
    out.vblanks = static_cast<uint32_t>(std::clamp(slots, 1.0, 1000000.0));
    out.frameSeconds = out.vblanks / PSyncEstimate::refreshHz;
    out.fps = 1.0 / out.frameSeconds;
    return out;
}

float PSyncSimulationDeltaTime(uint32_t elapsedVblanks) {
    return ps1_frame_delta_q16(static_cast<int32_t>(std::min<uint32_t>(elapsedVblanks, PS1_MAX_LOGIC_VBLANKS))) / 65536.0f;
}

PSyncEstimate PSyncCadence::Update(PSyncEstimate sample, double now) {
    sample.vblanks = std::max(1u, sample.vblanks);
    if (!std::isfinite(now) || (m_LastTime >= 0.0 && now < m_LastTime)) {
        Reset();
        now = 0.0;
    }
    m_LastTime = now;
    if (!m_Vblanks || sample.vblanks >= m_Vblanks) {
        m_Vblanks = sample.vblanks;
        m_RecoverySince = -1.0;
        m_RecoveryVblanks = 0;
    } else {
        if (m_RecoverySince < 0.0) {
            m_RecoverySince = now;
            m_RecoveryVblanks = sample.vblanks;
        } else {
            m_RecoveryVblanks = std::max(m_RecoveryVblanks, sample.vblanks);
        }
        if (now - m_RecoverySince + 1e-10 >= recoverySeconds) {
            // Retain the slowest requirement observed throughout recovery,
            // rather than trusting only its final, possibly lighter sample.
            m_Vblanks = m_RecoveryVblanks;
            m_RecoverySince = -1.0;
            m_RecoveryVblanks = 0;
        }
    }
    sample.vblanks = m_Vblanks;
    sample.frameSeconds = sample.vblanks / PSyncEstimate::refreshHz;
    sample.fps = 1.0 / sample.frameSeconds;
    return sample;
}

void PSyncCadence::Reset() {
    m_Vblanks = m_RecoveryVblanks = 0;
    m_RecoverySince = m_LastTime = -1.0;
}

bool PSyncClock::Advance(double elapsed, double interval) {
    if (!std::isfinite(interval) || interval <= 0.0) interval = 1.0 / PSyncEstimate::refreshHz;
    if (!std::isfinite(elapsed) || elapsed < 0.0) elapsed = 0.0;
    if (!m_Started) {
        m_Started = true;
        m_Delta = interval;
        // PS1 sets last_vblank immediately before entering its loop. Its first
        // Update therefore receives the minimum one-field delta.
        m_SimulationDelta = PSyncSimulationDeltaTime(1);
        return true;
    }
    m_Elapsed += elapsed;
    if (m_Elapsed + 1e-10 < interval) return false;
    const double periods = std::max(1.0, std::floor((m_Elapsed + 1e-10) / interval));
    m_Delta = periods * interval;
    const uint32_t fields = static_cast<uint32_t>(std::clamp(
        std::round(m_Delta * PSyncEstimate::refreshHz), 1.0, double(PS1_MAX_LOGIC_VBLANKS)));
    m_SimulationDelta = PSyncSimulationDeltaTime(fields);
    m_Elapsed = std::max(0.0, m_Elapsed - m_Delta);
    return true;
}

void PSyncClock::Reset() { m_Started = false; m_Elapsed = m_Delta = 0.0; m_SimulationDelta = 0.0f; }

} // namespace MipsyncEngine
