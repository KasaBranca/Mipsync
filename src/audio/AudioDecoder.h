#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace MipsyncEngine {

struct DecodedAudio {
    uint32_t sampleRate = 0;
    std::vector<float> monoSamples;
    uint32_t channels = 1;
    // Protected PC playback retains the original channel layout. Spectrum and
    // PS1 conversion continue to use the mono mix above.
    std::vector<int16_t> playbackSamples;
};

bool DecodeAudioFile(const std::string& absolutePath, DecodedAudio& outAudio,
                     std::string& outError);

} // namespace MipsyncEngine
