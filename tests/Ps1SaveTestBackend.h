#pragma once
#include <map>
#include <array>
#include <cstring>
#include <algorithm>
static std::map<std::string,std::array<unsigned char,PS1_SAVE_BLOCK_BYTES>> testCard;
static int testCardFailAfter=-1;
static bool testCardUnavailable=false;
extern "C" int ps1_save_card_read(const char* file,void* out,unsigned size){
    auto it=testCard.find(file);
    if(testCardUnavailable||it==testCard.end()||size!=PS1_SAVE_BLOCK_BYTES)return 0;
    std::memcpy(out,it->second.data(),size);return 1;
}
extern "C" int ps1_save_card_write(const char* file,unsigned offset,const void* data,unsigned size,int create){
    if(testCardUnavailable||offset%128||size%128||offset+size>PS1_SAVE_BLOCK_BYTES)return 0;
    if(create){if(testCard.contains(file))return 0;testCard[file]={};}
    auto it=testCard.find(file);if(it==testCard.end())return 0;
    unsigned count=testCardFailAfter<0?size:std::min(size,static_cast<unsigned>(testCardFailAfter));
    std::memcpy(it->second.data()+offset,data,count);return count==size;
}

extern "C" void ps1_save_restore_handoff(void){}
extern "C" int ps1_save_prepare_handoff(void){return 1;}
extern "C" void ps1_save_cancel_handoff(void){}
