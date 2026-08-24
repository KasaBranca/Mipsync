#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace MipsyncEngine {

struct Ps1VagData {
    uint32_t sampleRate = 0;
    std::vector<uint8_t> frames;
};

/// Converts a desktop audio asset to mono PS1 SPU ADPCM (max 22050 Hz).
bool EncodeAudioForPs1(const std::string& absolutePath, bool loop, Ps1VagData& outData,
                       std::string& outError, uint32_t maxSampleRate = 22050);

} // namespace MipsyncEngine
