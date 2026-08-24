#include "Texture.h"
#include "../core/Log.h"
#include <stb_image.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

namespace MipsyncEngine {

namespace {

void ComputeClampedSize(int srcW, int srcH, int maxSize, int& outW, int& outH) {
    outW = srcW;
    outH = srcH;
    if (maxSize <= 0 || (srcW <= maxSize && srcH <= maxSize))
        return;

    const float scale = static_cast<float>(maxSize) / static_cast<float>(std::max(srcW, srcH));
    outW = std::max(1, static_cast<int>(std::lround(static_cast<float>(srcW) * scale)));
    outH = std::max(1, static_cast<int>(std::lround(static_cast<float>(srcH) * scale)));
}

std::vector<unsigned char> DownsampleNearest(const unsigned char* src, int srcW, int srcH, int channels,
                                             int dstW, int dstH) {
    std::vector<unsigned char> dst(static_cast<size_t>(dstW) * static_cast<size_t>(dstH) *
                                   static_cast<size_t>(channels));
    if (dst.empty() || !src)
        return dst;

    for (int y = 0; y < dstH; ++y) {
        const int sy = (y * srcH) / dstH;
        for (int x = 0; x < dstW; ++x) {
            const int sx = (x * srcW) / dstW;
            const size_t dstOff =
                (static_cast<size_t>(y) * static_cast<size_t>(dstW) + static_cast<size_t>(x)) *
                static_cast<size_t>(channels);
            const size_t srcOff =
                (static_cast<size_t>(sy) * static_cast<size_t>(srcW) + static_cast<size_t>(sx)) *
                static_cast<size_t>(channels);
            std::memcpy(dst.data() + dstOff, src + srcOff, static_cast<size_t>(channels));
        }
    }
    return dst;
}

} // namespace

Texture::Texture(const std::string& path, const TextureParams& params) {
    stbi_set_flip_vertically_on_load(params.flipVerticallyOnLoad ? 1 : 0);

    int srcW = 0;
    int srcH = 0;
    int srcChannels = 0;
    unsigned char* loaded = stbi_load(path.c_str(), &srcW, &srcH, &srcChannels, 4);
    if (!loaded) {
        MIPSYNC_ERROR("Failed to load texture: {0}", path);
        return;
    }

    if (params.colorKeyTopLeft && srcW > 0 && srcH > 0) {
        const int keyR = loaded[0];
        const int keyG = loaded[1];
        const int keyB = loaded[2];
        const size_t pixelCount = static_cast<size_t>(srcW) * static_cast<size_t>(srcH);
        for (size_t i = 0; i < pixelCount; ++i) {
            unsigned char* pixel = loaded + i * 4u;
            const int dr = static_cast<int>(pixel[0]) - keyR;
            const int dg = static_cast<int>(pixel[1]) - keyG;
            const int db = static_cast<int>(pixel[2]) - keyB;
            const float distance = std::sqrt(static_cast<float>(dr * dr + dg * dg + db * db));
            if (distance <= 6.0f) {
                pixel[3] = 0;
            } else if (distance < 24.0f) {
                const float coverage = (distance - 6.0f) / 18.0f;
                pixel[3] = static_cast<unsigned char>(
                    std::lround(static_cast<float>(pixel[3]) * coverage));
            }
        }
    }

    int uploadW = srcW;
    int uploadH = srcH;
    ComputeClampedSize(srcW, srcH, params.maxSize, uploadW, uploadH);

    const unsigned char* uploadData = loaded;
    std::vector<unsigned char> resized;
    if (uploadW != srcW || uploadH != srcH) {
        MIPSYNC_WARN("Texture {0} exceeds PS1 limit ({1}x{2}), clamping to {3}x{4}", path, srcW, srcH,
                     uploadW, uploadH);
        resized = DownsampleNearest(loaded, srcW, srcH, 4, uploadW, uploadH);
        uploadData = resized.data();
    }

    m_Width = uploadW;
    m_Height = uploadH;
    m_Channels = 4;

    glGenTextures(1, &m_TextureID);
    glBindTexture(GL_TEXTURE_2D, m_TextureID);

    const GLenum filter = params.nearestFilter ? GL_NEAREST : GL_LINEAR;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
    const GLenum wrap = params.clampToEdge ? GL_CLAMP_TO_EDGE : GL_REPEAT;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrap);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrap);

    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, m_Width, m_Height, 0, GL_RGBA, GL_UNSIGNED_BYTE, uploadData);
    stbi_image_free(loaded);
    stbi_set_flip_vertically_on_load(true);

    MIPSYNC_DEBUG("Loaded texture: {0} ({1}x{2}, {3} channels)", path, m_Width, m_Height, m_Channels);
}

Texture::Texture(int width, int height, const unsigned char* data, int channels, const TextureParams& params) 
    : m_Width(width), m_Height(height), m_Channels(channels) {
    
    GLenum format = GL_RGBA;
    GLenum internalFormat = GL_RGBA8;
    if (channels == 1)      { format = GL_RED;  internalFormat = GL_R8; }
    else if (channels == 3) { format = GL_RGB;  internalFormat = GL_RGB8; }

    glGenTextures(1, &m_TextureID);
    glBindTexture(GL_TEXTURE_2D, m_TextureID);

    GLenum filter = params.nearestFilter ? GL_NEAREST : GL_LINEAR;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
    GLenum wrap = params.clampToEdge ? GL_CLAMP_TO_EDGE : GL_REPEAT;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrap);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrap);

    glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, width, height, 0, format, GL_UNSIGNED_BYTE, data);
}

Texture::~Texture() { Cleanup(); }

Texture::Texture(Texture&& o) noexcept : m_TextureID(o.m_TextureID), m_Width(o.m_Width), m_Height(o.m_Height), m_Channels(o.m_Channels) {
    o.m_TextureID = 0;
}

Texture& Texture::operator=(Texture&& o) noexcept {
    if (this != &o) {
        Cleanup();
        m_TextureID = o.m_TextureID; m_Width = o.m_Width; m_Height = o.m_Height; m_Channels = o.m_Channels;
        o.m_TextureID = 0;
    }
    return *this;
}

void Texture::Bind(int slot) const {
    glActiveTexture(GL_TEXTURE0 + slot);
    glBindTexture(GL_TEXTURE_2D, m_TextureID);
}

void Texture::Unbind() const { glBindTexture(GL_TEXTURE_2D, 0); }
void Texture::Cleanup() { if (m_TextureID) { glDeleteTextures(1, &m_TextureID); m_TextureID = 0; } }

Texture Texture::CreateSolid(unsigned char r, unsigned char g, unsigned char b, unsigned char a) {
    const unsigned char pixel[4] = { r, g, b, a };
    TextureParams params;
    params.nearestFilter = false;
    params.clampToEdge = true;
    params.maxSize = 0;
    return Texture(1, 1, pixel, 4, params);
}

Texture Texture::CreateCheckerboard(int size, int cellSize) {
    std::vector<unsigned char> pixels(size * size * 4);
    for (int y = 0; y < size; y++) {
        for (int x = 0; x < size; x++) {
            bool isWhite = ((x / cellSize) + (y / cellSize)) % 2 == 0;
            unsigned char c = isWhite ? 200 : 80;
            int i = (y * size + x) * 4;
            pixels[i] = c; pixels[i+1] = c; pixels[i+2] = c; pixels[i+3] = 255;
        }
    }
    return Texture(size, size, pixels.data(), 4);
}

} // namespace MipsyncEngine
