#include <stb_image.h>
#include <stb_image_write.h>
#define STBTT_STATIC
#define STB_TRUETYPE_IMPLEMENTATION
#include <imstb_truetype.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>

// Generate the BIOS TMD and the matching fast-boot fallback from one raster.
namespace {
constexpr int Width = 160, Height = 96;
using Pixel = std::array<unsigned char, 3>;
std::array<Pixel, Width * Height> pixels{};

void WriteRuntimeImage(const char* path) {
    std::vector<std::pair<uint16_t, uint16_t>> runs;
    for (const auto& p : pixels) {
        uint16_t color = static_cast<uint16_t>((p[0] >> 3) | ((p[1] >> 3) << 5) | ((p[2] >> 3) << 10));
        if (!runs.empty() && runs.back().first == color && runs.back().second < 65535)
            ++runs.back().second;
        else runs.emplace_back(color, 1);
    }
    std::ofstream output(path);
    output << "/* Generated from the shipped Mipsync icon and font. */\n"
              "#ifndef MIPSYNC_BOOT_BRAND_IMAGE_H\n#define MIPSYNC_BOOT_BRAND_IMAGE_H\n"
              "#include <stdint.h>\n"
           << "#define MIPSYNC_BOOT_BRAND_WIDTH " << Width << "\n"
           << "#define MIPSYNC_BOOT_BRAND_HEIGHT " << Height << "\n"
              "static const uint16_t k_mipsync_boot_brand_runs[][2] = {\n";
    for (auto [color, length] : runs) output << "    {" << color << ", " << length << "},\n";
    output << "};\n#endif\n";
    if (!output) throw std::runtime_error("Could not write fallback logo image");
}

void WriteBiosModel(const char* path) {
    struct Rect { int x1, x2, y1, y2; Pixel color; };
    std::vector<Rect> rectangles;
    using Key = std::tuple<int, int, int>;
    std::map<Key, size_t> previous;
    constexpr int modelWidth = Width / 2, modelHeight = 48;
    // BIOS TMD has a fixed 0x3278-byte budget. Merge scanline runs into flat
    // colored rectangles, quantizing the antialiased raster into its palette.
    const std::array<Pixel, 6> palette = {{{0,0,0}, {255,211,0}, {0,172,170},
                                        {230,8,31}, {0,147,205}, {245,245,245}}};
    for (int y = 0; y < modelHeight; ++y) {
        std::map<Key, size_t> current;
        std::array<int, modelWidth> colors{};
        for (int x = 0; x < modelWidth; ++x) {
            Pixel p{};
            for (int c = 0; c < 3; ++c) {
                int sum = 0;
                for (int dy = 0; dy < 2; ++dy) for (int dx = 0; dx < 2; ++dx)
                    sum += pixels[(y * 2 + dy) * Width + x * 2 + dx][c];
                p[c] = static_cast<unsigned char>(sum / 4);
            }
            int best = 0, distance = 1000000;
            for (int i = 0; i < static_cast<int>(palette.size()); ++i) {
                int d = 0;
                for (int c = 0; c < 3; ++c) {
                    int delta = static_cast<int>(p[c]) - palette[i][c];
                    d += delta * delta;
                }
                if (d < distance) { best = i; distance = d; }
            }
            // Grayscale wordmark edges must remain white/black. The icon's
            // cyan otherwise wins the nearest-color match for middle gray.
            colors[x] = p[0] == p[1] && p[1] == p[2] ? (p[0] >= 96 ? 5 : 0) : best;
        }
        for (int x = 0; x < modelWidth;) {
            int begin = x, color = colors[x++];
            while (x < modelWidth && colors[x] == color) ++x;
            if (!color) continue;
            Key key{begin, x, color};
            auto found = previous.find(key);
            if (found != previous.end()) {
                rectangles[found->second].y2 = y + 1;
                current[key] = found->second;
            } else {
                current[key] = rectangles.size();
                rectangles.push_back({begin, x, y, y + 1, palette[color]});
            }
        }
        previous = std::move(current);
    }
    std::vector<uint8_t> data;
    auto word = [&](uint32_t value) {
        for (int i = 0; i < 4; ++i) data.push_back(static_cast<uint8_t>(value >> (i * 8)));
    };
    auto half = [&](uint16_t value) {
        data.push_back(static_cast<uint8_t>(value));
        data.push_back(static_cast<uint8_t>(value >> 8));
    };
    std::map<std::pair<int,int>, uint16_t> indices;
    std::vector<std::pair<int,int>> vertices;
    auto vertex = [&](int x, int y) {
        auto key = std::pair{x,y};
        auto found = indices.find(key);
        if (found != indices.end()) return found->second;
        auto index = static_cast<uint16_t>(vertices.size());
        indices[key] = index;
        vertices.push_back(key);
        return index;
    };
    std::vector<std::array<uint16_t,4>> quads;
    for (const auto& r : rectangles)
        quads.push_back({vertex(r.x1,r.y1), vertex(r.x2,r.y1),
                        vertex(r.x1,r.y2), vertex(r.x2,r.y2)});
    // Use the same flat triangle packet family as the retail boot model;
    // some BIOS boot renderers only implement that subset of TMD.
    const uint32_t normalOffset = 28 + static_cast<uint32_t>(rectangles.size()) * 32;
    const uint32_t vertexOffset = normalOffset + 8;
    word(0x41); word(0); word(1); // TMD, relative addresses, one object.
    word(vertexOffset); word(static_cast<uint32_t>(vertices.size()));
    word(normalOffset); word(1);
    word(28); word(static_cast<uint32_t>(rectangles.size()) * 2); word(7);
    for (size_t i = 0; i < rectangles.size(); ++i) {
        const auto& c = rectangles[i].color;
        for (auto triangle : std::array<std::array<int,3>,2>{{{0,1,2},{1,3,2}}}) {
            word(0x20020304); // F3: 4 GPU words, 3 input words, double-sided.
            word(c[0] | (c[1] << 8) | (c[2] << 16) | (0x20u << 24));
            half(0); // BIOS boot renderer always reads a normal index.
            for (auto corner : triangle) half(quads[i][corner]);
        }
    }
    // The retail boot renderer forces lighting even for unlit TMD flags.
    // Use a full-length forward-facing normal so its bright front light,
    // together with ambient light, keeps the brand vivid during the boot
    // renderer's transformations. A backwards-facing normal gets only the
    // dark ambient contribution.
    half(0); half(0); half(4096); half(0);
    for (auto [x,y] : vertices) {
        half(static_cast<uint16_t>((x * 2 - Width / 2) * 10));
        half(static_cast<uint16_t>((y * 2 - 48) * 10 - 240));
        half(0); half(0);
    }
    if (data.size() > 0x3278) throw std::runtime_error("BIOS logo exceeds its fixed TMD budget");
    std::ofstream output(path, std::ios::binary);
    output.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
    if (!output) throw std::runtime_error("Could not write BIOS logo model");
    std::cout << "BIOS brand: " << data.size() << " bytes, " << rectangles.size() << " quads\n";
}

void DrawText(const stbtt_fontinfo& font, const std::string& text,
              float height, int baseline, int brightness, float spacing = 0) {
    const float scale = stbtt_ScaleForPixelHeight(&font, height);
    float width = 0;
    for (size_t i = 0; i < text.size(); ++i) {
        int advance, bearing;
        stbtt_GetCodepointHMetrics(&font, text[i], &advance, &bearing);
        width += advance * scale;
        if (i + 1 < text.size())
            width += spacing + stbtt_GetCodepointKernAdvance(&font, text[i], text[i + 1]) * scale;
    }
    float pen = (Width - width) / 2;
    for (size_t i = 0; i < text.size(); ++i) {
        int w, h, dx, dy, advance, bearing;
        unsigned char* mask = stbtt_GetCodepointBitmap(&font, 0, scale, text[i], &w, &h, &dx, &dy);
        for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) {
            const int px = static_cast<int>(std::round(pen)) + dx + x, py = baseline + dy + y;
            if (px < 0 || px >= Width || py < 0 || py >= Height) continue;
            const auto value = static_cast<unsigned char>((mask[y * w + x] * brightness + 127) / 255);
            pixels[py * Width + px] = {value, value, value};
        }
        stbtt_FreeBitmap(mask, nullptr);
        stbtt_GetCodepointHMetrics(&font, text[i], &advance, &bearing);
        pen += advance * scale;
        if (i + 1 < text.size())
            pen += spacing + stbtt_GetCodepointKernAdvance(&font, text[i], text[i + 1]) * scale;
    }
}
}

