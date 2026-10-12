#include "audio/MidiFile.h"
#include <iostream>
#include <stdexcept>
#include <cmath>
using namespace MipsyncEngine;
static void check(bool ok, const char* message) { if(!ok)throw std::runtime_error(message); }
static std::vector<uint8_t> file(const std::vector<std::vector<uint8_t>>& tracks) {
    std::vector<uint8_t> b={'M','T','h','d',0,0,0,6,0,static_cast<uint8_t>(tracks.size()>1),0,static_cast<uint8_t>(tracks.size()),0,96};
    for(auto& t:tracks){b.insert(b.end(),{'M','T','r','k',0,0,static_cast<uint8_t>(t.size()>>8),static_cast<uint8_t>(t.size())});b.insert(b.end(),t.begin(),t.end());}return b;
}
int main() {
    try {
        MidiSequence m;std::string error;
        auto bytes=file({{0,0xc0,10,0,0x90,72,100,96,72,0,0,0xff,0x2f,0}});
        check(ParseMidi(bytes,m,error),error.c_str());
        check(m.events.size()==3 && m.events[2].timeUs==500000 && m.durationUs==500000,"Running status / PPQ timing");
        auto pcm=RenderMidi(m);int peak=0;
        for(auto s:pcm)peak=std::max(peak,std::abs(int(s)));
        check(peak>2000 && pcm.back()==0,"Audible note with bounded release tail");
        bytes=file({{0,0xff,0x51,3,7,0xa1,0x20,96,0xff,0x51,3,3,0xd0,0x90,96,0xff,0x2f,0},
                    {0,0x90,60,100,0x81,0x40,0x80,60,0,0,0xff,0x2f,0}});
        check(ParseMidi(bytes,m,error),error.c_str());
        check(m.events.size()==2 && m.events[1].timeUs==750000 && m.durationUs==750000,"Merged tracks / tempo changes / VLQ");
        for(size_t i=0;i<bytes.size();++i) {
            auto truncated=std::vector<uint8_t>(bytes.begin(),bytes.begin()+i);
            check(!ParseMidi(truncated,m,error),"Truncated file was accepted");
        }
        check(!ParseMidi(file({{0,60,100}}),m,error),"Running status before status byte");
        check(!ParseMidi(file({{0x80,0x80,0x80,0x80,0,0x90,60,100}}),m,error),"Overlong VLQ");
        check(!ParseMidi(file({{0,0xff,0x51,3,0,0,0}}),m,error),"Zero tempo");
        check(!ParseMidi(file({{0,0x90,60,255}}),m,error),"Invalid data byte");
        check(!ParseMidi(file({{0,0xff,0x2f,0}}),m,error),"No notes");
        bytes=file({{0,0x90,60,100,1,0x80,60,0}});bytes[13]=0;
        check(!ParseMidi(bytes,m,error),"Zero division");
        check(IsMidiPath("assets/Test.MIDI") && !IsMidiPath("theme.mp3"),"MIDI extension detection");
        std::cout<<"MIDI parser, tempo merge, preview synthesis and malformed-input checks passed\n";
        return 0;
    } catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
