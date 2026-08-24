#include "BootSplash.h"
#include "Window.h"
#include "../editor/EditorTheme.h"
#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>
#include <GLFW/glfw3.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

// Mipsync Engine boot animation.
//
// The visual stack intentionally mirrors the portfolio background:
// LinearGradient -> SmokeFlow -> FlowField -> BrightnessContrast ->
// FilmGrain -> Ascii. The web version receives its SmokeFlow input from the
// pointer. Here, four scheduled emitters raster-trace the Mipsync icon instead.

namespace MipsyncEngine {
namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kDuration = 5.2f;
constexpr int kGridWidth = 72;
constexpr int kGridHeight = 54;

// Values copied from C:/Users/Owner/Documents/portfolio/app.js.
struct PortfolioShaderParams {
    // SmokeFlow
    float intensity = 0.72f;
    float emitRadius = 0.055f;
    float momentum = 42.0f;
    float dissipation = 0.34f;
    float detail = 34.0f;
    float gravity = -0.12f;
    float colorDecay = 0.32f;

    // FlowField
    float flowStrength = 0.035f;
    float flowDetail = 1.45f;
    float flowSpeed = 0.18f;
    float evolutionSpeed = 0.12f;
    float flowSeed = 19.0f;

    // BrightnessContrast / FilmGrain / Ascii
    float brightness = -0.18f;
    float contrast = 0.08f;
    float grainStrength = 0.035f;
    float asciiCellSize = 14.0f;
    float asciiSpacing = 0.82f;
    float asciiGamma = 0.82f;
};

constexpr PortfolioShaderParams kShader{};

struct SmokeCell {
    float density = 0.0f;
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
};

struct TrailPoint {
    float x = 0.0f;
    float y = 0.0f;
};

struct TraceStroke {
    std::vector<TrailPoint> points;
    ImVec4 color;
    float start = 0.0f;
    float duration = 1.0f;
};

float Clamp01(float x) {
    return std::max(0.0f, std::min(1.0f, x));
}

float EaseInOutCubic(float t) {
    t = Clamp01(t);
    return t < 0.5f
        ? 4.0f * t * t * t
        : 1.0f - std::pow(-2.0f * t + 2.0f, 3.0f) * 0.5f;
}

float EaseOutCubic(float t) {
    t = Clamp01(t);
    const float u = 1.0f - t;
    return 1.0f - u * u * u;
}

float MirrorCoordinate(float value, float maximum) {
    if (maximum <= 0.0f) return 0.0f;
    const float period = maximum * 2.0f;
    value = std::fmod(value, period);
    if (value < 0.0f) value += period;
    return value > maximum ? period - value : value;
}

uint32_t Hash(uint32_t value) {
    value ^= value >> 16;
    value *= 0x7feb352du;
    value ^= value >> 15;
    value *= 0x846ca68bu;
    value ^= value >> 16;
    return value;
}

float Hash01(uint32_t value) {
    return static_cast<float>(Hash(value) & 0x00ffffffu) /
           static_cast<float>(0x01000000u);
}

void CenterWindow(GLFWwindow* window) {
    if (!window) return;
    GLFWmonitor* monitor = glfwGetPrimaryMonitor();
    if (!monitor) return;
    const GLFWvidmode* mode = glfwGetVideoMode(monitor);
    if (!mode) return;
    int width = 0;
    int height = 0;
    glfwGetWindowSize(window, &width, &height);
    glfwSetWindowPos(window, (mode->width - width) / 2,
                     (mode->height - height) / 2);
}

std::vector<TrailPoint> MakeRasterPath(float minX, float minY,
                                       float maxX, float maxY,
                                       int rows) {
    std::vector<TrailPoint> points;
    points.reserve(static_cast<size_t>(rows) * 2u);
    for (int row = 0; row < rows; ++row) {
        const float u = rows <= 1
            ? 0.0f
            : static_cast<float>(row) / static_cast<float>(rows - 1);
        const float y = minY + (maxY - minY) * u;
        if ((row & 1) == 0) {
            points.push_back({minX, y});
            points.push_back({maxX, y});
        } else {
            points.push_back({maxX, y});
            points.push_back({minX, y});
        }
    }
    return points;
}

std::vector<TraceStroke> BuildIconStrokes() {
    // Geometry follows resources/icons/app_icon.png. Separate scheduled
    // rectangles preserve the pixel-built silhouette while SmokeFlow connects
    // each scan line into a continuous fluid trail.
    return {
        {MakeRasterPath(-0.58f, -0.58f, -0.10f, -0.35f, 5),
         ImVec4(0.992f, 0.773f, 0.106f, 1.0f), 0.30f, 0.70f},
        {MakeRasterPath(-0.58f, -0.35f, -0.35f,  0.58f, 13),
         ImVec4(0.992f, 0.773f, 0.106f, 1.0f), 0.78f, 1.18f},
        {MakeRasterPath(-0.08f, -0.35f,  0.18f,  0.58f, 13),
         ImVec4(0.976f, 0.067f, 0.118f, 1.0f), 1.02f, 1.28f},
        {MakeRasterPath( 0.20f, -0.58f,  0.46f, -0.35f, 5),
         ImVec4(0.118f, 0.694f, 0.635f, 1.0f), 1.42f, 0.72f},
        {MakeRasterPath( 0.48f, -0.58f,  0.72f,  0.58f, 15),
         ImVec4(0.000f, 0.521f, 0.733f, 1.0f), 1.58f, 1.42f},
    };
}

TrailPoint SampleStroke(const TraceStroke& stroke, float progress) {
    if (stroke.points.empty()) return {};
    if (stroke.points.size() == 1) return stroke.points.front();
    const float scaled = Clamp01(progress) *
                         static_cast<float>(stroke.points.size() - 1);
    const size_t index = std::min(
        static_cast<size_t>(scaled), stroke.points.size() - 2);
    const float local = scaled - static_cast<float>(index);
    const TrailPoint& a = stroke.points[index];
    const TrailPoint& b = stroke.points[index + 1];
    return {a.x + (b.x - a.x) * local, a.y + (b.y - a.y) * local};
}

class SmokeField {
public:
    SmokeField()
        : m_Current(static_cast<size_t>(kGridWidth * kGridHeight)),
          m_Next(static_cast<size_t>(kGridWidth * kGridHeight)) {}

