#include "MemoryAudioVoice.h"
#include <algorithm>
#include <limits>
#include <utility>
namespace MipsyncEngine {
MemoryAudioVoice::~MemoryAudioVoice() {
#ifdef _WIN32
    if(device){waveOutReset(device);waveOutUnprepareHeader(device,&header,sizeof(header));waveOutClose(device);}
#endif
}
bool MemoryAudioVoice::Play(std::vector<int16_t> samples,uint32_t rate,bool loop,uint32_t channels) {
#ifdef _WIN32
    if(device || samples.empty() || !rate || channels<1 || channels>8 || samples.size()>std::numeric_limits<DWORD>::max()/2)return false;
    pcm=std::move(samples);sampleRate=rate;channelCount=channels;
    WAVEFORMATEX f{};f.wFormatTag=WAVE_FORMAT_PCM;f.nChannels=channels;f.nSamplesPerSec=rate;f.wBitsPerSample=16;f.nBlockAlign=channels*2;f.nAvgBytesPerSec=rate*f.nBlockAlign;
    if(waveOutOpen(&device,WAVE_MAPPER,&f,0,0,CALLBACK_NULL)!=MMSYSERR_NOERROR){device=nullptr;return false;}
    header.lpData=reinterpret_cast<char*>(pcm.data());header.dwBufferLength=static_cast<DWORD>(pcm.size()*2);
    if(loop){header.dwFlags=WHDR_BEGINLOOP|WHDR_ENDLOOP;header.dwLoops=0xffffffff;}
    if(waveOutPrepareHeader(device,&header,sizeof(header))!=MMSYSERR_NOERROR)return false;
    return waveOutWrite(device,&header,sizeof(header))==MMSYSERR_NOERROR;
#else
    return false;
#endif
}
void MemoryAudioVoice::Pause(bool value) {
    paused=value;
#ifdef _WIN32
    if(device){if(value)waveOutPause(device);else waveOutRestart(device);}
#endif
}
void MemoryAudioVoice::SetVolume(int level) {
#ifdef _WIN32
    if(device){DWORD v=static_cast<DWORD>(std::clamp(level,0,1000))*65535/1000;waveOutSetVolume(device,v|(v<<16));}
#endif
}
bool MemoryAudioVoice::IsPlaying() const {
#ifdef _WIN32
    return device && !paused && !(header.dwFlags&WHDR_DONE);
#else
    return false;
#endif
}
uint64_t MemoryAudioVoice::PositionMs() const {
#ifdef _WIN32
    if(!device || !sampleRate)return 0;
    MMTIME t{};t.wType=TIME_SAMPLES;
    if(waveOutGetPosition(device,&t,sizeof(t))!=MMSYSERR_NOERROR)return 0;
    uint64_t samples=t.wType==TIME_SAMPLES?t.u.sample:(t.wType==TIME_BYTES?t.u.cb/(2*channelCount):0);
    if(!pcm.empty())samples%=pcm.size()/channelCount;return samples*1000/sampleRate;
#else
    return 0;
#endif
}
}
