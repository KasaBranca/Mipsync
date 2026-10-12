#pragma once
#include <cstdint>
#include <vector>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <mmsystem.h>
#endif
namespace MipsyncEngine {
// Owns a PCM buffer and its asynchronous device submission. No audio cache
// file is created when a protected player package is running.
class MemoryAudioVoice {
public:
    ~MemoryAudioVoice();
    bool Play(std::vector<int16_t> samples,uint32_t rate,bool loop,uint32_t channels=1);
    void Pause(bool paused);
    void SetVolume(int level);
    bool IsPlaying() const;
    uint64_t PositionMs() const;
private:
    std::vector<int16_t> pcm;
    uint32_t sampleRate=0;
    uint32_t channelCount=1;
    bool paused=false;
#ifdef _WIN32
    HWAVEOUT device=nullptr;
    WAVEHDR header{};
#endif
};
}