    void Step(float dt, float time) {
        dt = std::min(dt, 0.05f);
        const float densityDecay = std::exp(-dt * (0.42f + kShader.dissipation));
        const float colorDecay = std::exp(-dt * kShader.colorDecay);

        for (int y = 0; y < kGridHeight; ++y) {
            for (int x = 0; x < kGridWidth; ++x) {
                const float u = (static_cast<float>(x) + 0.5f) /
                                static_cast<float>(kGridWidth);
                const float v = (static_cast<float>(y) + 0.5f) /
                                static_cast<float>(kGridHeight);
                const ImVec2 flow = FlowAt(u, v, time);

                const float flowScale = kShader.momentum * 0.012f;
                float sourceX = static_cast<float>(x) - flow.x * flowScale * dt *
                                static_cast<float>(kGridWidth);
                float sourceY = static_cast<float>(y) -
                                (flow.y * flowScale + kShader.gravity * 0.018f) *
                                dt * static_cast<float>(kGridHeight);
                sourceX = MirrorCoordinate(sourceX, static_cast<float>(kGridWidth - 1));
                sourceY = MirrorCoordinate(sourceY, static_cast<float>(kGridHeight - 1));

                SmokeCell sampled = Sample(sourceX, sourceY);
                sampled.density *= densityDecay;
                sampled.r *= colorDecay;
                sampled.g *= colorDecay;
                sampled.b *= colorDecay;
                m_Next[static_cast<size_t>(y * kGridWidth + x)] = sampled;
            }
        }
        m_Current.swap(m_Next);
    }

    void Emit(const TrailPoint& previous, const TrailPoint& current,
              const ImVec4& color, float amount) {
        const float dx = current.x - previous.x;
        const float dy = current.y - previous.y;
        const int samples = std::max(2, static_cast<int>(
            std::sqrt(dx * dx + dy * dy) * 95.0f));
        for (int i = 0; i <= samples; ++i) {
            const float t = static_cast<float>(i) / static_cast<float>(samples);
            Inject(previous.x + dx * t, previous.y + dy * t, color, amount);
        }
    }

