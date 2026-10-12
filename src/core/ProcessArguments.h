#pragma once
#include <string>
#include <vector>
#include <stdexcept>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shellapi.h>
#endif

namespace MipsyncEngine {
// Windows CRT argv uses the system ANSI code page. Engine paths and command
// payloads use UTF-8, so take arguments from the Unicode process command line.
inline std::vector<std::string> ReadProcessArguments(int argc,char** argv) {
    std::vector<std::string> result;
#ifdef _WIN32
    int count=0;
    wchar_t** wide=CommandLineToArgvW(GetCommandLineW(),&count);
    if(!wide)throw std::runtime_error("Cannot read process command line");
    try {
        for(int i=0;i<count;++i) {
            int length=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,wide[i],-1,nullptr,0,nullptr,nullptr);
            if(length<=0)throw std::runtime_error("Invalid Unicode command line argument");
            std::string text(length,'\0');
            if(!WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,wide[i],-1,text.data(),length,nullptr,nullptr))throw std::runtime_error("Cannot convert command line argument");
            text.pop_back();result.push_back(std::move(text));
        }
    }catch(...){LocalFree(wide);throw;}
    LocalFree(wide);
#else
    for(int i=0;i<argc;++i)result.emplace_back(argv[i]);
#endif
    return result;
}
}
