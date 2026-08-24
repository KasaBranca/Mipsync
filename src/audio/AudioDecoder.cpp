#include "AudioDecoder.h"
#include "../assets/AssetManager.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <objbase.h>
#endif

namespace MipsyncEngine {
namespace {

uint32_t ReadBe32(const unsigned char* p) {
    return (static_cast<uint32_t>(p[0]) << 24) |
           (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8) | static_cast<uint32_t>(p[3]);
}

bool DecodeVag(const std::string& path, DecodedAudio& out, std::string& error) {
    FILE* file = nullptr;
#ifdef _WIN32
    _wfopen_s(&file, PathUtf8::FromString(path).c_str(), L"rb");
#else
    file = std::fopen(path.c_str(), "rb");
#endif
    if (!file) { error = "cannot open VAG"; return false; }
    std::fseek(file, 0, SEEK_END);
    const long length = std::ftell(file);
    std::fseek(file, 0, SEEK_SET);
    std::vector<unsigned char> bytes(length > 0 ? static_cast<size_t>(length) : 0);
    const bool readOk = !bytes.empty() &&
        std::fread(bytes.data(), 1, bytes.size(), file) == bytes.size();
    std::fclose(file);
    if (!readOk) { error = "cannot read VAG"; return false; }
    if (bytes.size() < 48 || std::memcmp(bytes.data(), "VAGp", 4) != 0) {
        error = "invalid VAG header"; return false;
    }
    out.sampleRate = ReadBe32(bytes.data() + 16);
    const size_t dataEnd = std::min(bytes.size(), static_cast<size_t>(48) + ReadBe32(bytes.data() + 12));
    static constexpr int coef[5][2] = {
        { 0, 0 }, { 60, 0 }, { 115, -52 }, { 98, -55 }, { 122, -60 }
    };
    int h1 = 0, h2 = 0;
    for (size_t offset = 48; offset + 16 <= dataEnd; offset += 16) {
        const int predictor = std::min<int>(bytes[offset] >> 4, 4);
        const int shift = std::min<int>(bytes[offset] & 15, 12);
        for (int i = 0; i < 28; ++i) {
            const unsigned char packed = bytes[offset + 2 + i / 2];
            int nibble = (i & 1) ? (packed >> 4) : (packed & 15);
            if (nibble >= 8) nibble -= 16;
            int sample = (nibble << 12) >> shift;
            sample += (h1 * coef[predictor][0] + h2 * coef[predictor][1] + 32) >> 6;
            sample = std::clamp(sample, -32768, 32767);
            h2 = h1; h1 = sample;
            out.monoSamples.push_back(static_cast<float>(sample) / 32768.0f);
        }
        if (bytes[offset + 1] & 1) break;
    }
    return out.sampleRate > 0 && !out.monoSamples.empty();
}

#ifdef _WIN32
template <typename T> void ReleaseCom(T*& value) {
    if (value) value->Release();
    value = nullptr;
}

bool DecodeMediaFoundation(const std::string& path, DecodedAudio& out, std::string& error) {
    const HRESULT comHr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const bool uninitCom = SUCCEEDED(comHr);
    HRESULT hr = MFStartup(MF_VERSION, MFSTARTUP_LITE);
    if (FAILED(hr)) {
        if (uninitCom) CoUninitialize();
        error = "Media Foundation startup failed";
        return false;
    }
    IMFSourceReader* reader = nullptr;
    IMFMediaType* requested = nullptr;
    IMFMediaType* actual = nullptr;
    hr = MFCreateSourceReaderFromURL(PathUtf8::FromString(path).c_str(), nullptr, &reader);
    if (SUCCEEDED(hr)) hr = MFCreateMediaType(&requested);
    if (SUCCEEDED(hr)) hr = requested->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
    if (SUCCEEDED(hr)) hr = requested->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM);
    if (SUCCEEDED(hr)) hr = reader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_AUDIO_STREAM, nullptr, requested);
    if (SUCCEEDED(hr)) hr = reader->GetCurrentMediaType(MF_SOURCE_READER_FIRST_AUDIO_STREAM, &actual);
    const UINT32 channels = SUCCEEDED(hr) ? MFGetAttributeUINT32(actual, MF_MT_AUDIO_NUM_CHANNELS, 0) : 0;
    const UINT32 sampleRate = SUCCEEDED(hr) ? MFGetAttributeUINT32(actual, MF_MT_AUDIO_SAMPLES_PER_SECOND, 0) : 0;
    const UINT32 bits = SUCCEEDED(hr) ? MFGetAttributeUINT32(actual, MF_MT_AUDIO_BITS_PER_SAMPLE, 16) : 0;
    if (FAILED(hr) || channels == 0 || sampleRate == 0 || bits != 16) {
        ReleaseCom(actual); ReleaseCom(requested); ReleaseCom(reader);
        MFShutdown(); if (uninitCom) CoUninitialize();
        error = "unsupported audio stream";
        return false;
    }
    out.sampleRate = sampleRate;
    while (true) {
        DWORD flags = 0;
        IMFSample* sample = nullptr;
        hr = reader->ReadSample(MF_SOURCE_READER_FIRST_AUDIO_STREAM, 0, nullptr, &flags, nullptr, &sample);
        if (FAILED(hr)) { ReleaseCom(sample); break; }
        if (sample) {
            IMFMediaBuffer* buffer = nullptr;
            if (SUCCEEDED(sample->ConvertToContiguousBuffer(&buffer))) {
                BYTE* data = nullptr;
                DWORD length = 0;
                if (SUCCEEDED(buffer->Lock(&data, nullptr, &length))) {
                    const int16_t* pcm = reinterpret_cast<const int16_t*>(data);
                    const size_t frames = length / (sizeof(int16_t) * channels);
                    out.monoSamples.reserve(out.monoSamples.size() + frames);
                    for (size_t frame = 0; frame < frames; ++frame) {
                        int sum = 0;
                        for (UINT32 ch = 0; ch < channels; ++ch) sum += pcm[frame * channels + ch];
                        out.monoSamples.push_back(static_cast<float>(sum) /
                                                  (32768.0f * static_cast<float>(channels)));
                    }
                    buffer->Unlock();
                }
                ReleaseCom(buffer);
            }
            ReleaseCom(sample);
        }
        if (flags & MF_SOURCE_READERF_ENDOFSTREAM) break;
    }
    ReleaseCom(actual); ReleaseCom(requested); ReleaseCom(reader);
    MFShutdown(); if (uninitCom) CoUninitialize();
    if (FAILED(hr) || out.monoSamples.empty()) { error = "audio decode failed"; return false; }
    return true;
}
#endif
} // namespace

bool DecodeAudioFile(const std::string& absolutePath, DecodedAudio& outAudio,
                     std::string& outError) {
    outAudio = {};
    outError.clear();
    std::string ext = PathUtf8::ToString(PathUtf8::FromString(absolutePath).extension());
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (ext == ".vag") return DecodeVag(absolutePath, outAudio, outError);
#ifdef _WIN32
    return DecodeMediaFoundation(absolutePath, outAudio, outError);
#else
    outError = "audio decoding unavailable";
    return false;
#endif
}
} // namespace MipsyncEngine
