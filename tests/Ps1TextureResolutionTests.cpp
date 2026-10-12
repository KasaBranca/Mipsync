#include "assets/Material.h"
#include "mips/PS1TextureExport.h"
#include "core/Log.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <regex>
#include <stdexcept>
#include <chrono>

namespace fs = std::filesystem;
static void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
int main() {
    MipsyncEngine::Log::Init();
    const fs::path root = fs::temp_directory_path() /
        ("mipsync-texture-resolution-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(root);
    check(fs::weakly_canonical(root).parent_path() == fs::weakly_canonical(fs::temp_directory_path()),
          "Test artifact directory must stay within the temporary directory");
    try {
        std::ofstream image(root / "detail.ppm", std::ios::binary);
        image << "P6\n512 512\n255\n";
        for (int y = 0; y < 512; ++y)
            for (int x = 0; x < 512; ++x) {
                const char rgb[] = {char((x / 4 & 1) ? 255 : 0), char((y / 4 & 1) ? 255 : 0), char(128)};
                image.write(rgb, 3);
            }
        image.close();
        using namespace MipsyncEngine;
        using namespace MipsyncEngine::Mips;
        std::string error;
        Material authored;
        authored.texturePath = "detail.ppm";
        authored.ps1TextureMaxSize = 256;
        check(Material::Save((root / "detail.nmat").string(), authored, error), "Material save failed");
        Material restored;
        check(Material::Load((root / "detail.nmat").string(), restored, error) && restored.ps1TextureMaxSize == 256,
              "PS1 material size did not survive save and load");
        { std::ofstream legacy(root / "legacy.nmat"); legacy << "{\"texture\":\"detail.ppm\"}"; }
        check(Material::Load((root / "legacy.nmat").string(), restored, error) && restored.ps1TextureMaxSize == 64,
              "Legacy materials must retain their original default resolution");

        const std::vector<std::string> paths = {"detail.ppm", "mipsynctile8://detail.ppm", "mipsyncui://detail.ppm"};
        check(EmitTexturesDataC(paths, root.string(), (root / "textures.c").string(), error, {}, {256, 256, 64}),
              "Mixed-resolution texture packing failed");
        std::ifstream generated(root / "textures.c");
        std::string text((std::istreambuf_iterator<char>(generated)), {});
        generated.close();
        std::regex hex("0x([0-9a-f]+)");
        struct Rect {int x, y, w, h;};
        std::vector<Rect> rects;
        size_t cursor = 0;
        while ((cursor = text.find("static const uint32_t", cursor)) != std::string::npos) {
            cursor = text.find('{', cursor) + 1;
            std::string words = text.substr(cursor, 160);
            std::vector<unsigned> header;
            for (auto wi = std::sregex_iterator(words.begin(), words.end(), hex); wi != std::sregex_iterator() && header.size() < 5; ++wi)
                header.push_back(std::stoul((*wi)[1], nullptr, 16));
            check(header.size() == 5 && header[0] == 16 && header[1] == 2, "Invalid direct-color TIM header");
            Rect r{int(header[3] & 65535), int(header[3] >> 16), int(header[4] & 65535), int(header[4] >> 16)};
            check(r.x >= 512 && r.x + r.w <= 1024 && r.y + r.h <= 512 && (r.y % 256) + r.h <= 256,
                  "Texture crossed a framebuffer or GPU texture page");
            for (auto p : rects)
                check(r.x >= p.x + p.w || p.x >= r.x + r.w || r.y >= p.y + p.h || p.y >= r.y + r.h,
                      "Textures overlap in VRAM");
            rects.push_back(r);
        }
        check(rects.size() == 3 && rects[0].w == 256 && rects[1].w == 256 && rects[2].w == 64,
              "Mesh, repeated floor, or UI resolution was ignored");
        check(!EmitTexturesDataC(std::vector<std::string>(5, "detail.ppm"), root.string(),
                               (root / "overflow.c").string(), error, {}, std::vector<int>(5, 256)),
              "Excess texture VRAM must fail instead of corrupting another texture");
        fs::remove_all(root);
        std::cout << "PS1 texture resolution, material persistence, and page packing passed.\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        fs::remove_all(root);
        return 1;
    }
}
