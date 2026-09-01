#include "PS1TextureExport.h"
#include "../core/Log.h"
#include "../assets/AssetManager.h"
#include <stb_image.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <filesystem>
#include <iomanip>
#include <unordered_set>

namespace MipsyncEngine::Mips {
namespace fs = std::filesystem;

namespace {

constexpr const char* kBakedTilePrefix = "mipsynctile8://";
constexpr int kBakedTileRepeats = 8;

bool ParseBakedTilePath(const std::string& path, std::string& sourcePath) {
    if (path.rfind(kBakedTilePrefix, 0) != 0) {
        sourcePath = path;
        return false;
    }
    sourcePath = path.substr(std::char_traits<char>::length(kBakedTilePrefix));
    return true;
}

PsxExportedTexture RepeatTexture(const PsxExportedTexture& source, int repeats) {
    PsxExportedTexture repeated;
    if (source.width <= 0 || source.height <= 0 || source.pixels565.empty() || repeats <= 0)
        return repeated;
    repeated.width = source.width * repeats;
    repeated.height = source.height * repeats;
    repeated.pixels565.resize(
        static_cast<size_t>(repeated.width) * static_cast<size_t>(repeated.height));
    for (int y = 0; y < repeated.height; ++y) {
        for (int x = 0; x < repeated.width; ++x) {
            repeated.pixels565[static_cast<size_t>(y) * repeated.width + x] =
                source.pixels565[static_cast<size_t>(y % source.height) * source.width +
                                 static_cast<size_t>(x % source.width)];
        }
    }
    return repeated;
}

void ComputeClampedSize(int srcW, int srcH, int maxSize, int& outW, int& outH) {
    outW = srcW;
    outH = srcH;
    if (maxSize <= 0 || (srcW <= maxSize && srcH <= maxSize))
        return;
    const float scale = static_cast<float>(maxSize) / static_cast<float>(std::max(srcW, srcH));
    outW = std::max(1, static_cast<int>(std::lround(static_cast<float>(srcW) * scale)));
    outH = std::max(1, static_cast<int>(std::lround(static_cast<float>(srcH) * scale)));
}

std::vector<unsigned char> DownsampleBox(const unsigned char* src, int srcW, int srcH, int channels,
                                         int dstW, int dstH) {
    std::vector<unsigned char> dst(static_cast<size_t>(dstW) * static_cast<size_t>(dstH) *
                                   static_cast<size_t>(channels));
    if (dst.empty() || !src)
        return dst;
    for (int y = 0; y < dstH; ++y) {
        const int y0 = (y * srcH) / dstH;
        const int y1 = std::max(y0 + 1, ((y + 1) * srcH + dstH - 1) / dstH);
        for (int x = 0; x < dstW; ++x) {
            const int x0 = (x * srcW) / dstW;
            const int x1 = std::max(x0 + 1, ((x + 1) * srcW + dstW - 1) / dstW);
            const size_t dstOff =
                (static_cast<size_t>(y) * static_cast<size_t>(dstW) + static_cast<size_t>(x)) *
                static_cast<size_t>(channels);
            uint32_t sum[4] = { 0, 0, 0, 0 };
            uint32_t count = 0;
            for (int sy = y0; sy < y1 && sy < srcH; ++sy) {
                for (int sx = x0; sx < x1 && sx < srcW; ++sx) {
                    const size_t srcOff =
                        (static_cast<size_t>(sy) * static_cast<size_t>(srcW) + static_cast<size_t>(sx)) *
                        static_cast<size_t>(channels);
                    for (int c = 0; c < channels && c < 4; ++c)
                        sum[c] += src[srcOff + static_cast<size_t>(c)];
                    ++count;
                }
            }
            if (count == 0)
                count = 1;
            for (int c = 0; c < channels; ++c)
                dst[dstOff + static_cast<size_t>(c)] =
                    static_cast<unsigned char>((sum[c] + count / 2u) / count);
        }
    }
    return dst;
}

int RoundEvenUp(int v) {
    if (v < 2)
        return 2;
    return (v + 1) & ~1;
}

/* PS1 clear in main.c — texels that quantize to this 565 value need a +1 nudge. */
constexpr uint16_t kPs1Bg565 = static_cast<uint16_t>((6u << 10) | (6u << 5) | 6u); /* RGB(48,50,54) */

static uint16_t QuantizeRgb565(uint8_t r, uint8_t g, uint8_t b) {
    const uint16_t R = static_cast<uint16_t>((r >> 3) & 0x1F);
    const uint16_t G = static_cast<uint16_t>((g >> 3) & 0x1F);
    const uint16_t B = static_cast<uint16_t>((b >> 3) & 0x1F);
    return static_cast<uint16_t>((B << 10) | (G << 5) | R);
}

uint16_t RgbaToPsx565(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    if (a < 128) {
        return 0; // Transparent on PS1
    }
    uint16_t c = QuantizeRgb565(r, g, b);
    /* Only nudge exact background collision (not a global shadow lift). */
    if (c == kPs1Bg565) {
        const uint16_t B = static_cast<uint16_t>(((c >> 10) & 0x1Fu) + 1u);
        c = static_cast<uint16_t>(((B & 0x1Fu) << 10) | (c & 0x03FFu));
    }
    if (c == 0) {
        c = static_cast<uint16_t>((1u << 10) | (1u << 5) | 1u);
    }
    return c;
}

uint16_t SanitizePsx565Texel(uint16_t c) {
    if (c == 0)
        return 0; // Preserve transparent
    c = static_cast<uint16_t>(c & 0x7FFFu);
    if (c == 0)
        return static_cast<uint16_t>((1u << 10) | (1u << 5) | 1u);
    return c;
}

uint16_t Psx565PaddingPixel() {
    return SanitizePsx565Texel(0);
}

std::vector<uint32_t> BuildTim16(int vramX, int vramY, int w, int h,
                                 const std::vector<uint16_t>& pixels) {
    std::vector<uint32_t> tim;
    tim.reserve(5 + (pixels.size() + 1) / 2);
    tim.push_back(0x10u); /* TIM magic */
    tim.push_back(0x02u); /* 16 bpp, no CLUT */
    tim.push_back(12u + static_cast<uint32_t>(w * h * 2));
    tim.push_back(static_cast<uint32_t>(static_cast<uint16_t>(vramX)) |
                  (static_cast<uint32_t>(static_cast<uint16_t>(vramY)) << 16));
    tim.push_back(static_cast<uint32_t>(static_cast<uint16_t>(w)) |
                  (static_cast<uint32_t>(static_cast<uint16_t>(h)) << 16));
    for (size_t i = 0; i < pixels.size(); i += 2) {
        const uint32_t lo = pixels[i];
        const uint32_t hi = (i + 1 < pixels.size()) ? pixels[i + 1] : 0u;
        tim.push_back(lo | (hi << 16));
    }
    return tim;
}

std::string SanitizeSymbol(const std::string& name) {
    std::string out;
    for (char c : name) {
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') || c == '_')
            out.push_back(c);
        else
            out.push_back('_');
    }
    if (out.empty() || (out[0] >= '0' && out[0] <= '9'))
        out.insert(out.begin(), '_');
    return out;
}

} // namespace

bool LoadPsxTextureFromFile(const std::string& absPath, int maxSize, PsxExportedTexture& out,
                            std::string& outError, bool flipVertically) {
    out = {};
    outError.clear();

    stbi_set_flip_vertically_on_load(flipVertically ? 1 : 0);
    int srcW = 0;
    int srcH = 0;
    int srcChannels = 0;
    unsigned char* loaded = stbi_load(absPath.c_str(), &srcW, &srcH, &srcChannels, 4);
    stbi_set_flip_vertically_on_load(true);
    if (!loaded) {
        outError = "stbi_load failed: " + absPath;
        return false;
    }

    int uploadW = srcW;
    int uploadH = srcH;
    ComputeClampedSize(srcW, srcH, maxSize, uploadW, uploadH);

    uploadW = RoundEvenUp(uploadW);
    uploadH = RoundEvenUp(uploadH);

    const unsigned char* uploadData = loaded;
    std::vector<unsigned char> resized;
    if (uploadW != srcW || uploadH != srcH) {
        resized = DownsampleBox(loaded, srcW, srcH, 4, uploadW, uploadH);
        uploadData = resized.data();
    }
    out.width = uploadW;
    out.height = uploadH;
    out.pixels565.assign(static_cast<size_t>(uploadW) * static_cast<size_t>(uploadH), Psx565PaddingPixel());

    for (int y = 0; y < uploadH; ++y) {
        const int sy = std::min(y, uploadH - 1);
        for (int x = 0; x < uploadW; ++x) {
            const int sx = std::min(x, uploadW - 1);
            const size_t off =
                (static_cast<size_t>(sy) * static_cast<size_t>(uploadW) + static_cast<size_t>(sx)) * 4u;
            const uint8_t r = uploadData[off + 0];
            const uint8_t g = uploadData[off + 1];
            const uint8_t b = uploadData[off + 2];
            const uint8_t a = uploadData[off + 3];
            out.pixels565[static_cast<size_t>(y) * static_cast<size_t>(uploadW) +
                          static_cast<size_t>(x)] = RgbaToPsx565(r, g, b, a);
        }
    }

    for (uint16_t& px : out.pixels565)
        px = SanitizePsx565Texel(px);

    stbi_image_free(loaded);
    return true;
}

bool EmitTexturesDataC(const std::vector<std::string>& projectRelativePaths,
                       const std::string& projectRoot, const std::string& outCFile,
                       std::string& outError, const std::vector<std::string>& backgroundPaths) {
    try {
        const fs::path cPath = PathUtf8::FromString(outCFile);
        if (cPath.has_parent_path())
            fs::create_directories(cPath.parent_path());

        std::ofstream out(cPath, std::ios::trunc);
        if (!out.is_open()) {
            outError = "cannot open textures_data.c: " + outCFile;
            return false;
        }

        out << "/* Auto-generated by Mipsync PS1TextureExport. Do not edit. */\n"
               "#include \"../runtime/textures.h\"\n\n";

        /* getTPage() selects X bases in 64-word increments. Since these TIMs
         * are 16-bpp (one texel per VRAM word), 64x64 cells give every texture
         * a non-overlapping base and page-local UV range while packing densely. */
        constexpr int kMaxTexSize = 64;
        const fs::path root = PathUtf8::FromString(projectRoot);
        const std::unordered_set<std::string> backgroundSet(backgroundPaths.begin(),
                                                            backgroundPaths.end());

        struct LoadedTex {
            std::string sym;
            PsxExportedTexture tex;
        };
        std::vector<LoadedTex> loaded;
        loaded.reserve(projectRelativePaths.size());

        for (size_t i = 0; i < projectRelativePaths.size(); ++i) {
            const std::string& rel = projectRelativePaths[i];
            std::string sourceRel;
            const bool bakedTile = ParseBakedTilePath(rel, sourceRel);
            const fs::path abs = root / sourceRel;
            PsxExportedTexture tex;
            std::string err;
            int maxTexSize = bakedTile ? (kMaxTexSize / kBakedTileRepeats) : kMaxTexSize;
            bool isBackground = backgroundSet.find(rel) != backgroundSet.end();
            if (isBackground) {
                maxTexSize = 512;
            }
            if (!LoadPsxTextureFromFile(PathUtf8::ToString(abs), maxTexSize, tex, err, !isBackground)) {
                MIPSYNC_WARN("PS1 texture export skipped '{}': {}", rel, err);
                loaded.push_back({});
                continue;
            }
            if (bakedTile)
                tex = RepeatTexture(tex, kBakedTileRepeats);
            {
                size_t bad = 0;
                for (uint16_t& px : tex.pixels565) {
                    if ((px & 0x8000u) != 0)
                        ++bad;
                    px = SanitizePsx565Texel(px);
                }
                if (bad > 0) {
                    MIPSYNC_WARN("PS1 texture '{}': fixed {} semi-transparent texels (STP)",
                                 rel, bad);
                }
            }
            loaded.push_back({ SanitizeSymbol(rel) + "_" + std::to_string(i), std::move(tex) });
            MIPSYNC_INFO("PS1 texture export: {} -> {}x{}{}", sourceRel,
                         loaded.back().tex.width, loaded.back().tex.height,
                         bakedTile ? " (8x baked repeats)" : "");
        }

        int normalTexCount = 0;
        int backgroundTexCount = 0;
        const bool hasBackgrounds = !backgroundSet.empty();
        for (size_t ti = 0; ti < loaded.size(); ++ti) {
            const LoadedTex& lt = loaded[ti];
            if (lt.sym.empty() || lt.tex.pixels565.empty())
                continue;

            int vramX = 0;
            int vramY = 0;
            const std::string& rel = projectRelativePaths[ti];
            if (backgroundSet.find(rel) != backgroundSet.end()) {
                ++backgroundTexCount;
                /* Keep the active 320x240 pre-render background in an aligned,
                 * texture-readable region outside both framebuffers. All
                 * backgrounds share this slot and are uploaded on camera switch. */
                vramX = 640;
                vramY = 0;
            } else {
                int slot = normalTexCount;
                if (hasBackgrounds) {
                    if (slot == 0) {
                        vramX = 512;
                        vramY = 0;
                    } else {
                        const int lowerSlot = slot - 1;
                        vramX = 512 + (lowerSlot % 8) * 64;
                        vramY = 256 + (lowerSlot / 8) * 64;
                        if (vramY + lt.tex.height > 512) {
                            MIPSYNC_WARN("PS1 texture VRAM full after reserving pre-render backgrounds; '{}' may overlap VRAM", rel);
                        }
                    }
                } else {
                    vramX = 512 + (slot % 8) * 64;
                    vramY = (slot / 8) * 64;
                    if (vramY + lt.tex.height > 512) {
                        MIPSYNC_WARN("PS1 texture VRAM full; '{}' may overlap VRAM", rel);
                    }
                }
                normalTexCount++;
            }
            const std::vector<uint32_t> tim =
                BuildTim16(vramX, vramY, lt.tex.width, lt.tex.height, lt.tex.pixels565);
            out << "static const uint32_t k_ps1_tex_" << lt.sym << "_tim[] = {\n";
            for (size_t wi = 0; wi < tim.size(); ++wi) {
                if (wi % 6 == 0)
                    out << "    ";
                out << "0x" << std::hex << tim[wi] << std::dec;
                if (wi + 1 < tim.size())
                    out << ", ";
                if (wi % 6 == 5 || wi + 1 == tim.size())
                    out << "\n";
            }
            out << "};\n\n";
        }

        out << "const ps1_texture_desc g_ps1_textures[] = {\n";
        for (size_t ti = 0; ti < loaded.size(); ++ti) {
            const LoadedTex& lt = loaded[ti];
            if (lt.sym.empty() || lt.tex.pixels565.empty()) {
                out << "    { 0, 0, 0 },\n";
                continue;
            }
            const bool isBackground =
                backgroundSet.find(projectRelativePaths[ti]) != backgroundSet.end();
            out << "    { k_ps1_tex_" << lt.sym << "_tim, "
                << "sizeof(k_ps1_tex_" << lt.sym << "_tim) / sizeof(k_ps1_tex_" << lt.sym
                << "_tim[0]), "
                << (isBackground ? 1 : 0) << " },\n";
        }
        out << "};\n";
        out << "const unsigned int g_ps1_texture_count = "
            << projectRelativePaths.size() << "u;\n";
        return out.good();
    } catch (const std::exception& ex) {
        outError = ex.what();
        return false;
    }
}

} // namespace MipsyncEngine::Mips