int main(int argc, char** argv) try {
    if (argc < 4 || argc > 6) throw std::runtime_error("Usage: generator icon.png font.ttf bios.tmd [preview.png|-] [runtime_image.h]");
    int w, h, channels;
    unsigned char* icon = stbi_load(argv[1], &w, &h, &channels, 4);
    if (!icon) throw std::runtime_error("Could not read Mipsync icon");
    constexpr int size = 56, left = (Width - size) / 2, top = 4;
    for (int y = 0; y < size; ++y) for (int x = 0; x < size; ++x) {
        // Area sampling preserves the thin black divisions of the brand icon.
        std::array<int, 3> sum{};
        for (int sy = 0; sy < 4; ++sy) for (int sx = 0; sx < 4; ++sx) {
            int ix = std::min(w - 1, static_cast<int>((x + (sx + 0.5) / 4) * w / size));
            int iy = std::min(h - 1, static_cast<int>((y + (sy + 0.5) / 4) * h / size));
            const auto* src = icon + (iy * w + ix) * 4;
            for (int c = 0; c < 3; ++c) sum[c] += src[c] * src[3] / 255;
        }
        auto& dst = pixels[(top + y) * Width + left + x];
        for (int c = 0; c < 3; ++c) dst[c] = static_cast<unsigned char>((sum[c] + 8) / 16);
    }
    stbi_image_free(icon);
    std::ifstream input(argv[2], std::ios::binary);
    std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(input)), {});
    stbtt_fontinfo font{};
    if (bytes.empty() || !stbtt_InitFont(&font, bytes.data(), 0))
        throw std::runtime_error("Could not read Mipsync font");
    DrawText(font, "Mipsync", 30, 88, 245);
    if (std::string(argv[3]) != "-") WriteBiosModel(argv[3]);
    if (argc >= 5 && std::string(argv[4]) != "-") {
        // Preview the source raster used to build the BIOS model.
        if (!stbi_write_png(argv[4], Width, Height, 3, pixels.data(), Width * 3))
            throw std::runtime_error("Could not write preview");
    }
    if (argc == 6) WriteRuntimeImage(argv[5]);
    return 0;
} catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    return 1;
}
