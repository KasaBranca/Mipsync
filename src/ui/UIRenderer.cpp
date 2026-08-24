#include "UIRenderer.h"
#include "RectTransform.h"
#include "UICanvasLayout.h"
#include "UILayout.h"
#include "../scene/Scene.h"
#include "../renderer/Renderer.h"
#include "../assets/AssetManager.h"
#include "../core/Log.h"
#include "../core/Engine.h"
#include "../core/Input.h"
#include "../audio/AudioSystem.h"
#include <imgui.h>
#include <imgui_internal.h>
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <unordered_map>

namespace MipsyncEngine {

namespace {

static const char* UI_VERTEX_SHADER = R"(
#version 330 core
layout(location = 0) in vec3 aPosition;
layout(location = 2) in vec2 aUV;
out vec2 vUV;
uniform mat4 uMVP;
void main() {
    vUV = aUV;
    gl_Position = uMVP * vec4(aPosition, 1.0);
}
)";

static const char* UI_FRAGMENT_SHADER = R"(
#version 330 core
in vec2 vUV;
out vec4 fragColor;
uniform vec4 uColor;
uniform sampler2D uTexture;
uniform int uUseTexture;
void main() {
    vec4 c = uUseTexture != 0 ? texture(uTexture, vUV) * uColor : uColor;
    if (c.a < 0.004)
        discard;
    fragColor = c;
}
)";

bool CanvasUsesCamera(const CanvasComponent& canvas, uint32_t activeCameraEntityId, Scene& scene) {
    if (canvas.renderMode == UICanvasRenderMode::ScreenSpaceOverlay)
        return true;
    if (canvas.renderMode == UICanvasRenderMode::WorldSpace)
        return true;
    if (canvas.eventCameraEntityId == 0)
        return true;
    if (activeCameraEntityId == 0) {
        if (Entity* primary = scene.GetPrimaryCameraEntity())
            return canvas.eventCameraEntityId == primary->GetID();
        return true;
    }
    return canvas.eventCameraEntityId == activeCameraEntityId;
}

const uint8_t* Ps1FontRows(char character) {
    static const uint8_t glyphs[36][7] = {
        { 0x0e, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0e },
        { 0x04, 0x0c, 0x04, 0x04, 0x04, 0x04, 0x0e },
        { 0x0e, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1f },
        { 0x1e, 0x01, 0x01, 0x0e, 0x01, 0x01, 0x1e },
        { 0x02, 0x06, 0x0a, 0x12, 0x1f, 0x02, 0x02 },
        { 0x1f, 0x10, 0x10, 0x1e, 0x01, 0x01, 0x1e },
        { 0x0e, 0x10, 0x10, 0x1e, 0x11, 0x11, 0x0e },
        { 0x1f, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08 },
        { 0x0e, 0x11, 0x11, 0x0e, 0x11, 0x11, 0x0e },
        { 0x0e, 0x11, 0x11, 0x0f, 0x01, 0x01, 0x0e },
        { 0x0e, 0x11, 0x11, 0x1f, 0x11, 0x11, 0x11 },
        { 0x1e, 0x11, 0x11, 0x1e, 0x11, 0x11, 0x1e },
        { 0x0e, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0e },
        { 0x1e, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1e },
        { 0x1f, 0x10, 0x10, 0x1e, 0x10, 0x10, 0x1f },
        { 0x1f, 0x10, 0x10, 0x1e, 0x10, 0x10, 0x10 },
        { 0x0e, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0f },
        { 0x11, 0x11, 0x11, 0x1f, 0x11, 0x11, 0x11 },
        { 0x0e, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0e },
        { 0x07, 0x02, 0x02, 0x02, 0x12, 0x12, 0x0c },
        { 0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11 },
        { 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1f },
        { 0x11, 0x1b, 0x15, 0x15, 0x11, 0x11, 0x11 },
        { 0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11 },
        { 0x0e, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0e },
        { 0x1e, 0x11, 0x11, 0x1e, 0x10, 0x10, 0x10 },
        { 0x0e, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0d },
        { 0x1e, 0x11, 0x11, 0x1e, 0x14, 0x12, 0x11 },
        { 0x0f, 0x10, 0x10, 0x0e, 0x01, 0x01, 0x1e },
        { 0x1f, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04 },
        { 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0e },
        { 0x11, 0x11, 0x11, 0x11, 0x11, 0x0a, 0x04 },
        { 0x11, 0x11, 0x11, 0x15, 0x15, 0x1b, 0x11 },
        { 0x11, 0x11, 0x0a, 0x04, 0x0a, 0x11, 0x11 },
        { 0x11, 0x11, 0x0a, 0x04, 0x04, 0x04, 0x04 },
        { 0x1f, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1f },
    };
    static const uint8_t exclamation[7] = { 0x04, 0x04, 0x04, 0x04, 0x04, 0x00, 0x04 };
    static const uint8_t question[7] = { 0x0e, 0x11, 0x01, 0x02, 0x04, 0x00, 0x04 };
    static const uint8_t dash[7] = { 0x00, 0x00, 0x00, 0x1f, 0x00, 0x00, 0x00 };
    static const uint8_t dot[7] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x0c, 0x0c };
    static const uint8_t colon[7] = { 0x00, 0x0c, 0x0c, 0x00, 0x0c, 0x0c, 0x00 };
    static const uint8_t greater[7] = { 0x10, 0x08, 0x04, 0x02, 0x04, 0x08, 0x10 };
    static const uint8_t less[7] = { 0x01, 0x02, 0x04, 0x08, 0x04, 0x02, 0x01 };

    if (character >= 'a' && character <= 'z')
        character = static_cast<char>(character - 'a' + 'A');
    if (character >= '0' && character <= '9')
        return glyphs[character - '0'];
    if (character >= 'A' && character <= 'Z')
        return glyphs[10 + character - 'A'];
    if (character == '!') return exclamation;
    if (character == '?') return question;
    if (character == '-' || character == '_') return dash;
    if (character == '.') return dot;
    if (character == ':') return colon;
    if (character == '>') return greater;
    if (character == '<') return less;
    return nullptr;
}

