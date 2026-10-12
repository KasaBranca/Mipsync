#include "../core/PreparationCancellation.h"
#include "../core/AssetFiles.h"
#include "MidiFile.h"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <array>
#include <cctype>

namespace MipsyncEngine {
namespace {
struct Reader {
    const std::vector<uint8_t>& b; size_t p, end;
    uint8_t byte() { if (p >= end) throw std::runtime_error("Truncated MIDI data"); return b[p++]; }
    uint32_t be(unsigned n) { uint32_t v=0; while(n--) v=(v<<8)|byte(); return v; }
    uint32_t vlq() { uint32_t v=0; for(int i=0;i<4;++i) { auto x=byte(); v=(v<<7)|(x&127); if(!(x&128)) return v; } throw std::runtime_error("Invalid MIDI variable-length value"); }
    void skip(size_t n) { if(n > end-p) throw std::runtime_error("Truncated MIDI event"); p+=n; }
};
struct Raw { uint64_t tick; uint32_t tempo; uint8_t s,a,b; };
}
bool IsMidiPath(const std::string& path) {
    auto pos=path.find_last_of('.'); auto ext=pos==std::string::npos ? "" : path.substr(pos);
    std::transform(ext.begin(),ext.end(),ext.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});
    return ext==".mid" || ext==".midi";
}
bool ReadMidi(const std::string& path, MidiSequence& out, std::string& error) {
    if (AssetFiles::IsPackedPath(std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(path.data()), path.size())))) {
        try { auto bytes=AssetFiles::Read(std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(path.data()), path.size()))); return ParseMidi(bytes,out,error); }
        catch(const std::exception& e){error=e.what();return false;}
    }
    std::ifstream f(std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(path.data()), path.size())),std::ios::binary);
    if(!f) { error="Cannot open MIDI file"; return false; }
    f.seekg(0,std::ios::end); auto size=f.tellg();
    if(size<0 || size>16*1024*1024) { error="MIDI file exceeds 16 MiB"; return false; }
    f.seekg(0); std::vector<uint8_t> bytes(static_cast<size_t>(size));
    if(!bytes.empty() && !f.read(reinterpret_cast<char*>(bytes.data()),bytes.size())) {error="Cannot read MIDI file";return false;}
    return ParseMidi(bytes,out,error);
}
bool ParseMidi(const std::vector<uint8_t>& bytes, MidiSequence& out, std::string& error) {
    out={}; error.clear();
    try {
        Reader r{bytes,0,bytes.size()};
        if(r.be(4)!=0x4d546864) throw std::runtime_error("Expected a Standard MIDI File (MThd)");
        auto header=r.be(4); if(header<6) throw std::runtime_error("Invalid MIDI header");
        auto format=r.be(2), tracks=r.be(2), division=r.be(2); r.skip(header-6);
        if(format>1 || !tracks || tracks>64 || (format==0 && tracks!=1)) throw std::runtime_error("SFX supports MIDI format 0 or 1, up to 64 tracks");
        if(!division || (division&0x8000)) throw std::runtime_error("SFX MIDI requires quarter-note (PPQ) timing");
        std::vector<Raw> raw; uint64_t endTick=0; bool notes=false;
        for(unsigned t=0;t<tracks;++t) {
            if(r.be(4)!=0x4d54726b) throw std::runtime_error("Expected MIDI track (MTrk)");
            auto len=r.be(4); if(len>r.end-r.p) throw std::runtime_error("Truncated MIDI track");
            Reader tr{bytes,r.p,r.p+len}; r.skip(len); uint64_t tick=0; uint8_t running=0;
            while(tr.p<tr.end) {
                tick+=tr.vlq(); if(tick>0xffffffffu) throw std::runtime_error("MIDI tick range exceeded");
                uint8_t s=tr.byte(), a=0,b=0; bool first=(s<128);
                if(first) { a=s; s=running; if(s<0x80 || s>=0xf0) throw std::runtime_error("Invalid MIDI running status"); }
                if(s==0xff) {
                    running=0; auto type=tr.byte(); auto count=tr.vlq();
                    if(type==0x51) { if(count!=3) throw std::runtime_error("Invalid MIDI tempo"); auto tempo=tr.be(3); if(!tempo) throw std::runtime_error("Zero MIDI tempo"); raw.push_back({tick,tempo,0,0,0}); }
                    else {tr.skip(count); if(type==0x2f) {if(count) throw std::runtime_error("Invalid end-of-track"); break;}}
                } else if(s==0xf0 || s==0xf7) {running=0; tr.skip(tr.vlq());}
                else {
                    if(s>=0xf0) throw std::runtime_error("Unsupported MIDI system event");
                    running=s; if(!first) a=tr.byte();
                    if((s&0xf0)!=0xc0 && (s&0xf0)!=0xd0) b=tr.byte();
                    if(a>=128 || b>=128) throw std::runtime_error("Invalid MIDI data byte");
                    raw.push_back({tick,0,s,a,b}); if((s&0xf0)==0x90 && b) notes=true;
                }
                if(raw.size()>65535) throw std::runtime_error("MIDI exceeds 65535 events");
            }
            endTick=std::max(endTick,tick);
        }
        if(!notes) throw std::runtime_error("MIDI contains no notes");
        std::stable_sort(raw.begin(),raw.end(),[](auto& a,auto& b){return a.tick<b.tick;});
        uint64_t last=0, us=0, remainder=0; uint32_t tempo=500000;
        auto advance=[&](uint64_t tick) {
            auto numerator=(tick-last)*tempo+remainder; us+=numerator/division; remainder=numerator%division; last=tick;
            if(us>600000000) throw std::runtime_error("SFX MIDI exceeds 10 minutes");
        };
        for(auto& e:raw) { advance(e.tick); if(e.tempo) tempo=e.tempo; else out.events.push_back({static_cast<uint32_t>(us),e.s,e.a,e.b}); }
        advance(endTick); out.durationUs=static_cast<uint32_t>(std::max<uint64_t>(us,1));
        return true;
    } catch(const std::exception& e) {out={};error=e.what();return false;}
}
float MidiTone(unsigned family, double phase) {
    double p=phase-std::floor(phase), sine=std::sin(p*6.283185307179586);
    switch(family&7) {
        case 0:return static_cast<float>((sine+0.25*std::sin(p*12.566370614))/1.25);
        case 1:return static_cast<float>((sine+0.4*std::sin(p*18.849555922))/1.4);
        case 2:return static_cast<float>(1.0-4.0*std::abs(p-0.5));
        case 3:return p<0.5?0.7f:-0.7f;
        case 4:return static_cast<float>((sine+0.3*std::sin(p*25.132741229))/1.3);
        case 5:return static_cast<float>((2*p-1)*0.7);
        case 6:return static_cast<float>(sine);
        default: {uint32_t x=static_cast<uint32_t>(p*112)+1; x^=x<<13; x^=x>>17; x^=x<<5; return static_cast<float>(static_cast<int>(x&65535)-32768)/32768.0f;}
    }
}
std::vector<int16_t> RenderMidi(const MidiSequence& midi, uint32_t rate) {
    CheckPreparationCancelled();
    struct Note {bool on=false, held=false; uint8_t ch=0,key=0,vel=0,program=0; double phase=0; float env=0;};
    std::array<Note,23> notes{}; std::array<uint8_t,16> program{}, vol{},expression{}, sustain{};
    std::array<int,16> bend{}; vol.fill(127); expression.fill(127); bend.fill(8192);
    size_t n=static_cast<size_t>((static_cast<uint64_t>(midi.durationUs)+100000)*rate/1000000);
    std::vector<int16_t> pcm(n); size_t event=0, steal=0;
    for(size_t i=0;i<n;++i) {
        if ((i & 1023u) == 0) CheckPreparationCancelled();
        uint64_t time=i*1000000ull/rate;
        while(event<midi.events.size() && midi.events[event].timeUs<=time) {
            auto e=midi.events[event++]; unsigned ch=e.status&15;
            switch(e.status&0xf0) {
                case 0xc0:program[ch]=e.data1;break;
                case 0xe0:bend[ch]=e.data1+(e.data2<<7);break;
                case 0xb0:
                    if(e.data1==7) vol[ch]=e.data2;
                    if(e.data1==11) expression[ch]=e.data2;
                    if(e.data1==64) {sustain[ch]=e.data2>=64; if(!sustain[ch]) for(auto&v:notes) if(v.ch==ch && !v.held)v.on=false;}
                    if(e.data1==120 || e.data1==123) for(auto& v:notes) if(v.ch==ch){v.on=false;v.held=false; if(e.data1==120)v.env=0;}
                    break;
                case 0x80:case 0x90:
                    if((e.status&0xf0)==0x80 || !e.data2) {for(auto&v:notes) if(v.ch==ch&&v.key==e.data1&&v.held){v.held=false;if(!sustain[ch])v.on=false;}}
                    else {auto it=std::find_if(notes.begin(),notes.end(),[](auto&v){return !v.on&&v.env<0.001f;}); auto&v=it==notes.end()?notes[(steal++)%notes.size()]:*it; v={true,true,static_cast<uint8_t>(ch),e.data1,e.data2,program[ch],0,0};}
                    break;
            }
        }
        double sample=0;
        for(auto&v:notes) {
            if(time>=midi.durationUs){v.on=false;v.held=false;}
            if(v.on)v.env=std::min(v.env+1.0f/(rate*0.003f),1.0f); else v.env=std::max(v.env-1.0f/(rate*0.035f),0.0f);
            if(v.env<=0)continue;
            double frequency=440*std::pow(2.0,(static_cast<int>(v.key)-69+(bend[v.ch]-8192)/4096.0)/12.0);
            sample+=MidiTone(v.ch==9?7:v.program/16,v.phase)*v.env*v.vel/127.0*vol[v.ch]/127.0*expression[v.ch]/127.0*0.23;
            v.phase+=frequency/rate;
        }
        pcm[i]=static_cast<int16_t>(std::clamp(sample,-0.999,0.999)*32767);
    }
    return pcm;
}
}
