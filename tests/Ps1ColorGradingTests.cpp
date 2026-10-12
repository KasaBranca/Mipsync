#include "ps1/Ps1ColorGrading.h"
#include "ps1/PSync.h"
#include <vector>
#include <iostream>
#include <stdexcept>
extern "C" {
#include "render.h"
#include "color_grading.h"
#include <psxgpu.h>
}
using MipsyncEngine::Ps1::ColorGrading;
using MipsyncEngine::Ps1::GradeRgb555;
static void Require(bool v, const char* message) { if (!v) throw std::runtime_error(message); }
static uint32_t* ordering;
static std::vector<uint16_t> framebuffer(512 * 64), expected;
static bool submittedWorld = false, submittedUI = false;
extern "C" void DrawOTag(const uint32_t* ot) {
    if (ot == ordering + PS1_RENDER_OT_LEN - 1) {
        Require(ordering[1] == 0xffffff, "UI was included in graded world submission");
        submittedWorld = true;
    } else {
        Require(ot == ordering + 1 && submittedWorld && framebuffer == expected,
                "UI submitted before world grading completed");
        framebuffer[0] = 0x1234; submittedUI = true;
    }
}
extern "C" int DrawSync(int) { return 0; }
extern "C" void ps1_gpu_store_image(const RECT* r, uint32_t* output) {
    Require(submittedWorld && !submittedUI, "readback outside world/UI boundary");
    auto* p = reinterpret_cast<uint16_t*>(output);
    for (int y = 0; y < r->h; ++y)
        for (int x = 0; x < r->w; ++x) *p++ = framebuffer[(r->y + y) * 512 + r->x + x];
}
extern "C" int LoadImage(const RECT* r, const uint32_t* input) {
    const auto* p = reinterpret_cast<const uint16_t*>(input);
    for (int y = 0; y < r->h; ++y)
        for (int x = 0; x < r->w; ++x) framebuffer[(r->y + y) * 512 + r->x + x] = *p++;
    return 0;
}
int main() try {
    ColorGrading identity{true};
    Require(!MipsyncEngine::Ps1::NeedsColorGrading(identity),"neutral grading should bypass full-frame readback");
    for (unsigned p = 0; p < 32768; ++p)
        Require(GradeRgb555(p, identity) == p, "identity grading altered RGB555");
    ColorGrading gray{true, 0, 1, 0};
    const auto redGray = GradeRgb555(31, gray);
    Require((redGray & 31) == ((redGray >> 5) & 31) &&
            (redGray & 31) == ((redGray >> 10) & 31), "saturation zero did not produce neutral gray");
    ColorGrading tint{true, 0, 1, 1, {0,1,0}};
    Require(GradeRgb555(0x7fff, tint) == (31 << 5), "color filter not applied");
    ColorGrading settings{true, -.202f, 1.576f, .7f, {.8f,.9f,1.f}};
    Require(MipsyncEngine::Ps1::NeedsColorGrading(settings),"authored grading incorrectly bypassed");
    const auto transform=MipsyncEngine::Ps1::BuildColorGradingTransform(settings);
    std::vector<uint16_t> lut(32768), pixels(65536);
    for (unsigned p = 0; p < 32768; ++p) {
        lut[p] = GradeRgb555(p, settings);
        // Independently express the shader's affine RGB transform.
        double input[3], luma = 0;
        const double weights[3] = {.2126,.7152,.0722};
        for (int c=0;c<3;++c) { input[c]=(((p>>(5*c))&31)/31.0*std::pow(2.,settings.exposure)-.5)*settings.contrast+.5; luma+=weights[c]*input[c]; }
        uint16_t ref=0;
        for(int c=0;c<3;++c) {
            const double original=((1-settings.saturation)*luma+settings.saturation*input[c])*settings.filter[c];
            double affine=transform.offset[c];
            for (int channel=0;channel<3;++channel) affine+=transform.matrix[c*3+channel]*(((p>>(5*channel))&31)/31.0);
            Require(std::abs(affine-original)<1e-7,"precomputed shader transform changes grading");
            ref |= uint16_t(std::lround(std::clamp(original,0.,1.)*31))<<(5*c);
        }
        Require(lut[p] == ref, "LUT differs from shader expression");
    }
    for (unsigned p = 0; p < 65536; ++p) pixels[p] = p;
    ps1_grade_pixels(pixels.data(), pixels.size(), lut.data());
    for (unsigned p = 0; p < 65536; ++p)
        Require(pixels[p] == (lut[p & 0x7fff] | (p & 0x8000)), "runtime LUT lookup or mask preservation wrong");
    const auto unchanged = pixels;
    ps1_grade_pixels(pixels.data(), pixels.size(), nullptr);
    Require(pixels == unchanged, "disabled grading changed pixels");
    for (int offset=0;offset<2;++offset) for (unsigned count=0;count<35;++count) {
        std::vector<uint16_t> edge(count+offset+2,0x4321);
        auto original=edge;
        for (unsigned i=0;i<count;++i) edge[offset+i]=original[offset+i]=uint16_t(i*3217);
        ps1_grade_pixels(edge.data()+offset,count,lut.data());
        for (unsigned i=0;i<edge.size();++i) {
            const bool included=i>=unsigned(offset) && i<unsigned(offset)+count;
            const uint16_t expectedPixel=included ? (lut[original[i]&0x7fff] | (original[i]&0x8000)) : original[i];
            Require(edge[i]==expectedPixel,"packed grading mishandled odd count/alignment or wrote past range");
        }
    }
    for (unsigned i=0;i<framebuffer.size();++i) framebuffer[i]=i*13;
    expected=framebuffer;
    for(int y=15;y<28;++y) for(int x=20;x<341;++x)
        expected[y*512+x]=lut[framebuffer[y*512+x]&0x7fff] | (framebuffer[y*512+x]&0x8000);
    std::vector<uint32_t> ot(PS1_RENDER_OT_LEN, 0); ordering=ot.data(); ordering[1]=0x654321;
    ps1_render_submit(ordering,20,15,321,13,lut.data());
    Require(submittedUI && framebuffer[0] == 0x1234 && ordering[1] == 0x654321,
            "UI color or ordering tag not preserved");
    std::cout << "PS1 RGB555 grading, strip DMA, mask bits and world/UI ordering passed\n";
    return 0;
} catch(const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
