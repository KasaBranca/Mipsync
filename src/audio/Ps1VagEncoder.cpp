#include "Ps1VagEncoder.h"
#include "AudioDecoder.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

namespace MipsyncEngine {
namespace {

std::vector<int16_t> Resample(const DecodedAudio& input, uint32_t targetRate) {
    if (input.sampleRate == 0 || input.monoSamples.empty()) return {};
    const size_t outCount = std::max<size_t>(1,
        (input.monoSamples.size() * static_cast<uint64_t>(targetRate)) / input.sampleRate);
    std::vector<int16_t> out(outCount);
    const double ratio = static_cast<double>(input.sampleRate) / targetRate;
    for (size_t i = 0; i < outCount; ++i) {
        const double pos = static_cast<double>(i) * ratio;
        const size_t a = std::min(static_cast<size_t>(pos), input.monoSamples.size() - 1);
        const size_t b = std::min(a + 1, input.monoSamples.size() - 1);
        const float t = static_cast<float>(pos - static_cast<double>(a));
        const float sample = input.monoSamples[a] * (1.0f - t) + input.monoSamples[b] * t;
        out[i] = static_cast<int16_t>(std::lround(std::clamp(sample, -1.0f, 0.999969f) * 32768.0f));
    }
    return out;
}

void EncodeFrame(const int16_t* samples, int count, int& history1, int& history2,
                 uint8_t flags, uint8_t* out) {
    static constexpr int coef[5][2] = {
        { 0, 0 }, { 60, 0 }, { 115, -52 }, { 98, -55 }, { 122, -60 }
    };
    int bestFilter = 0;
    int bestShift = 0;
    int64_t bestError = std::numeric_limits<int64_t>::max();
    for (int filter = 0; filter < 5; ++filter) {
        for (int shift = 0; shift <= 12; ++shift) {
            const int step = 1 << (12 - shift);
            int h1 = history1, h2 = history2;
            int64_t error = 0;
            for (int i = 0; i < 28; ++i) {
                const int target = i < count ? samples[i] : 0;
                const int prediction = (h1 * coef[filter][0] + h2 * coef[filter][1] + 32) >> 6;
                const int residual = target - prediction;
                int q = residual >= 0 ? (residual + step / 2) / step : -((-residual + step / 2) / step);
                q = std::clamp(q, -8, 7);
                const int decoded = std::clamp(prediction + q * step, -32768, 32767);
                const int diff = target - decoded;
                error += static_cast<int64_t>(diff) * diff;
                h2 = h1; h1 = decoded;
            }
            if (error < bestError) {
                bestError = error; bestFilter = filter; bestShift = shift;
            }
        }
    }

    std::fill(out, out + 16, 0);
    out[0] = static_cast<uint8_t>((bestFilter << 4) | bestShift);
    out[1] = flags;
    const int step = 1 << (12 - bestShift);
    for (int i = 0; i < 28; ++i) {
        const int target = i < count ? samples[i] : 0;
        const int prediction = (history1 * coef[bestFilter][0] + history2 * coef[bestFilter][1] + 32) >> 6;
        const int residual = target - prediction;
        int q = residual >= 0 ? (residual + step / 2) / step : -((-residual + step / 2) / step);
        q = std::clamp(q, -8, 7);
        const int decoded = std::clamp(prediction + q * step, -32768, 32767);
        history2 = history1; history1 = decoded;
        const uint8_t nibble = static_cast<uint8_t>(q) & 0x0f;
        if (i & 1) out[2 + i / 2] |= static_cast<uint8_t>(nibble << 4);
        else out[2 + i / 2] = nibble;
    }
}

} // namespace

bool EncodeAudioForPs1(const std::string& absolutePath, bool loop, Ps1VagData& outData,
                       std::string& outError, uint32_t maxSampleRate) {
    DecodedAudio decoded;
    if (!DecodeAudioFile(absolutePath, decoded, outError)) return false;
    maxSampleRate = std::clamp<uint32_t>(maxSampleRate, 4000, 22050);
    outData.sampleRate = std::clamp<uint32_t>(decoded.sampleRate, 4000, maxSampleRate);
    const std::vector<int16_t> pcm = Resample(decoded, outData.sampleRate);
    if (pcm.empty()) { outError = "audio contains no samples"; return false; }
    const size_t frameCount = (pcm.size() + 27) / 28;
    outData.frames.resize(frameCount * 16);
    int history1 = 0, history2 = 0;
    for (size_t frame = 0; frame < frameCount; ++frame) {
        uint8_t flags = 0;
        if (loop && frame == 0) flags |= 4;
        if (frame + 1 == frameCount) flags |= loop ? 3 : 1;
        const size_t offset = frame * 28;
        const int count = static_cast<int>(std::min<size_t>(28, pcm.size() - offset));
        EncodeFrame(pcm.data() + offset, count, history1, history2, flags,
                    outData.frames.data() + frame * 16);
    }
    return true;
}

} // namespace MipsyncEngine
