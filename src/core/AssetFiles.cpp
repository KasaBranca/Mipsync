#include "AssetFiles.h"
#include "NativePaths.h"
#include "../assets/AssetManager.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cstring>
#include <cctype>
#include <fstream>
#include <limits>
#include <stdexcept>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <bcrypt.h>
#endif

namespace MipsyncEngine::AssetFiles {
namespace {
namespace fs = std::filesystem;
using Json = nlohmann::json;
fs::path mountedRoot;
Entries mounted;
constexpr uint8_t marker[16] = {0x4d,0x50,0x4b,0x59,0x37,0xa2,0xd9,0x14,0x83,0xf5,0x61,0xb6,0x29,0x7c,0xe8,0x40};
struct KeySlot { uint8_t tag[16]; uint8_t masked[32]; uint8_t mask[32]; };
// A build-specific random key is bound to the copied player executable.
// Splitting the key avoids putting it as a clear key next to the package.
volatile const KeySlot playerKey = {{0x4d,0x50,0x4b,0x59,0x37,0xa2,0xd9,0x14,0x83,0xf5,0x61,0xb6,0x29,0x7c,0xe8,0x40},{},{}};
constexpr size_t maxPackBytes = 1024ull*1024*1024;
std::string EntryName(const fs::path& p) {
    if (p.empty() || p.is_absolute() || p.has_root_name()) throw std::runtime_error("Invalid package path");
    for (const auto& part:p) if(part==".." || part==".") throw std::runtime_error("Invalid package path");
    auto s=PathUtf8::ToString(p.lexically_normal());
    std::replace(s.begin(),s.end(),'\\','/');
#ifdef _WIN32
    std::transform(s.begin(),s.end(),s.begin(),[](unsigned char c){return c<128?static_cast<char>(std::tolower(c)):static_cast<char>(c);});
#endif
    return s;
}
std::string Lookup(const fs::path& p) {
    if(mountedRoot.empty())return {};
    auto rel=fs::absolute(p).lexically_normal().lexically_relative(mountedRoot);
    if(rel.empty() || rel==".")return {};
    for(const auto& part:rel)if(part=="..")return {};
    return EntryName(rel);
}
Bytes DiskRead(const fs::path& p) {
    std::ifstream f(NativePaths::FilePath(p),std::ios::binary|std::ios::ate);
    if(!f)throw std::runtime_error("Cannot open asset: "+PathUtf8::ToString(p));
    auto length=f.tellg();
    if(length<0 || static_cast<uint64_t>(length)>maxPackBytes)throw std::runtime_error("Asset exceeds package size limit");
    Bytes b(static_cast<size_t>(length));f.seekg(0);
    if(!b.empty() && !f.read(reinterpret_cast<char*>(b.data()),b.size()))throw std::runtime_error("Cannot read asset");
    return b;
}
void DiskWrite(const fs::path& p,const Bytes& b) {
    std::ofstream f(NativePaths::FilePath(p),std::ios::binary|std::ios::trunc);
    if(!f || !f.write(reinterpret_cast<const char*>(b.data()),b.size()))throw std::runtime_error("Cannot write player package");
    f.close();if(!f)throw std::runtime_error("Cannot finish player package");
}
#ifdef _WIN32
void Check(NTSTATUS status) { if(status<0)throw std::runtime_error("Player package cryptography failed"); }
struct Algorithm { BCRYPT_ALG_HANDLE h=nullptr; ~Algorithm(){if(h)BCryptCloseAlgorithmProvider(h,0);} };
struct CipherKey { BCRYPT_KEY_HANDLE h=nullptr; ~CipherKey(){if(h)BCryptDestroyKey(h);} };
Bytes Crypt(bool encrypt,const Bytes& input,const Key& key,Bytes& header) {
    Algorithm alg;Check(BCryptOpenAlgorithmProvider(&alg.h,BCRYPT_AES_ALGORITHM,nullptr,0));
    Check(BCryptSetProperty(alg.h,BCRYPT_CHAINING_MODE,reinterpret_cast<PUCHAR>(const_cast<wchar_t*>(BCRYPT_CHAIN_MODE_GCM)),sizeof(BCRYPT_CHAIN_MODE_GCM),0));
    ULONG objectBytes=0,got=0;Check(BCryptGetProperty(alg.h,BCRYPT_OBJECT_LENGTH,reinterpret_cast<PUCHAR>(&objectBytes),sizeof(objectBytes),&got,0));
    Bytes object(objectBytes);CipherKey k;
    Check(BCryptGenerateSymmetricKey(alg.h,&k.h,object.data(),objectBytes,const_cast<PUCHAR>(key.data()),key.size(),0));
    BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO auth;BCRYPT_INIT_AUTH_MODE_INFO(auth);
    auth.pbNonce=header.data()+8;auth.cbNonce=12;
    auth.pbTag=header.data()+20;auth.cbTag=16;
    auth.pbAuthData=header.data();auth.cbAuthData=20;
    Bytes out(input.size());ULONG written=0;
    auto status=encrypt?BCryptEncrypt(k.h,const_cast<PUCHAR>(input.data()),input.size(),&auth,nullptr,0,out.data(),out.size(),&written,0)
                       :BCryptDecrypt(k.h,const_cast<PUCHAR>(input.data()),input.size(),&auth,nullptr,0,out.data(),out.size(),&written,0);
    if(status<0)throw std::runtime_error("Player package authentication failed (wrong player or damaged data)");
    out.resize(written);return out;
}
#else
Bytes Crypt(bool,const Bytes&,const Key&,Bytes&) {throw std::runtime_error("Encrypted Windows player packages require Windows");}
#endif
}
bool IsPackedPath(const fs::path& p) {return !Lookup(p).empty();}
bool Exists(const fs::path& p) {
    const auto name=Lookup(p);if(!name.empty())return mounted.count(name)!=0;
    std::error_code ec;return fs::is_regular_file(NativePaths::FilePath(p),ec);
}
Bytes Read(const fs::path& p) {
    const auto name=Lookup(p);if(name.empty())return DiskRead(p);
    auto it=mounted.find(name);if(it==mounted.end())throw std::runtime_error("Asset absent from player package: "+name);
    return it->second;
}
std::string ReadText(const fs::path& p) {auto b=Read(p);return std::string(b.begin(),b.end());}
void PutMemoryFile(const fs::path& p,Bytes b) {
    auto name=Lookup(p);if(name.empty())throw std::runtime_error("Memory asset must belong to mounted player");
    mounted[name]=std::move(b);
}
Key RandomKey() {
    Key key{};
#ifdef _WIN32
    Check(BCryptGenRandom(nullptr,key.data(),key.size(),BCRYPT_USE_SYSTEM_PREFERRED_RNG));
#else
    throw std::runtime_error("Windows player packaging requires Windows");
#endif
    return key;
}
bool WritePack(const fs::path& p,const Entries& entries,const Key& key,std::string& error) {
    try {
        Json files=Json::object();for(const auto& [name,b]:entries) {
            auto normalized=EntryName(PathUtf8::FromString(name));
            if(files.contains(normalized))throw std::runtime_error("Duplicate player package path");
            files[normalized]=Json::binary(b);
        }
        auto payload=Json::to_cbor(files);if(payload.size()>maxPackBytes-36)throw std::runtime_error("Player package exceeds 1 GiB");
        Bytes header(36);std::memcpy(header.data(),"MIPPAK01",8);
#ifdef _WIN32
        Check(BCryptGenRandom(nullptr,header.data()+8,12,BCRYPT_USE_SYSTEM_PREFERRED_RNG));
#endif
        auto cipher=Crypt(true,payload,key,header);header.insert(header.end(),cipher.begin(),cipher.end());
        DiskWrite(p,header);error.clear();return true;
    }catch(const std::exception& e){error=e.what();return false;}
}
bool Mount(const fs::path& p,const Key& key,std::string& error) {
    Unmount();
    try {
        auto bytes=DiskRead(p);
        if(bytes.size()<37 || std::memcmp(bytes.data(),"MIPPAK01",8))throw std::runtime_error("Invalid player package header");
        Bytes header(bytes.begin(),bytes.begin()+36),cipher(bytes.begin()+36,bytes.end());
        auto plain=Crypt(false,cipher,key,header);auto files=Json::from_cbor(plain);
        if(!files.is_object() || files.size()>100000)throw std::runtime_error("Invalid player package index");
        Entries next;for(auto it=files.begin();it!=files.end();++it) {
            auto name=EntryName(PathUtf8::FromString(it.key()));
            if(!it->is_binary() || !next.emplace(name,it->get_binary()).second)throw std::runtime_error("Invalid player package entry");
        }
        if(!next.count("boot.json") || !next.count("project/nostalty.project"))throw std::runtime_error("Incomplete player package");
        mounted=std::move(next);mountedRoot=fs::absolute(p.parent_path()).lexically_normal();error.clear();return true;
    }catch(const std::exception& e){Unmount();error=e.what();return false;}
}
void Unmount(){mounted.clear();mountedRoot.clear();}
bool MountPlayer(const fs::path& dir,std::string& error) {
    Key key{};for(size_t i=0;i<key.size();++i)key[i]=playerKey.masked[i]^playerKey.mask[i];
    return Mount(dir/"game.mpak",key,error);
}
bool BindPlayerKey(const fs::path& exe,const Key& key,std::string& error) {
    try {
        auto b=DiskRead(exe);size_t slot=0,count=0;
        for(size_t i=0;i+sizeof(KeySlot)<=b.size();++i)if(!std::memcmp(b.data()+i,marker,16)){
            // Only an unbound slot is patched; marker constants in code are ignored.
            bool empty=true;for(size_t j=16;j<sizeof(KeySlot);++j)empty&=b[i+j]==0;
            if(empty){slot=i;++count;}
        }
        if(count!=1)throw std::runtime_error("Player executable has no unique unbound package key slot");
        auto mask=RandomKey();for(size_t i=0;i<key.size();++i){b[slot+16+i]=key[i]^mask[i];b[slot+48+i]=mask[i];}
        DiskWrite(exe,b);error.clear();return true;
    }catch(const std::exception& e){error=e.what();return false;}
}
}
