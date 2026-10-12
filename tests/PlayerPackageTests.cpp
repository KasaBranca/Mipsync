#include "core/AssetFiles.h"
#include "core/Log.h"
#include "assets/AssetManager.h"
#include "build/PlayerBuild.h"
#include "mips/MipsScriptLoader.h"
#include "audio/AudioDecoder.h"
#include "audio/MemoryAudioVoice.h"
#include "assets/Material.h"
#include <thread>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <algorithm>
using namespace MipsyncEngine;
namespace fs=std::filesystem;
void Check(bool value,const std::string& message){if(!value)throw std::runtime_error(message);}
void Write(const fs::path& path,const std::string& text){fs::create_directories(path.parent_path());std::ofstream(path,std::ios::binary)<<text;}
int main(int argc,char** argv) {
    fs::path work=fs::absolute("Testing/player-package");
    try {
        Log::Init();
        fs::create_directories(work);
        auto key=AssetFiles::RandomKey();
        AssetFiles::Entries files={{"boot.json",{'{','}'}},{"Project/nostalty.project",{'{','}'}},
            {"Project/assets/秘密.txt",{'s','e','c','r','e','t'}},{"Project/assets/empty",{}}};
        AssetFiles::Bytes wav(44+512*2);
        auto put=[&](size_t offset,uint32_t v,size_t count){for(size_t i=0;i<count;++i)wav[offset+i]=static_cast<uint8_t>(v>>(i*8));};
        std::copy_n("RIFF",4,wav.begin());put(4,wav.size()-8,4);
        std::copy_n("WAVEfmt ",8,wav.begin()+8);put(16,16,4);put(20,1,2);put(22,1,2);put(24,22050,4);put(28,44100,4);put(32,2,2);put(34,16,2);std::copy_n("data",4,wav.begin()+36);put(40,1024,4);
        files["Project/assets/test.wav"]=wav;
        std::string error;
        Check(AssetFiles::WritePack(work/"a.mpak",files,key,error),error);
        Check(AssetFiles::WritePack(work/"b.mpak",files,key,error),error);
        auto encrypted=AssetFiles::Read(work/"a.mpak");
        Check(encrypted!=AssetFiles::Read(work/"b.mpak"),"Nonce must be fresh");
        std::string raw(encrypted.begin(),encrypted.end());
        Check(raw.find("secret")==std::string::npos && raw.find("nostalty.project")==std::string::npos,"Plaintext leaked");
        Write(work/"Project/assets/not-packed.txt","loose fallback");
        Check(AssetFiles::Mount(work/"a.mpak",key,error),error);
        Check(AssetFiles::ReadText(work/PathUtf8::FromString("Project/assets/秘密.txt"))=="secret","Unicode read failed");
        Check(AssetFiles::Read(work/"Project/assets/empty").empty(),"Empty entry failed");
        DecodedAudio decoded;
        Check(DecodeAudioFile(PathUtf8::ToString(work/"Project/assets/test.wav"),decoded,error),"Memory audio decoder: "+error);
        Check(decoded.sampleRate==22050 && decoded.monoSamples.size()==512,"Memory audio decoding changed samples");
        Material legacy; legacy.color={.2f,.4f,.6f,1.f};
        auto migratedPath=work/"Project/assets/materials/Migrated.nmat";
        Check(Material::Save(PathUtf8::ToString(migratedPath),legacy,error),error);
        Check(!fs::exists(migratedPath),"Runtime material migration leaked a loose asset");
        Material restored;
        Check(Material::Load(PathUtf8::ToString(migratedPath),restored,error) && restored.color==legacy.color,"Memory material migration failed");
        MemoryAudioVoice voice;
        Check(voice.Play(std::vector<int16_t>(4410*2,0),44100,true,2),"PCM loop could not start");
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
        Check(voice.IsPlaying(),"PCM loop stopped after its first buffer");
        voice.SetVolume(250);voice.Pause(true);Check(!voice.IsPlaying(),"PCM pause failed");
        voice.Pause(false);Check(voice.IsPlaying(),"PCM resume failed");
        Check(!AssetFiles::Exists(work/"Project/assets/not-packed.txt"),"Loose fallback must be blocked");
        bool rejected=false;try{AssetFiles::Read(work/"Project/assets/not-packed.txt");}catch(...){rejected=true;}
        Check(rejected,"Missing entry must fail closed");AssetFiles::Unmount();
        Check(!AssetFiles::Mount(work/"a.mpak",AssetFiles::RandomKey(),error),"Wrong key accepted");
        encrypted.back()^=1;std::ofstream damaged(work/"damaged.mpak",std::ios::binary);damaged.write(reinterpret_cast<const char*>(encrypted.data()),encrypted.size());damaged.close();
        Check(!AssetFiles::Mount(work/"damaged.mpak",key,error),"Tampered package accepted");
        files["../escaped"]={1};Check(!AssetFiles::WritePack(work/"invalid.mpak",files,key,error),"Traversal accepted");

        auto project=work/"Input";Write(project/"nostalty.project","{}");
        Write(project/"scenes/start.nscene","{\"version\":1,\"entities\":[]}");
        const std::string script="class Player : MipsBehaviour { public float speed=3; public bool active=true; public AudioClip clip; public float[] samples; public Entity target; void Update(){ speed=speed+Time.deltaTime; } }";
        Write(project/"assets/scripts/Player.mips",script);
        Write(project/"assets/authoring.blend","excluded authoring");
        Write(project/"assets/tool.exe","excluded executable");
        Write(project/"assets/secret.env","excluded credentials");
        Write(project/"assets/icon.png","runtime texture sentinel");
        std::vector<std::string> errors;
        auto module=Mips::MipsScriptLoader::CompileFile(project,"assets/scripts/Player.mips",errors);
        Check(module && errors.empty(),"Script fixture did not compile");
        auto bytes=Mips::SerializePlayerModule(*module);auto round=Mips::DeserializePlayerModule(bytes);
        Check(round->className==module->className && round->fields.size()==module->fields.size() && round->methods.size()==module->methods.size(),"Bytecode metadata lost");
        Check(round->numberConstants==module->numberConstants && round->stringConstants==module->stringConstants && round->nameConstants==module->nameConstants && round->methods.front().code==module->methods.front().code,"Bytecode data changed");
        Check(std::search(bytes.begin(),bytes.end(),script.begin(),script.end())==bytes.end(),"Source text retained");
        Check(argc>1,"Engine path required");
        PlayerBuildRequest request;request.projectPath=PathUtf8::ToString(project);request.outputParent=PathUtf8::ToString(work/"Output");request.engineDirectory=PathUtf8::ToString(fs::absolute(argv[1]).parent_path());request.settings.productName="PackageTest";request.settings.scenesInBuild={"scenes/start.nscene"};
        Write(work/"Output/PackageTest/PackageTest_Data/Project/assets/old.png","legacy loose asset");
        fs::copy_file(fs::absolute(argv[1]),work/"Output/PackageTest/PackageTest.exe",fs::copy_options::overwrite_existing);
        auto result=PlayerBuild::BuildWindows(request);Check(result.success,result.message);
        auto output=PathUtf8::FromString(result.outputDirectory);
        Check(!fs::exists(output/"PackageTest_Data/Project"),"Legacy sources retained");
        Check(fs::exists(output/"PackageTest_Data/game.mpak"),"Encrypted package missing");
        // Recover the test's own bound key to inspect build contents.
        auto exe=AssetFiles::Read(PathUtf8::FromString(result.executablePath));
        const AssetFiles::Bytes marker={0x4d,0x50,0x4b,0x59,0x37,0xa2,0xd9,0x14,0x83,0xf5,0x61,0xb6,0x29,0x7c,0xe8,0x40};
        bool mounted=false;
        for(size_t i=0;i+80<=exe.size();++i)if(std::equal(marker.begin(),marker.end(),exe.begin()+i)) {
            AssetFiles::Key bound{};for(size_t j=0;j<32;++j)bound[j]=exe[i+16+j]^exe[i+48+j];
            if(AssetFiles::Mount(output/"PackageTest_Data/game.mpak",bound,error)){mounted=true;break;}
        }
        Check(mounted,"Bound player key could not authenticate");auto virtualRoot=output/"PackageTest_Data/Project";
        Check(AssetFiles::Exists(virtualRoot/"assets/icon.png"),"Runtime asset missing");
        for(const char* excluded:{"assets/authoring.blend","assets/tool.exe","assets/secret.env"})Check(!AssetFiles::Exists(virtualRoot/excluded),"Excluded content packaged");
        errors.clear();auto compiled=Mips::MipsScriptLoader::CompileFile(virtualRoot,"assets/scripts/Player.mips",errors);
        Check(compiled && errors.empty() && compiled->methods.front().code==module->methods.front().code,"Packed script load failed");AssetFiles::Unmount();
        request.settings.scenesInBuild={"../outside.nscene"};auto failed=PlayerBuild::BuildWindows(request);Check(!failed.success && fs::exists(output/"PackageTest_Data/game.mpak"),"Failed build damaged existing output");
        std::cout<<"Player package tests passed: encryption, authentication, isolation, bytecode, source exclusion and rebuild.\n";
        return 0;
    }catch(const std::exception& e){AssetFiles::Unmount();std::cerr<<e.what()<<'\n';return 1;}
}
