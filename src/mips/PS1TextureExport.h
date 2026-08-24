#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace MipsyncEngine::Mips {

struct PsxExportedTexture {
    int width = 0;
    int height = 0;
    std::vector<uint16_t> pixels565; // row-major, PSX 16-bit direct color
};

/// Load and downsample an image for PS1 VRAM (16 bpp). `maxSize` is longest edge (e.g. 128).
bool LoadPsxTextureFromFile(const std::string& absPath, int maxSize, PsxExportedTexture& out,
                            std::string& outError, bool flipVertically = true);

bool EmitTexturesDataC(const std::vector<std::string>& projectRelativePaths,
                       const std::string& projectRoot, const std::string& outCFile,
                       std::string& outError,
                       const std::vector<std::string>& backgroundPaths = {});

} // namespace MipsyncEngine::Mips