    const std::vector<SmokeCell>& Cells() const { return m_Current; }

private:
    ImVec2 FlowAt(float u, float v, float time) const {
        const float px = (u - 0.5f) * kShader.flowDetail;
        const float py = (v - 0.5f) * kShader.flowDetail;
        const float evolution = time * kShader.evolutionSpeed;
        const float seed = kShader.flowSeed * 0.071f;
        const float a = std::sin((px * 3.1f + py * 2.3f + seed + evolution) * kPi);
        const float b = std::cos((py * 3.7f - px * 1.9f + seed * 1.7f - evolution) * kPi);
        const float angle = (a + b) * kPi + time * kShader.flowSpeed;
        return {std::cos(angle) * kShader.flowStrength,
                std::sin(angle) * kShader.flowStrength};
    }

    SmokeCell Sample(float x, float y) const {
        const int x0 = std::clamp(static_cast<int>(std::floor(x)), 0, kGridWidth - 1);
        const int y0 = std::clamp(static_cast<int>(std::floor(y)), 0, kGridHeight - 1);
        const int x1 = std::min(x0 + 1, kGridWidth - 1);
        const int y1 = std::min(y0 + 1, kGridHeight - 1);
        const float tx = x - std::floor(x);
        const float ty = y - std::floor(y);
        const SmokeCell& a = m_Current[static_cast<size_t>(y0 * kGridWidth + x0)];
        const SmokeCell& b = m_Current[static_cast<size_t>(y0 * kGridWidth + x1)];
        const SmokeCell& c = m_Current[static_cast<size_t>(y1 * kGridWidth + x0)];
        const SmokeCell& d = m_Current[static_cast<size_t>(y1 * kGridWidth + x1)];
        auto bilerp = [&](float av, float bv, float cv, float dv) {
            const float top = av + (bv - av) * tx;
            const float bottom = cv + (dv - cv) * tx;
            return top + (bottom - top) * ty;
        };
        return {bilerp(a.density, b.density, c.density, d.density),
                bilerp(a.r, b.r, c.r, d.r),
                bilerp(a.g, b.g, c.g, d.g),
                bilerp(a.b, b.b, c.b, d.b)};
    }

    void Inject(float normalizedX, float normalizedY, const ImVec4& color,
                float amount) {
        const float centerX = (normalizedX * 0.5f + 0.5f) *
                              static_cast<float>(kGridWidth - 1);
        const float centerY = (normalizedY * 0.5f + 0.5f) *
                              static_cast<float>(kGridHeight - 1);
        const float radius = kShader.emitRadius *
                             static_cast<float>(std::min(kGridWidth, kGridHeight));
        const int reach = std::max(2, static_cast<int>(std::ceil(radius * 2.0f)));
        const int minX = std::max(0, static_cast<int>(centerX) - reach);
        const int maxX = std::min(kGridWidth - 1, static_cast<int>(centerX) + reach);
        const int minY = std::max(0, static_cast<int>(centerY) - reach);
        const int maxY = std::min(kGridHeight - 1, static_cast<int>(centerY) + reach);
        const float invRadius = 1.0f / std::max(radius, 0.001f);

        for (int y = minY; y <= maxY; ++y) {
            for (int x = minX; x <= maxX; ++x) {
                const float nx = (static_cast<float>(x) - centerX) * invRadius;
                const float ny = (static_cast<float>(y) - centerY) * invRadius;
                const float distance2 = nx * nx + ny * ny;
                if (distance2 >= 4.0f) continue;
                const float weight = std::exp(-distance2 * 1.65f) *
                                     amount * kShader.intensity;
                SmokeCell& cell = m_Current[static_cast<size_t>(y * kGridWidth + x)];
                const float previousDensity = cell.density;
                const float combined = std::min(3.0f, previousDensity + weight);
                if (combined > 0.0001f) {
                    cell.r = (cell.r * previousDensity + color.x * weight) /
                             (previousDensity + weight);
                    cell.g = (cell.g * previousDensity + color.y * weight) /
                             (previousDensity + weight);
                    cell.b = (cell.b * previousDensity + color.z * weight) /
                             (previousDensity + weight);
                }
                cell.density = combined;
            }
        }
    }