int Ps1FontScale(float fontSize, float layoutHeight) {
    const float safeLayoutHeight = std::max(layoutHeight, 1.0f);
    const int ps1FontSize = std::clamp(static_cast<int>(std::round(fontSize * 240.0f / safeLayoutHeight)), 4, 64);
    return std::max(ps1FontSize / 8, 1);
}

bool ContainsNonAscii(const std::string& text) {
    for (unsigned char c : text) {
        if (c >= 0x80)
            return true;
    }
    return false;
}

int ConfirmKeyToGlfw(UIConfirmKey key) {
    switch (key) {
    case UIConfirmKey::Space: return GLFW_KEY_SPACE;
    case UIConfirmKey::Z: return GLFW_KEY_Z;
    case UIConfirmKey::X: return GLFW_KEY_X;
    case UIConfirmKey::E: return GLFW_KEY_E;
    case UIConfirmKey::F: return GLFW_KEY_F;
    case UIConfirmKey::Enter:
    default:
        return GLFW_KEY_ENTER;
    }
}

int ConfirmButtonToGlfw(UIConfirmGamepadButton button) {
    switch (button) {
    case UIConfirmGamepadButton::East: return GLFW_GAMEPAD_BUTTON_B;
    case UIConfirmGamepadButton::West: return GLFW_GAMEPAD_BUTTON_X;
    case UIConfirmGamepadButton::North: return GLFW_GAMEPAD_BUTTON_Y;
    case UIConfirmGamepadButton::Start: return GLFW_GAMEPAD_BUTTON_START;
    case UIConfirmGamepadButton::South:
    default:
        return GLFW_GAMEPAD_BUTTON_A;
    }
}

struct GamepadEdgeState {
    bool up = false;
    bool down = false;
    bool confirm = false;
};

std::unordered_map<uint32_t, GamepadEdgeState> s_GamepadPrev;

bool ReadGamepadButton(int button) {
    GLFWgamepadstate state{};
    if (glfwGetGamepadState(GLFW_JOYSTICK_1, &state) != GLFW_TRUE)
        return false;
    if (button < 0 || button >= GLFW_GAMEPAD_BUTTON_LAST + 1)
        return false;
    return state.buttons[button] == GLFW_PRESS;
}

bool ReadGamepadAxisDown() {
    GLFWgamepadstate state{};
    if (glfwGetGamepadState(GLFW_JOYSTICK_1, &state) != GLFW_TRUE)
        return false;
    return state.axes[GLFW_GAMEPAD_AXIS_LEFT_Y] > 0.65f;
}

bool ReadGamepadAxisUp() {
    GLFWgamepadstate state{};
    if (glfwGetGamepadState(GLFW_JOYSTICK_1, &state) != GLFW_TRUE)
        return false;
    return state.axes[GLFW_GAMEPAD_AXIS_LEFT_Y] < -0.65f;
}

