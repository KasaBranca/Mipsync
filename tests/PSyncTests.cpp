#include "ps1/PSync.h"
#include "ps1/PSyncGeometry.h"
#include "core/GameInputBuffer.h"
#include "mips/Bytecode.h"
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <limits>

extern "C" int CheckPS1FrameTiming(void);

using namespace MipsyncEngine;
static void Require(bool value, const char* text) { if (!value) throw std::runtime_error(text); }
static bool Near(double a, double b, double epsilon = 1e-6) { return std::abs(a - b) < epsilon; }

int main() {
    try {
        Require(Near(PSyncEstimate::refreshHz, 59.826097856, 1e-5), "NTSC hardware clock, not nominal 60 Hz");
        Require(CheckPS1FrameTiming() == 0, "shared C timing matches exported fixed-point division");
        for (uint32_t fields = 1; fields <= 4; ++fields)
            Require(PSyncSimulationDeltaTime(fields) == (fields * 65536 / 60) / 65536.0f,
                    "script delta truncates to PS1 Q16.16 at nominal 60 Hz");
        Require(PSyncSimulationDeltaTime(0) == PSyncSimulationDeltaTime(1), "minimum one logic field");
        Require(PSyncSimulationDeltaTime(std::numeric_limits<uint32_t>::max()) == PSyncSimulationDeltaTime(4),
                "long frames cap simulation at four logic fields");
        PSyncWorkload flat;
        const glm::vec4 a(-1, -1, 0, 1), b(1, -1, 0, 1), c(-1, 1, 0, 1), d(1, 1, 0, 1);
        flat.Triangle(a, b, c, false, false);
        flat.Triangle(b, d, c, false, false);
        Require(Near(flat.coveredPixels, 320.0 * 240.0), "full-screen triangle area");
        Require(Near(flat.gpuCycles, 76800.0), "clear plus flat full-screen writes");
        PSyncWorkload ui;
        ui.UIQuad(a, b, d, c, false);
        Require(ui.uiPrimitives == 1 && ui.drawnTriangles == 0 && ui.dmaWords == 2052,
                "UI tile costs one packet, not two 3D triangles");
        Require(Near(ui.gpuCycles, flat.gpuCycles), "flat UI tile fill rate");
        auto texture = flat;
        texture.Triangle(a, b, c, true);
        texture.Triangle(b, d, c, true);
        Require(Near(texture.coveredPixels, 153600.0), "PS1 overdraw includes hidden surfaces");
        Require(Near(texture.gpuCycles - flat.gpuCycles, 153600.0), "16-bit texture read and shaded write");
        PSyncWorkload clipped;
        clipped.Triangle(glm::vec4(-3,-3,0,1), glm::vec4(5,-3,0,1), glm::vec4(-3,5,0,1), true);
        Require(Near(clipped.coveredPixels, 76800.0), "clip outside viewport, not bounding box area");
        clipped.Triangle(glm::vec4(2,2,0,1), glm::vec4(3,2,0,1), glm::vec4(2,3,0,1), true);
        Require(clipped.drawnTriangles == 1, "off-screen primitive excluded");
        clipped.Triangle(glm::vec4(0,0,0,-1), glm::vec4(1,0,0,-1), glm::vec4(0,1,0,-1), true);
        clipped.Triangle(glm::vec4(std::numeric_limits<float>::quiet_NaN()), b, c, true);
        Require(clipped.drawnTriangles == 1, "behind-camera and nonfinite primitives excluded");
        PSyncWorkload winding;
        winding.Triangle(a,c,b,false,true,false,false);
        Require(winding.drawnTriangles == 0, "backface culling");
        winding.Triangle(a,c,b,false,true,false,true);
        Require(winding.drawnTriangles == 1, "double sided meshes");
        std::vector<glm::vec4> geometry = {a,b,c};
        std::vector<uint32_t> indices = {0,1,2};
        std::unordered_map<uint64_t, PSyncGeometryCacheEntry> previous, current;
        uint64_t cached = 0;
        PSyncWorkload cold;
        ProfilePSyncGeometry(cold, geometry, indices, 0, 3,
            [](const glm::vec4& v) { return v; }, true, false, 0, previous, current, cached);
        Require(cold.projectedVertices == 3 && cold.litVertices == 3, "cold cache projects used vertices");
        previous.swap(current); current.clear(); cached = 0;
        PSyncWorkload warm;
        ProfilePSyncGeometry(warm, geometry, indices, 0, 3,
            [](const glm::vec4& v) { return v; }, true, false, 0, previous, current, cached);
        Require(warm.projectedVertices == 0 && warm.drawnTriangles == 1 && warm.coveredPixels == cold.coveredPixels,
                "warm projection cache still pays raster and packet costs");
        previous.swap(current); current.clear(); cached = 0;
        PSyncWorkload moved;
        ProfilePSyncGeometry(moved, geometry, indices, 0, 3,
            [](const glm::vec4& v) { return v + glm::vec4(0.1f,0,0,0); }, true, false, 0,
            previous, current, cached);
        Require(moved.projectedVertices == 3, "camera/object/animation movement invalidates projection cache");
        Require(moved.litVertices == 0, "camera motion must not invalidate world lighting");
        current.clear(); cached = 0;
        PSyncWorkload relit;
        ProfilePSyncGeometry(relit, geometry, indices, 0, 3,
            [](const glm::vec4& v) { return v; }, true, false, 1, previous, current, cached);
        Require(relit.litVertices == 3, "light changes invalidate lighting cache");
        Require(relit.projectedVertices == 0, "light changes must not invalidate projection");
        PSyncWorkload faces;
        current.clear(); cached = 0;
        ProfilePSyncGeometry(faces, geometry, indices, 0, 3,
            [](const glm::vec4& v) { return v; }, true, false, 0, previous, current, cached, 21, true);
        Require(faces.litFaces == 1 && faces.litVertices == 0, "flat props light faces rather than shared vertices");
        previous.swap(current); current.clear(); cached = 0;
        PSyncWorkload anotherInstance;
        ProfilePSyncGeometry(anotherInstance, geometry, indices, 0, 3,
            [](const glm::vec4& v) { return v; }, true, false, 0, previous, current, cached, 22, true);
        Require(anotherInstance.litFaces == 1, "shared mesh instances must not alias a cache entry");
        PSyncWorkload uncached;
        ProfilePSyncGeometry(uncached, geometry, indices, 0, 3,
            [](const glm::vec4& v) { return v; }, true, false, 0, previous, current, cached, 21, true, false);
        Require(uncached.litFaces == 1 && uncached.projectedVertices == 3, "uncached runtime paths must pay each frame");
        PSyncWorkload unlit;
        previous.clear();current.clear();cached=0;
        ProfilePSyncGeometry(unlit, geometry, indices, 0, 3,
            [](const glm::vec4& v) {return v;},true,false,0,previous,current,cached,21,true,true,false);
        Require(unlit.litFaces==0&&unlit.litVertices==0&&unlit.projectedVertices==3&&unlit.drawnTriangles==1,
            "Unlit skips lighting cost while keeping geometry and raster costs");
        Require(unlit.dmaWords==uncached.dmaWords&&unlit.gpuCycles==uncached.gpuCycles,
            "Unlit keeps the actual PS1 Gouraud packet/raster costs");
        previous.swap(current);current.clear();cached=0;
        PSyncWorkload toggledLit;
        ProfilePSyncGeometry(toggledLit, geometry, indices, 0, 3,
            [](const glm::vec4& v) {return v;},true,false,0,previous,current,cached,21,true,true,true);
        Require(toggledLit.litFaces==1&&toggledLit.projectedVertices==0,
            "Unlit to Lit invalidates lighting while retaining projection cache");
        const std::vector<glm::vec4> oversized(4097, a);
        previous.clear(); current.clear(); cached = 0;
        for (int frame = 0; frame < 2; ++frame) {
            PSyncWorkload large;
            ProfilePSyncGeometry(large, oversized, indices, 0, 3,
                [](const glm::vec4& v) { return v; }, true, false, 0, previous, current, cached, 0, true);
            Require(large.litFaces == 1 && current.empty(), "meshes above the PS1 4096-vertex cache limit stay uncached");
            previous.swap(current); cached = 0;
        }
        Require(PSyncPlaneAxisTiles(4.91f, 0) == 5 && PSyncPlaneAxisTiles(2.f, 0) == 4 &&
                PSyncPlaneAxisTiles(-10.f, 0) == 10 && PSyncPlaneAxisTiles(1.f, 3) == 8 &&
                PSyncPlaneAxisTiles(100.f/8, 0) == 13, "PS1 plane repeat and subdivision grid");
        for (int tiling : {0,1,-1,512,-512,1257,-1257,4096,-4096})
        for (int offset : {0,64,-64,255,32767,-32768}) for (int levels : {0,3,4}) {
            ps1_plane_span spans[PS1_PLANE_AXIS_CAP];
            const int count=ps1_plane_axis(tiling,offset,levels,spans);
            Require(count>0 && count<=PS1_PLANE_AXIS_CAP && spans[0].start_q16==0 &&
                spans[count-1].end_q16==65536,"floor cells cover the entire axis");
            for (int i=0;i<count;++i) {
                Require(spans[i].start_q16<spans[i].end_q16 &&
                    (!i || spans[i-1].end_q16==spans[i].start_q16),"floor cells have no geometry gaps");
                Require(spans[i].uv_start_q8>=0 && spans[i].uv_start_q8<=256 &&
                    spans[i].uv_end_q8>=0 && spans[i].uv_end_q8<=256,"UV stays in one texture repeat");
                Require(!tiling || (tiling>0 ? spans[i].uv_end_q8>=spans[i].uv_start_q8 :
                    spans[i].uv_end_q8<=spans[i].uv_start_q8),"floor UV never interpolates across a wrap");
            }
        }
        PSyncWorkload gpuBound;
        gpuBound.gpuCycles = PSyncEstimate::cpuHz / PSyncEstimate::refreshHz * 1.01;
        const auto gpuEstimate = EstimatePSync(gpuBound);
        Require(gpuEstimate.vblanks == 2 && Near(gpuEstimate.fps, PSyncEstimate::refreshHz / 2), "missed VBlank => 29.9 FPS");
        PSyncWorkload raw178;
        raw178.gpuCycles = PSyncEstimate::cpuHz / 17.8;
        const auto synchronized178 = EstimatePSync(raw178);
        Require(synchronized178.vblanks == 4 && Near(synchronized178.fps, PSyncEstimate::refreshHz / 4),
                "17.8 FPS raw processing rate becomes 15 FPS at PS1 VSync");
        auto cpuBound = PSyncWorkload{};
        cpuBound.opcodes[static_cast<int>(Mips::OpCode::Div)] = 12000;
        const auto cpuEstimate = EstimatePSync(cpuBound);
        Require(cpuEstimate.vblanks >= 3 && cpuEstimate.cpuMs > cpuEstimate.gpuMs, "script-bound frame rate");
        auto overlap = cpuBound;
        overlap.gpuCycles = cpuEstimate.cpuMs * PSyncEstimate::cpuHz / 1000.0;
        Require(EstimatePSync(overlap).vblanks == cpuEstimate.vblanks, "double buffering overlaps CPU and GPU");
        auto physics = PSyncWorkload{};
        physics.colliders = 200;
        physics.hostCalls[static_cast<int>(Mips::HostFunc::Physics_Raycast)] = 100;
        Require(EstimatePSync(physics).fps < EstimatePSync(PSyncWorkload{}).fps, "host raycasts scale with collider count");
        // Recorded ReviewDemo Game workload, 2026-10-06. A native-clock PS1
        // build measured ~9 fields with packed grading and ~7 without grading.
        // This guards against the former 1/4-field prediction, independently
        // of the formula's coefficients, and verifies both rendering paths.
        PSyncWorkload review;
        review.objects = 21; review.submittedTriangles = 908; review.drawnTriangles = 450;
        review.projectedVertices = 655; review.litFaces = 812; review.litVertices = 80;
        review.lights = review.pointLights = 1; review.dmaWords = 9600;
        review.gpuCycles = 185400; review.colorGradingPixels = 76800;
        const auto gradedReview = EstimatePSync(review);
        Require(gradedReview.vblanks >= 8 && gradedReview.vblanks <= 10,
                "graded ReviewDemo prediction differs from native-clock measurement by more than one field");
        review.colorGradingPixels = 0;
        const auto ungradedReview = EstimatePSync(review);
        Require(ungradedReview.vblanks >= 6 && ungradedReview.vblanks <= 8,
                "ungraded ReviewDemo prediction differs from native-clock measurement by more than one field");
        Require(gradedReview.vblanks > ungradedReview.vblanks,
                "serialized grading must reduce cadence for the same world workload");
        review.pointLights = 0;
        Require(EstimatePSync(review).cpuMs < ungradedReview.cpuMs,
                "directional and local light costs must differ");
        Require(EstimatePSync(PSyncWorkload{}).vblanks == 1, "light scenes must retain native refresh");
        PSyncCadence cadence;
        PSyncWorkload borderline;
        const double threeFields = 3.0 / PSyncEstimate::refreshHz;
        borderline.gpuCycles = PSyncEstimate::cpuHz * threeFields * 1.001;
        const auto four = EstimatePSync(borderline);
        borderline.gpuCycles = PSyncEstimate::cpuHz * threeFields * 0.999;
        const auto three = EstimatePSync(borderline);
        Require(four.vblanks == 4 && three.vblanks == 3, "borderline samples straddle three-field threshold");
        Require(cadence.Update(three, 0).vblanks == 3, "first sample is not forced to 15 FPS");
        Require(cadence.Update(four, 0.01).vblanks == 4, "heavier workload slows cadence immediately");
        for (int i = 1; i <= 120; ++i) {
            const auto stable = cadence.Update(i % 2 ? three : four, 0.01 + i * threeFields);
            Require(stable.vblanks == 4 && Near(stable.fps, four.fps) &&
                    Near(stable.frameSeconds, four.frameSeconds), "borderline alternation stays at four fields in pacing and display");
            Require(stable.gpuMs == (i % 2 ? three : four).gpuMs, "instantaneous workload diagnostics stay intact");
        }
        double recovery = 0.01 + 121 * threeFields;
        Require(cadence.Update(three, recovery).vblanks == 4, "one lighter frame does not speed up");
        Require(cadence.Update(three, recovery + 0.49).vblanks == 4, "recovery waits for sustained lighter workload");
        Require(cadence.Update(three, recovery + 0.5).vblanks == 3, "genuine improvement recovers after half a second");
        Require(cadence.Update(four, recovery + 0.51).vblanks == 4, "new heavy frame immediately cancels recovery");
        const auto two = gpuEstimate;
        cadence.Update(two, recovery + 0.52);
        cadence.Update(three, recovery + 0.7);
        Require(cadence.Update(two, recovery + 1.02).vblanks == 3, "recovery keeps the slowest requirement in its window");
        cadence.Reset();
        Require(cadence.Update(two, 0).vblanks == 2, "restart/toggle discards previous cadence");
        Require(cadence.Update(four, 1).vblanks == 4 && cadence.Update(two, 0).vblanks == 2,
                "clock discontinuity discards stale recovery");
        for (const auto& target : {gpuEstimate, synchronized178})
        for (const double editorRate : {120.0, 144.0, 240.0, 1000.0}) {
            PSyncClock clock;
            int ticks = 0;
            double gameTime = 0.0;
            double simulationTime = 0.0;
            const double interval = target.frameSeconds;
            for (int i = 0; i < int(editorRate * 60); ++i) {
                if (clock.Advance(1.0 / editorRate, interval)) {
                    ++ticks; gameTime += clock.DeltaTime();
                    Require(clock.SimulationDeltaTime() == PSyncSimulationDeltaTime(ticks == 1 ? 1 : target.vblanks),
                            "Update delta follows the PS1 field count, independently of PC refresh");
                    simulationTime += clock.SimulationDeltaTime();
                }
            }
            Require(std::abs(ticks / 60.0 - target.fps) < 0.025, "PS1 estimate drives pacing independently of editor refresh");
            Require(std::abs(gameTime - 60.0) < 0.08, "presentation clock preserves elapsed time");
            Require(Near(simulationTime, PSyncSimulationDeltaTime(1) +
                (ticks - 1) * double(PSyncSimulationDeltaTime(target.vblanks)), 1e-5),
                "logic advances exactly once per predicted frame");
            clock.Reset();
            Require(clock.SimulationDeltaTime() == 0.0f, "reset discards previous logic delta");
            Require(clock.Advance(0.0, interval), "toggle/resume/step reset advances immediately");
            Require(clock.SimulationDeltaTime() == PSyncSimulationDeltaTime(1), "first update matches PS1 startup");
        }
        PSyncClock slowPC;
        slowPC.Advance(0, gpuEstimate.frameSeconds);
        Require(slowPC.Advance(0.5, gpuEstimate.frameSeconds), "PC delay produces one game update");
        Require(slowPC.DeltaTime() >= 0.45, "PC delay consumed without replaying a backlog");
        Require(slowPC.SimulationDeltaTime() == PSyncSimulationDeltaTime(4), "PC stall uses PS1's four-field logic cap");
        Require(!slowPC.Advance(0, gpuEstimate.frameSeconds), "stall does not create duplicate game updates");
        GameInputBuffer<4> input;
        const bool pressed[4] = {false,true,false,false}, released[4] = {};
        input.Accumulate(pressed, {3,2}, 1);
        input.Accumulate(released, {-1,4}, -0.5f);
        Require(input.keyDown[1], "short press survives skipped frames");
        Require(input.mouseDelta == glm::vec2(2,6) && input.scrollDelta == 0.5f, "mouse and wheel accumulate");
        input.Clear();
        Require(!input.keyDown[1] && input.mouseDelta == glm::vec2(0) && input.scrollDelta == 0, "consume input exactly once");
        std::cout << "P-Sync workload, clipping, CPU/GPU overlap, VBlank pacing and input tests passed\n";
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
