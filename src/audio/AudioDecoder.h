#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace MipsyncEngine {

struct DecodedAudio {
    uint32_t sampleRate = 0;
    std::vector<float> monoSamples;
};

bool DecodeAudioFile(const std::string& absolutePath, DecodedAudio& outAudio,
                     std::string& outError);

} // namespace MipsyncEngine
