#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
namespace MipsyncEngine::Ps1 {
struct ColorGrading {
    bool enabled = false;
    float exposure = 0, contrast = 1, saturation = 1;
    std::array<float, 3> filter{1, 1, 1};
};
inline bool NeedsColorGrading(const ColorGrading& settings) {
    return settings.enabled && (settings.exposure!=0 || settings.contrast!=1 ||
        settings.saturation!=1 || settings.filter!=std::array<float,3>{1,1,1});
}
struct ColorGradingTransform {
    std::array<double,9> matrix{1,0,0,0,1,0,0,0,1};
    std::array<double,3> offset{};
};
inline ColorGradingTransform BuildColorGradingTransform(const ColorGrading& settings) {
    ColorGradingTransform result;
    if (!settings.enabled) return result;
    const double scale=std::exp2(settings.exposure)*std::max(0.f,settings.contrast);
    const double saturation=std::max(0.f,settings.saturation);
    constexpr double luma[3]={.2126,.7152,.0722};
    for (int row=0; row<3; ++row) {
        result.offset[row]=(.5-.5*std::max(0.f,settings.contrast))*settings.filter[row];
        for (int col=0; col<3; ++col)
            result.matrix[row*3+col]=scale*settings.filter[row]*
                ((1-saturation)*luma[col]+(row==col ? saturation : 0));
    }
    return result;
}
// Same operation order as Renderer.cpp, applied to the final lit/fogged
// RGB555 framebuffer rather than to textures before lighting.
inline uint16_t GradeRgb555(uint16_t pixel, const ColorGrading& settings) {
    if (!settings.enabled) return pixel & 0x7fff;
    double color[3];
    const double exposure = std::exp2(settings.exposure);
    for (int c = 0; c < 3; ++c)
        color[c] = (((pixel >> (c * 5)) & 31) / 31.0 * exposure - .5) *
            std::max(0.f, settings.contrast) + .5;
    const double luma = color[0] * .2126 + color[1] * .7152 + color[2] * .0722;
    uint16_t result = 0;
    for (int c = 0; c < 3; ++c) {
        const double graded = (luma + (color[c] - luma) * std::max(0.f, settings.saturation)) * settings.filter[c];
        result |= static_cast<uint16_t>(std::lround(std::clamp(graded, 0.0, 1.0) * 31)) << (c * 5);
    }
    return result;
}
} // namespace MipsyncEngine::Ps1