bool ReadGamepadEdge(uint32_t groupId, int confirmButton, bool& outUp, bool& outDown,
                     bool& outConfirm) {
    const bool upNow = ReadGamepadButton(GLFW_GAMEPAD_BUTTON_DPAD_UP) || ReadGamepadAxisUp();
    const bool downNow = ReadGamepadButton(GLFW_GAMEPAD_BUTTON_DPAD_DOWN) || ReadGamepadAxisDown();
    const bool confirmNow = ReadGamepadButton(confirmButton);
    GamepadEdgeState& prev = s_GamepadPrev[groupId];
    outUp = upNow && !prev.up;
    outDown = downNow && !prev.down;
    outConfirm = confirmNow && !prev.confirm;
    prev = { upNow, downNow, confirmNow };
    return upNow || downNow || confirmNow || outUp || outDown || outConfirm;
}

Entity* FindButtonGroupAncestor(Scene& scene, Entity* entity) {
    Entity* walk = entity;
    while (walk) {
        if (walk->GetComponent<UIButtonGroupComponent>())
            return walk;
        walk = scene.FindEntity(walk->GetParentID());
    }
    return nullptr;
}

UIRect FitRectToAspect(const UIRect& rect, float aspect) {
    if (aspect <= 0.0001f || rect.Width() <= 0.0f || rect.Height() <= 0.0f)
        return rect;

    const float rectAspect = rect.Width() / std::max(rect.Height(), 0.0001f);
    UIRect out = rect;
    if (rectAspect > aspect) {
        const float width = rect.Height() * aspect;
        const float cx = rect.Center().x;
        out.minX = cx - width * 0.5f;
        out.maxX = cx + width * 0.5f;
    } else {
        const float height = rect.Width() / aspect;
        const float cy = rect.Center().y;
        out.minY = cy - height * 0.5f;
        out.maxY = cy + height * 0.5f;
    }
    return out;
}

} // namespace

UIRenderer::UIRenderer() = default;

UIRenderer::~UIRenderer() {
    Shutdown();
}

void UIRenderer::Init() {
    CreateShaders();
    m_Quad = Mesh::CreateScreenQuad();
    m_WhiteTexture = std::make_unique<Texture>(Texture::CreateSolid(255, 255, 255, 255));

    struct TextVertex {
        glm::vec3 pos;
        glm::vec2 uv;
    };
    glGenVertexArrays(1, &m_TextVAO);
    glGenBuffers(1, &m_TextVBO);
    glBindVertexArray(m_TextVAO);
    glBindBuffer(GL_ARRAY_BUFFER, m_TextVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(TextVertex) * 6, nullptr, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(TextVertex),
                          reinterpret_cast<void*>(offsetof(TextVertex, pos)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(TextVertex),
                          reinterpret_cast<void*>(offsetof(TextVertex, uv)));
    glBindVertexArray(0);
}

void UIRenderer::Shutdown() {
    if (m_TextVAO) {
        glDeleteVertexArrays(1, &m_TextVAO);
        m_TextVAO = 0;
    }
    if (m_TextVBO) {
        glDeleteBuffers(1, &m_TextVBO);
        m_TextVBO = 0;
    }
    m_UIShader = Shader();
    m_Quad = Mesh();
    m_WhiteTexture.reset();
}

void UIRenderer::CreateShaders() {
    m_UIShader = Shader(UI_VERTEX_SHADER, UI_FRAGMENT_SHADER, false);
    if (!m_UIShader.IsValid())
        MIPSYNC_ERROR("UI shader failed to compile");
}

void UIRenderer::BeginPass(int viewportWidth, int viewportHeight, Framebuffer* targetFBO) {
    m_ViewportWidth = std::max(viewportWidth, 1);
    m_ViewportHeight = std::max(viewportHeight, 1);
    m_InPass = true;

    m_ActiveFBO = targetFBO;
    if (targetFBO)
        targetFBO->Bind();
    glViewport(0, 0, m_ViewportWidth, m_ViewportHeight);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_CULL_FACE);
}

void UIRenderer::EndPass() {
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    if (m_ActiveFBO)
        m_ActiveFBO->Unbind();
    m_ActiveFBO = nullptr;
    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    m_InPass = false;
}