    std::vector<SmokeCell> m_Current;
    std::vector<SmokeCell> m_Next;
};

void DrawLinearGradient(ImDrawList* drawList, const ImVec2& min,
                        const ImVec2& max) {
    // Portfolio LinearGradient: #111113 -> #030304, start (0,1), end (1,0).
    const ImU32 colorA = IM_COL32(17, 17, 19, 255);
    const ImU32 colorB = IM_COL32(3, 3, 4, 255);
    const ImU32 middle = IM_COL32(10, 10, 12, 255);
    drawList->AddRectFilledMultiColor(min, max, middle, colorB, colorA, middle);
}

void DrawSmokeAscii(ImDrawList* drawList, ImFont* font,
                    const ImVec2& min, const ImVec2& max,
                    const SmokeField& field, float master, uint32_t frame) {
    if (!font || master <= 0.001f) return;
    constexpr const char* characters = "@W8M#*+=-:. ";
    constexpr int characterCount = 12;
    const float cellWidth = (max.x - min.x) / static_cast<float>(kGridWidth);
    const float cellHeight = (max.y - min.y) / static_cast<float>(kGridHeight);
    const float fontSize = std::min(kShader.asciiCellSize,
                                    cellHeight / kShader.asciiSpacing);
    const auto& cells = field.Cells();

    for (int y = 0; y < kGridHeight; ++y) {
        for (int x = 0; x < kGridWidth; ++x) {
            const SmokeCell& cell = cells[static_cast<size_t>(y * kGridWidth + x)];
            float luminance = std::max(0.0f, cell.density + kShader.brightness);
            luminance = Clamp01((luminance - 0.5f) * (1.0f + kShader.contrast) + 0.5f);
            luminance = std::pow(luminance, kShader.asciiGamma);
            if (luminance < 0.035f) continue;

            const float grain = (Hash01(frame * 73856093u +
                                        static_cast<uint32_t>(x) * 19349663u +
                                        static_cast<uint32_t>(y) * 83492791u) - 0.5f) *
                                kShader.grainStrength;
            luminance = Clamp01(luminance + grain);
            const int charIndex = std::clamp(
                static_cast<int>((1.0f - luminance) *
                                 static_cast<float>(characterCount - 1)),
                0, characterCount - 1);
            if (characters[charIndex] == ' ') continue;

            char glyph[2] = {characters[charIndex], '\0'};
            const float alpha = Clamp01(luminance * 1.2f) * master;
            const int red = static_cast<int>(Clamp01(cell.r * (0.62f + luminance * 0.60f)) * 255.0f);
            const int green = static_cast<int>(Clamp01(cell.g * (0.62f + luminance * 0.60f)) * 255.0f);
            const int blue = static_cast<int>(Clamp01(cell.b * (0.62f + luminance * 0.60f)) * 255.0f);
            const int a = static_cast<int>(alpha * 255.0f);
            const ImVec2 position = {
                min.x + static_cast<float>(x) * cellWidth,
                min.y + static_cast<float>(y) * cellHeight
            };

            // A faint fluid body behind the glyph prevents the trace from
            // becoming a disconnected dot matrix while keeping the Ascii pass.
            drawList->AddRectFilled(
                position,
                {position.x + cellWidth * 1.08f, position.y + cellHeight * 1.08f},
                IM_COL32(red, green, blue, static_cast<int>(a * 0.22f)), 2.0f);
            drawList->AddText(font, fontSize, position,
                              IM_COL32(red, green, blue, a), glyph);
        }
    }
}

void DrawFilmGrain(ImDrawList* drawList, const ImVec2& min, const ImVec2& max,
                   uint32_t frame, float master) {
    if (master <= 0.001f) return;
    constexpr int grainCount = 210;
    for (int i = 0; i < grainCount; ++i) {
        const uint32_t seed = Hash(frame * 2654435761u + static_cast<uint32_t>(i));
        const float x = min.x + Hash01(seed) * (max.x - min.x);
        const float y = min.y + Hash01(seed ^ 0x9e3779b9u) * (max.y - min.y);
        const int alpha = static_cast<int>(255.0f * kShader.grainStrength * master *
                                           (0.3f + Hash01(seed ^ 0x85ebca6bu) * 0.7f));
        drawList->AddRectFilled({x, y}, {x + 1.0f, y + 1.0f},
                                IM_COL32(255, 255, 255, alpha));
    }
}

void DrawBootFrame(ImDrawList* drawList, ImFont* font,
                   const ImVec2& min, const ImVec2& max,
                   SmokeField& smoke, const std::vector<TraceStroke>& strokes,
                   std::vector<TrailPoint>& previousPoints,
                   std::vector<bool>& hasPrevious, float time, float dt,
                   uint32_t frame) {
    const float fadeIn = EaseOutCubic(time / 0.35f);
    const float fadeOut = 1.0f - EaseInOutCubic((time - (kDuration - 0.75f)) / 0.75f);
    const float master = fadeIn * fadeOut;

    DrawLinearGradient(drawList, min, max);
    smoke.Step(dt, time);

    for (size_t i = 0; i < strokes.size(); ++i) {
        const TraceStroke& stroke = strokes[i];
        const float progress = (time - stroke.start) / stroke.duration;
        if (progress < 0.0f || progress > 1.0f) continue;
        const TrailPoint point = SampleStroke(stroke, EaseInOutCubic(progress));
        const TrailPoint previous = hasPrevious[i] ? previousPoints[i] : point;
        smoke.Emit(previous, point, stroke.color, 0.62f);
        previousPoints[i] = point;
        hasPrevious[i] = true;

        const ImVec2 head = {
            min.x + (point.x * 0.5f + 0.5f) * (max.x - min.x),
            min.y + (point.y * 0.5f + 0.5f) * (max.y - min.y)
        };
        const float pulse = 5.0f + std::sin(time * 14.0f + static_cast<float>(i)) * 1.3f;
        drawList->AddCircleFilled(
            head, pulse,
            IM_COL32(static_cast<int>(stroke.color.x * 255.0f),
                     static_cast<int>(stroke.color.y * 255.0f),
                     static_cast<int>(stroke.color.z * 255.0f),
                     static_cast<int>(master * 210.0f)));
    }

    DrawSmokeAscii(drawList, font, min, max, smoke, master, frame);
    DrawFilmGrain(drawList, min, max, frame, master);
}

} // namespace

