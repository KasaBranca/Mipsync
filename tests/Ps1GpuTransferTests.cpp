#include <cstdint>
#include <vector>
#include <stdexcept>
#include <iostream>
extern "C" {
#include "gpu_transfer.h"
}
static std::vector<uint32_t> source;
static std::vector<uint16_t> vram;
static void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
extern "C" int DrawSync(int) { return 0; }
extern "C" int LoadImage(const RECT* rect, const uint32_t* data) {
    unsigned int words = rect->w * rect->h / 2;
    unsigned int dmaWords = words < 16 ? words : (words + 15) / 16 * 16;
    require(dmaWords == words, "SDK would send extra image data as GPU commands");
    require(data >= source.data() && data + dmaWords <= source.data() + source.size(),
        "DMA reads beyond texture allocation");
    const auto* pixels = reinterpret_cast<const uint16_t*>(data);
    for (int y = 0; y < rect->h; ++y)
        for (int x = 0; x < rect->w; ++x)
            vram[(rect->y + y) * 1024 + rect->x + x] = pixels[y * rect->w + x];
    return 0;
}
int main() {
    try {
        for (auto dimensions : {std::pair{104,70}, {214,72}, {220,137}, {256,172}, {64,64},
                {2,17}, {30,5}, {16,1}, {12,2}, {100,71}, {102,69}}) {
            int w = dimensions.first, h = dimensions.second;
            source.resize(w * h / 2);
            auto* pixels = reinterpret_cast<uint16_t*>(source.data());
            for (int i = 0; i < w * h; ++i) pixels[i] = static_cast<uint16_t>(i * 17 + 1);
            vram.assign(1024 * 512, 0xaaaa);
            RECT rect{512,256,static_cast<short>(w),static_cast<short>(h)};
            ps1_gpu_load_image(&rect, source.data());
            for (int y = 0; y < h; ++y)
                for (int x = 0; x < w; ++x)
                    require(vram[(256 + y) * 1024 + 512 + x] == pixels[y * w + x],
                        "Split transfer changes pixel positions");
            require(vram[255 * 1024 + 512] == 0xaaaa &&
                    vram[(256 + h) * 1024 + 512] == 0xaaaa &&
                    vram[256 * 1024 + 511] == 0xaaaa &&
                    vram[256 * 1024 + 512 + w] == 0xaaaa, "Transfer touches neighbouring VRAM");
        }
        std::cout << "Exact texture DMA transfer regression passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n'; return 1;
    }
}
