#include <iostream>
#include <stdexcept>
#include <string>
extern "C" {
#include "save.h"
#include "save_handoff.h"
}
#include "Ps1SaveTestBackend.h"
static void check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
static int number(const char* key,int type){fix16_t n=0;check(ps1_save_get(key,type,&n,nullptr),"missing save key");return n;}
int main(){try{
    check(ps1_save_handoff_address(0x9390)==0x80009390u,"physical BIOS allocation rejected");
    check(ps1_save_handoff_address(0x80009390)==0x80009390u,"cached BIOS allocation rejected");
    check(ps1_save_handoff_address(0xa0009390)==0x80009390u,"uncached BIOS allocation rejected");
    check(!ps1_save_handoff_address(0)&&!ps1_save_handoff_address(0x8000ff00)&&!ps1_save_handoff_address(0x80010000),"invalid kernel allocation accepted");
    ps1_save_init("Test Company/Game");
    check(ps1_save_set("coins",PS1_SAVE_INT,7*FIX16_ONE,nullptr),"set int");
    check(ps1_save_set("speed",PS1_SAVE_FLOAT,FIX16_ONE*3/2,nullptr),"set float");
    check(ps1_save_set("door",PS1_SAVE_BOOL,FIX16_ONE,nullptr),"set bool");
    check(ps1_save_set("note",PS1_SAVE_STRING,0,"room 2: note collected"),"set string");
    std::array<unsigned char,PS1_SAVE_SNAPSHOT_BYTES> snapshot{};
    check(ps1_save_snapshot(snapshot.data(),snapshot.size())==snapshot.size(),"scene snapshot encode");
    ps1_save_init("Test Company/Game");check(ps1_save_restore_snapshot(snapshot.data(),snapshot.size()),"scene handoff restore");
    check(number("coins",PS1_SAVE_INT)==7*FIX16_ONE,"scene handoff lost cached values");
    check(ps1_save_write("slot1.json"),"first card write");
    check(testCard.size()==1,"one slot must use one card block");
    auto& bytes=testCard.begin()->second;check(bytes[0]=='S'&&bytes[1]=='C'&&bytes[3]==1,"BIOS save header");
    ps1_save_init("Test Company/Game");check(ps1_save_read("slot1.json"),"read after restart");
    check(number("coins",PS1_SAVE_INT)==7*FIX16_ONE,"integer round trip");
    check(number("coins",PS1_SAVE_FLOAT)==7*FIX16_ONE,"numeric coercion");
    check(number("speed",PS1_SAVE_FLOAT)==FIX16_ONE*3/2,"float round trip");
    check(!ps1_save_get("speed",PS1_SAVE_INT,nullptr,nullptr),"int getter must reject floats");
    const char* text=nullptr;check(ps1_save_get("note",PS1_SAVE_STRING,nullptr,&text)&&std::string(text)=="room 2: note collected","string round trip");
    ps1_save_set("coins",PS1_SAVE_INT,8*FIX16_ONE,nullptr);check(ps1_save_write("slot1.json"),"second bank write");
    ps1_save_set("coins",PS1_SAVE_INT,9*FIX16_ONE,nullptr);testCardFailAfter=128;
    check(!ps1_save_write("slot1.json"),"interrupted write reported success");testCardFailAfter=-1;
    check(ps1_save_read("slot1.json")&&number("coins",PS1_SAVE_INT)==8*FIX16_ONE,"interruption destroyed previous save");
    bytes[4352+300]^=1;
    ps1_save_set("coins",PS1_SAVE_INT,77*FIX16_ONE,nullptr);
    check(!ps1_save_read("slot1.json")&&number("coins",PS1_SAVE_INT)==77*FIX16_ONE,"corrupt save changed live data");
    testCardUnavailable=true;check(!ps1_save_write("new.json"),"missing/full card succeeded");testCardUnavailable=false;
    ps1_save_init("Other Game");check(!ps1_save_read("slot1.json"),"game namespace collision");
    std::string key(48,'a');check(!ps1_save_set(key.c_str(),PS1_SAVE_INT,1,nullptr),"long key truncated");
    for(int i=0;i<PS1_SAVE_ENTRIES;++i)check(ps1_save_set(std::to_string(i).c_str(),PS1_SAVE_INT,i,nullptr),"entry capacity");
    check(!ps1_save_set("overflow",PS1_SAVE_INT,1,nullptr),"save capacity silently exceeded");
    std::cout<<"PS1 Save: types, restart, BIOS block format, interrupted writes, corruption, namespace and capacity passed\n";return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