void UIRenderer::DrawSolidQuad(const glm::mat4& mvp, const glm::vec4& color, const Texture* texture) {
    if (!m_UIShader.IsValid())
        return;

    m_UIShader.Bind();
    m_UIShader.SetMat4("uMVP", mvp);
    m_UIShader.SetVec4("uColor", color);
    const bool useTex = texture != nullptr;
    m_UIShader.SetInt("uUseTexture", useTex ? 1 : 0);
    if (useTex) {
        texture->Bind(0);
        m_UIShader.SetInt("uTexture", 0);
    } else {
        m_WhiteTexture->Bind(0);
        m_UIShader.SetInt("uTexture", 0);
    }

    m_Quad.Bind();
    glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(m_Quad.GetIndexCount()), GL_UNSIGNED_INT, nullptr);
    m_Quad.Unbind();
    m_UIShader.Unbind();
}

void UIRenderer::DrawGlyphQuad(const glm::mat4& mvp, const glm::vec4& color, GLuint textureId,
                               const glm::vec3 corners[4], const glm::vec2 uvs[4]) {
    if (!m_UIShader.IsValid() || m_TextVAO == 0 || textureId == 0)
        return;

    struct TextVertex {
        glm::vec3 pos;
        glm::vec2 uv;
    };

    const TextVertex verts[6] = {
        { corners[0], uvs[0] }, { corners[1], uvs[1] }, { corners[2], uvs[2] },
        { corners[0], uvs[0] }, { corners[2], uvs[2] }, { corners[3], uvs[3] },
    };

    m_UIShader.Bind();
    m_UIShader.SetMat4("uMVP", mvp);
    m_UIShader.SetVec4("uColor", color);
    m_UIShader.SetInt("uUseTexture", 1);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, textureId);
    m_UIShader.SetInt("uTexture", 0);

    glBindVertexArray(m_TextVAO);
    glBindBuffer(GL_ARRAY_BUFFER, m_TextVBO);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(verts), verts);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);
    m_UIShader.Unbind();
}

void UIRenderer::DrawCanvasPlaneText(const char* text, float fontSize, const glm::vec4& color,
                                       const glm::mat4& canvasWorld, const glm::mat4& view,
                                       const glm::mat4& proj, float drawX, float layoutYTop) {
    if (!text || !*text || !m_UIShader.IsValid())
        return;

    ImFont* font = ImGui::GetFont();
    if (!font)
        return;

    ImFontBaked* baked = font->GetFontBaked(fontSize);
    if (!baked)
        return;

    const ImTextureID texId = ImGui::GetIO().Fonts->TexRef.GetTexID();
    if (texId == ImTextureID_Invalid)
        return;
    const GLuint fontTexture = static_cast<GLuint>(static_cast<size_t>(texId));

    const float scale = fontSize / baked->Size;
    const glm::mat4 planeMvp = proj * view * canvasWorld;
    constexpr float kTextPlaneEpsilon = 0.5f;
    const float z = kTextPlaneEpsilon;

    float x = drawX;
    const char* s = text;
    while (*s) {
        unsigned int c = 0;
        s += ImTextCharFromUtf8(&c, s, nullptr);
        if (c == 0)
            break;

        if (c < 32) {
            if (c == '\n')
                x = drawX;
            continue;
        }

        const ImFontGlyph* glyph = baked->FindGlyph(static_cast<ImWchar>(c));
        if (!glyph)
            continue;

        if (glyph->Visible) {
            const float lx1 = x + glyph->X0 * scale;
            const float lx2 = x + glyph->X1 * scale;
            const float ly1 = layoutYTop - glyph->Y0 * scale;
            const float ly2 = layoutYTop - glyph->Y1 * scale;

            const glm::vec3 corners[4] = {
                { lx1, ly1, z }, { lx2, ly1, z }, { lx2, ly2, z }, { lx1, ly2, z },
            };
            const glm::vec2 uvs[4] = {
                { glyph->U0, glyph->V0 }, { glyph->U1, glyph->V0 },
                { glyph->U1, glyph->V1 }, { glyph->U0, glyph->V1 },
            };
            DrawGlyphQuad(planeMvp, color, fontTexture, corners, uvs);
        }

        x += glyph->AdvanceX * scale;
    }
}

