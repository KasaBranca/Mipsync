#pragma once
// ─────────────────────────────────────────────────
// Mipsync Engine — Texture (PS1 style: nearest neighbor)
// ─────────────────────────────────────────────────

#include <glad/glad.h>
#include <string>

namespace MipsyncEngine {

struct TextureParams {
    bool nearestFilter = true;   // PS1: always nearest neighbor
    bool clampToEdge   = false;
    int  maxSize       = 256;    // PS1 texture size limit
    /// stbi flip on load (default true for authored PNGs; false for GL-captured thumbnails).
    bool flipVerticallyOnLoad = true;
    /// Treat the top-left pixel as a matte color and recover transparency around it.
    bool colorKeyTopLeft = false;
};

class Texture {
public:
    Texture() = default;
    Texture(const std::string& path, const TextureParams& params = {});
    Texture(int width, int height, const unsigned char* data = nullptr, int channels = 4, const TextureParams& params = {});
    ~Texture();

    Texture(Texture&& o) noexcept;
    Texture& operator=(Texture&& o) noexcept;
    Texture(const Texture&) = delete;
    Texture& operator=(const Texture&) = delete;

    void Bind(int slot = 0) const;
    void Unbind() const;

    GLuint GetID() const { return m_TextureID; }
    int GetWidth() const { return m_Width; }
    int GetHeight() const { return m_Height; }

    static Texture CreateCheckerboard(int size = 64, int cellSize = 8);
    static Texture CreateSolid(unsigned char r, unsigned char g, unsigned char b, unsigned char a = 255);

private:
    void Cleanup();
    GLuint m_TextureID = 0;
    int m_Width = 0, m_Height = 0, m_Channels = 0;
};

} // namespace MipsyncEngine