void BootSplash::Play() {
    WindowProps props;
    props.title = "Mipsync Engine";
    props.width = 720;
    props.height = 540;
    props.maximized = false;
    props.vsync = true;

    Window window(props);
    GLFWwindow* native = window.GetNativeWindow();
    if (!native) return;

    glfwSetWindowAttrib(native, GLFW_DECORATED, GLFW_FALSE);
    glfwSetWindowAttrib(native, GLFW_RESIZABLE, GLFW_FALSE);
    CenterWindow(native);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    EditorTheme::Apply();
    EditorTheme::LoadFonts();

    ImGui_ImplGlfw_InitForOpenGL(native, true);
    ImGui_ImplOpenGL3_Init("#version 330");

    const ImVec2 viewportMin{0.0f, 0.0f};
    const ImVec2 viewportMax{static_cast<float>(props.width),
                             static_cast<float>(props.height)};
    SmokeField smoke;
    const std::vector<TraceStroke> strokes = BuildIconStrokes();
    std::vector<TrailPoint> previousPoints(strokes.size());
    std::vector<bool> hasPrevious(strokes.size(), false);

    const double startTime = glfwGetTime();
    double lastTime = startTime;
    uint32_t frame = 0;

    while (!window.ShouldClose()) {
        glfwPollEvents();
        const double now = glfwGetTime();
        const float dt = static_cast<float>(std::min(now - lastTime, 0.05));
        lastTime = now;
        const float time = static_cast<float>(now - startTime);

        if (time >= kDuration) break;

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        ImGui::SetNextWindowPos(viewportMin);
        ImGui::SetNextWindowSize(viewportMax);
        ImGui::Begin("##BootSplash", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs |
                     ImGuiWindowFlags_NoBackground |
                     ImGuiWindowFlags_NoBringToFrontOnFocus);

        ImDrawList* drawList = ImGui::GetWindowDrawList();
        DrawBootFrame(drawList, ImGui::GetFont(), viewportMin, viewportMax,
                      smoke, strokes, previousPoints, hasPrevious,
                      time, dt, frame++);
        ImGui::End();
        ImGui::Render();

        glViewport(0, 0, props.width, props.height);
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        window.OnUpdate();
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
}

} // namespace MipsyncEngine