void UIRenderer::Render(Scene& scene, const Camera& camera, int viewportWidth, int viewportHeight,
                          Framebuffer* targetFBO, uint32_t activeCameraEntityId, bool sceneView3D,
                          int layoutWidth, int layoutHeight) {
    struct CanvasEntry {
        Entity* entity;
        CanvasComponent* canvas;
        int sortOrder;
    };

    std::vector<CanvasEntry> canvases;
    canvases.reserve(scene.GetEntities().size());

    for (auto& entityPtr : scene.GetEntities()) {
        auto* canvas = entityPtr->GetComponent<CanvasComponent>();
        if (!canvas || !canvas->enabled)
            continue;
        if (!CanvasUsesCamera(*canvas, activeCameraEntityId, scene))
            continue;
        canvases.push_back({ entityPtr.get(), canvas, canvas->sortOrder });
    }

    if (canvases.empty())
        return;

    std::sort(canvases.begin(), canvases.end(),
              [](const CanvasEntry& a, const CanvasEntry& b) { return a.sortOrder < b.sortOrder; });

    BeginPass(viewportWidth, viewportHeight, targetFBO);

    const float vpW = static_cast<float>(m_ViewportWidth);
    const float vpH = static_cast<float>(m_ViewportHeight);
    const int layoutW = layoutWidth > 0 ? layoutWidth : m_ViewportWidth;
    const int layoutH = layoutHeight > 0 ? layoutHeight : m_ViewportHeight;
    const float layoutWf = static_cast<float>(layoutW);
    const float layoutHf = static_cast<float>(layoutH);
    const glm::mat4 ortho = glm::ortho(0.0f, vpW, 0.0f, vpH);
    const glm::mat4 view = camera.GetViewMatrix();
    const glm::mat4 proj = camera.GetProjectionMatrix();

    auto buildQuadMvp = [&](const UIRect& rect, bool pixelSpace, const glm::mat4& canvasWorld,
                            Entity* entity = nullptr) {
        const glm::vec3 center = { rect.Center().x, rect.Center().y, 0.0f };
        float rotationZ = 0.0f;
        if (entity) {
            if (const auto* transform = entity->GetComponent<TransformComponent>())
                rotationZ = transform->rotation.z;
        }
        const glm::mat4 local = glm::translate(glm::mat4(1.0f), center) *
                                glm::rotate(glm::mat4(1.0f), glm::radians(rotationZ),
                                            glm::vec3(0.0f, 0.0f, 1.0f)) *
                                glm::scale(glm::mat4(1.0f),
                                           glm::vec3(rect.Width() * 0.5f, rect.Height() * 0.5f, 1.0f));
        if (pixelSpace)
            return ortho * local;
        return proj * view * canvasWorld * local;
    };

    auto drawScreenRect = [&](float x, float y, float width, float height, const glm::vec4& color) {
        if (width <= 0.0f || height <= 0.0f)
            return;
        const UIRect rect{ x, y, x + width, y + height };
        DrawSolidQuad(buildQuadMvp(rect, true, glm::mat4(1.0f)), color, nullptr);
    };

    auto drawPs1BitmapText = [&](const UIRect& rect, UITextAlignment alignment, const std::string& textStr,
                                 float fontSize, const glm::vec4& color) {
        if (textStr.empty())
            return;

        const float safeLayoutW = std::max(layoutWf, 1.0f);
        const float safeLayoutH = std::max(layoutHf, 1.0f);
        const float ps1ScaleX = vpW / 320.0f;
        const float ps1ScaleY = vpH / 240.0f;
        const int fontScale = Ps1FontScale(fontSize, safeLayoutH);
        const int ps1TextWidth = static_cast<int>(textStr.size()) * 6 * fontScale;

        const int ps1RectX = static_cast<int>(std::floor((rect.minX / safeLayoutW) * 320.0f + 0.5f));
        const int ps1RectY = static_cast<int>(std::floor((1.0f - rect.maxY / safeLayoutH) * 240.0f + 0.5f));
        const int ps1RectW = static_cast<int>(std::floor((rect.Width() / safeLayoutW) * 320.0f + 0.5f));
        const int ps1RectH = static_cast<int>(std::floor((rect.Height() / safeLayoutH) * 240.0f + 0.5f));

        int ps1PenX = ps1RectX;
        if (alignment == UITextAlignment::Center)
            ps1PenX = ps1RectX + (ps1RectW - ps1TextWidth) / 2;
        else if (alignment == UITextAlignment::Right)
            ps1PenX = ps1RectX + ps1RectW - ps1TextWidth;

        int ps1PenY = ps1RectY + (ps1RectH - 7 * fontScale) / 2;
        const int ps1StartX = ps1PenX;

        for (char character : textStr) {
            if (character == '\n') {
                ps1PenX = ps1StartX;
                ps1PenY += 8 * fontScale;
                continue;
            }

            if (const uint8_t* glyphRows = Ps1FontRows(character)) {
                for (int rowIndex = 0; rowIndex < 7; ++rowIndex) {
                    for (int colIndex = 0; colIndex < 5; ++colIndex) {
                        if ((glyphRows[rowIndex] & static_cast<uint8_t>(1u << (4 - colIndex))) == 0)
                            continue;

                        const float screenX = static_cast<float>(ps1PenX + colIndex * fontScale) * ps1ScaleX;
                        const float screenY =
                            vpH - static_cast<float>(ps1PenY + rowIndex * fontScale + fontScale) * ps1ScaleY;
                        drawScreenRect(screenX, screenY,
                                       static_cast<float>(fontScale) * ps1ScaleX,
                                       static_cast<float>(fontScale) * ps1ScaleY,
                                       color);
                    }
                }
            }
            ps1PenX += 6 * fontScale;
        }
    };

    auto drawText = [&](const UIRect& rect, UITextAlignment alignment, const std::string& textStr,
                        float fontSize, const glm::vec4& color, bool pixelSpace,
                        const glm::mat4& canvasWorld) {
        const char* text = textStr.c_str();
        ImFont* font = ImGui::GetFont();
        float drawX = 0.0f;
        float layoutYTop = 0.0f;
        CalcUITextDrawLayout(rect, alignment, font, fontSize, text, drawX, layoutYTop);

        if (pixelSpace) {
            if (!ContainsNonAscii(textStr)) {
                drawPs1BitmapText(rect, alignment, textStr, fontSize, color);
            } else {
                const glm::mat4 orthoMvp = glm::ortho(0.0f, vpW, 0.0f, vpH);
                DrawCanvasPlaneText(text, fontSize, color, glm::mat4(1.0f), glm::mat4(1.0f), orthoMvp,
                                    drawX, layoutYTop);
            }
            return;
        }

        DrawCanvasPlaneText(text, fontSize, color, canvasWorld, view, proj, drawX, layoutYTop);
    };

    for (const CanvasEntry& entry : canvases) {
        const float scaleFactor = ComputeCanvasScaleFactor(*entry.canvas, layoutW, layoutH);

        bool pixelSpace = false;
        if (sceneView3D) {
            pixelSpace = false;
        } else {
            pixelSpace = entry.canvas->renderMode == UICanvasRenderMode::ScreenSpaceOverlay;
        }

        glm::mat4 canvasWorld(1.0f);
        if (!pixelSpace) {
            float unitsPerPixel = kCanvasUnitsPerPixel;
            if (sceneView3D) {
                const float aspect = vpH > 0.0f ? (vpW / vpH) : 1.0f;
                unitsPerPixel = ComputeSceneViewCanvasUnitsPerPixel(
                    camera, entry.canvas->planeDistance, layoutWf, layoutHf, aspect);
            }
            canvasWorld = BuildCanvasWorldMatrix(scene, *entry.entity, *entry.canvas, camera, unitsPerPixel,
                                                 sceneView3D);
        }

        UILayoutContext ctx;
        ctx.viewportWidth = sceneView3D ? layoutWf : vpW;
        ctx.viewportHeight = sceneView3D ? layoutHf : vpH;
        ctx.scaleFactor = scaleFactor;
        ctx.pixelSpace = pixelSpace;

        struct ButtonEntry {
            Entity* entity = nullptr;
            UIButtonComponent* button = nullptr;
            UIButtonGroupComponent* group = nullptr;
            uint32_t groupEntityId = 0;
            UIRect rect{};
        };

        struct GroupState {
            Entity* entity = nullptr;
            UIButtonGroupComponent* group = nullptr;
            std::vector<size_t> buttonIndices;
        };

        std::vector<ButtonEntry> buttons;
        std::unordered_map<uint32_t, GroupState> groups;
        std::unordered_map<uint32_t, size_t> buttonIndexByEntity;

        VisitCanvasUI(scene, *entry.entity, *entry.canvas, ctx, [&](Entity& entity, const UIRect& rect) {
            if (auto* group = entity.GetComponent<UIButtonGroupComponent>(); group && group->enabled) {
                groups[entity.GetID()].entity = &entity;
                groups[entity.GetID()].group = group;
            }
            if (auto* button = entity.GetComponent<UIButtonComponent>(); button && button->enabled) {
                Entity* groupEntity = FindButtonGroupAncestor(scene, &entity);
                auto* group = groupEntity ? groupEntity->GetComponent<UIButtonGroupComponent>() : nullptr;
                if (!group || !group->enabled)
                    return;
                const size_t index = buttons.size();
                buttons.push_back({ &entity, button, group, groupEntity->GetID(), rect });
                buttonIndexByEntity[entity.GetID()] = index;
                groups[groupEntity->GetID()].entity = groupEntity;
                groups[groupEntity->GetID()].group = group;
                groups[groupEntity->GetID()].buttonIndices.push_back(index);
            }
        });

        for (auto& [groupId, state] : groups) {
            if (!state.group || state.buttonIndices.empty())
                continue;
            UIButtonGroupComponent& group = *state.group;
            const int count = static_cast<int>(state.buttonIndices.size());
            group.selectedIndex = std::clamp(group.selectedIndex, 0, std::max(count - 1, 0));
            group.pressedThisFrame = false;

            if (!sceneView3D) {
                bool moveUp = false;
                bool moveDown = false;
                bool confirm = false;
                if (group.keyboardNavigation) {
                    moveUp = moveUp || Input::IsKeyDown(GLFW_KEY_UP);
                    moveDown = moveDown || Input::IsKeyDown(GLFW_KEY_DOWN);
                }
                if (group.keyboardConfirm)
                    confirm = confirm || Input::IsKeyDown(ConfirmKeyToGlfw(group.confirmKey));
                if (group.gamepadNavigation || group.gamepadConfirm) {
                    bool padUp = false;
                    bool padDown = false;
                    bool padConfirm = false;
                    ReadGamepadEdge(groupId, ConfirmButtonToGlfw(group.confirmButton),
                                    padUp, padDown, padConfirm);
                    if (group.gamepadNavigation) {
                        moveUp = moveUp || padUp;
                        moveDown = moveDown || padDown;
                    }
                    if (group.gamepadConfirm)
                        confirm = confirm || padConfirm;
                }

                if (moveUp && !moveDown) {
                    if (group.selectedIndex > 0)
                        --group.selectedIndex;
                    else if (group.wrapNavigation)
                        group.selectedIndex = count - 1;
                } else if (moveDown && !moveUp) {
                    if (group.selectedIndex < count - 1)
                        ++group.selectedIndex;
                    else if (group.wrapNavigation)
                        group.selectedIndex = 0;
                }
                group.pressedThisFrame = confirm;
            }
        }

        VisitCanvasUI(scene, *entry.entity, *entry.canvas, ctx, [&](Entity& entity, const UIRect& rect) {
            if (auto it = buttonIndexByEntity.find(entity.GetID()); it != buttonIndexByEntity.end()) {
                ButtonEntry& b = buttons[it->second];
                const auto groupIt = groups.find(b.groupEntityId);
                const bool selected = groupIt != groups.end() && groupIt->second.group &&
                    !groupIt->second.buttonIndices.empty() &&
                    groupIt->second.buttonIndices[static_cast<size_t>(
                        std::clamp(groupIt->second.group->selectedIndex, 0,
                                   static_cast<int>(groupIt->second.buttonIndices.size()) - 1))] == it->second;
                const bool pressed = selected && groupIt != groups.end() &&
                    groupIt->second.group && groupIt->second.group->pressedThisFrame;
                const glm::vec4 bg = pressed ? b.button->pressedColor :
                    (selected ? b.button->selectedColor : b.button->normalColor);
                const Texture* bgTexture = b.button->backgroundTexture.get();
                if (!bgTexture && !b.button->backgroundTexturePath.empty()) {
                    b.button->backgroundTexture =
                        AssetManager::Get().GetTexture(b.button->backgroundTexturePath);
                    bgTexture = b.button->backgroundTexture.get();
                }
                UIRect drawRect = rect;
                if (bgTexture && b.button->preserveAspect && bgTexture->GetHeight() > 0)
                    drawRect = FitRectToAspect(
                        rect, static_cast<float>(bgTexture->GetWidth()) /
                                  static_cast<float>(bgTexture->GetHeight()));
                DrawSolidQuad(buildQuadMvp(drawRect, pixelSpace, canvasWorld, b.entity), bg, bgTexture);
            }

            if (auto* image = entity.GetComponent<UIImageComponent>(); image && image->enabled) {
                const Texture* tex = image->texture.get();
                if (!tex && !image->texturePath.empty()) {
                    image->texture = AssetManager::Get().GetTexture(image->texturePath);
                    tex = image->texture.get();
                }
                UIRect drawRect = rect;
                if (tex && image->preserveAspect && tex->GetHeight() > 0)
                    drawRect = FitRectToAspect(
                        rect, static_cast<float>(tex->GetWidth()) / static_cast<float>(tex->GetHeight()));
                DrawSolidQuad(buildQuadMvp(drawRect, pixelSpace, canvasWorld, &entity), image->color, tex);
            }

            if (auto* text = entity.GetComponent<UITextComponent>(); text && text->enabled && !text->text.empty())
                drawText(rect, text->alignment, text->text, text->fontSize, text->color, pixelSpace, canvasWorld);

            if (auto* spectrum = entity.GetComponent<UIAudioSpectrumComponent>(); spectrum && spectrum->enabled) {
                if (spectrum->backgroundColor.a > 0.0f)
                    DrawSolidQuad(buildQuadMvp(rect, pixelSpace, canvasWorld, &entity), spectrum->backgroundColor, nullptr);

                const int bars = std::clamp(spectrum->barCount, 4, 32);
                const auto& raw = Engine::Get().GetAudioSystem().GetSpectrum(spectrum->sourceEntityId);
                if (spectrum->displayBands.size() != static_cast<size_t>(bars))
                    spectrum->displayBands.assign(static_cast<size_t>(bars), 0.0f);
                const float smooth = std::clamp(spectrum->smoothing, 0.0f, 0.98f);
                const float gap = std::clamp(spectrum->barGap, 0.0f,
                                             rect.Width() / static_cast<float>(bars) * 0.8f);
                const float barWidth = std::max(0.5f,
                    (rect.Width() - gap * static_cast<float>(bars - 1)) / static_cast<float>(bars));
                for (int bar = 0; bar < bars; ++bar) {
                    const size_t sampleIndex = raw.empty() ? 0u : std::min(
                        static_cast<size_t>(bar) * raw.size() / static_cast<size_t>(bars), raw.size() - 1u);
                    const float target = raw.empty() ? 0.0f :
                        std::clamp(raw[sampleIndex] * spectrum->sensitivity, 0.0f, 1.0f);
                    float& shown = spectrum->displayBands[static_cast<size_t>(bar)];
                    shown = target > shown ? target : shown * smooth + target * (1.0f - smooth);
                    const float height = std::max(rect.Height() * shown, shown > 0.002f ? 1.0f : 0.0f);
                    if (height <= 0.0f)
                        continue;
                    const float x = rect.minX + static_cast<float>(bar) * (barWidth + gap);
                    const UIRect barRect{ x, rect.minY, x + barWidth, rect.minY + height };
                    DrawSolidQuad(buildQuadMvp(barRect, pixelSpace, canvasWorld), spectrum->color, nullptr);
                }
            }
        });

        for (auto& [groupId, state] : groups) {
            if (!state.group || state.buttonIndices.empty())
                continue;
            UIButtonGroupComponent& group = *state.group;
            const int selected = std::clamp(group.selectedIndex, 0,
                                            static_cast<int>(state.buttonIndices.size()) - 1);
            const ButtonEntry& selectedButton = buttons[state.buttonIndices[static_cast<size_t>(selected)]];
            const UIRect& buttonRect = selectedButton.rect;
            const glm::vec2 size = {
                std::max(group.cursorSize.x, 1.0f),
                std::max(group.cursorSize.y, 1.0f),
            };
            const glm::vec2 center = {
                buttonRect.minX + group.cursorOffset.x,
                buttonRect.Center().y + group.cursorOffset.y,
            };
            const UIRect cursorRect{
                center.x - size.x * 0.5f,
                center.y - size.y * 0.5f,
                center.x + size.x * 0.5f,
                center.y + size.y * 0.5f,
            };

            const Texture* cursorTexture = group.cursorTexture.get();
            if (!cursorTexture && !group.cursorTexturePath.empty()) {
                group.cursorTexture = AssetManager::Get().GetTexture(group.cursorTexturePath);
                cursorTexture = group.cursorTexture.get();
            }

            if (cursorTexture) {
                DrawSolidQuad(buildQuadMvp(cursorRect, pixelSpace, canvasWorld),
                              glm::vec4(1.0f), cursorTexture);
            } else {
                drawText(cursorRect, UITextAlignment::Center, ">", std::max(size.y, 12.0f),
                         glm::vec4(1.0f), pixelSpace, canvasWorld);
            }
        }
    }

    EndPass();
}

} // namespace MipsyncEngine
