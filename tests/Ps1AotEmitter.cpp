#include "mips/Lexer.h"
#include "mips/Parser.h"
#include "mips/Compiler.h"
#include "mips/PS1Export.h"
#include <fstream>
#include <filesystem>
#include <iostream>
#include <stdexcept>
using namespace MipsyncEngine::Mips;
static CompiledModule Compile(const std::string& text) {
    Lexer lexer(text); auto tokens=lexer.Tokenize();
    if(!lexer.GetErrors().empty())throw std::runtime_error("fixture lexer error");
    Parser parser(tokens);auto ast=parser.ParseProgram();
    if(!parser.GetErrors().empty())throw std::runtime_error("fixture parser error: "+parser.GetErrors().front());
    Compiler compiler(*ast->classes[0],ast.get());auto module=compiler.Compile();
    if(!compiler.GetErrors().empty())throw std::runtime_error(compiler.GetErrors().front());
    return *module;
}
static std::string Read(const std::filesystem::path& path){std::ifstream f(path);if(!f)throw std::runtime_error("missing fixture");return {std::istreambuf_iterator<char>(f),{}};}
int main(int argc,char**argv){try{
    if(argc!=3)throw std::runtime_error("output C and fixture directory required");
    std::vector<CompiledModule> modules;
    modules.push_back(Compile(Read(std::filesystem::path(argv[2])/"ObjectReferences.mips")));
    modules.push_back(Compile("class Health : MipsBehaviour { public float hp = 7; public float x = 3; }"));
    modules.push_back(Compile(Read(std::filesystem::path(argv[2])/"ArraysCoroutines.mips")));
    modules.push_back(Compile(Read(std::filesystem::path(argv[2])/"NativeParity.mips")));
    std::string error;
    if(!ValidatePs1Target(modules,4,error))throw std::runtime_error(error);
    ScriptsDataEmit stats;
    if(!EmitScriptsDataC(modules,argv[1],stats,error))throw std::runtime_error(error);
    std::string generated=Read(argv[1]);
    if(generated.find("k_mbc_")!=std::string::npos || generated.find("vm_execute")!=std::string::npos)
        throw std::runtime_error("native export contains interpreted script data");
    std::vector<CompiledModule> many(32,modules[1]);
    if(!ValidatePs1Target(many,32,error))throw std::runtime_error("native export retained the four-module VM limit");
    // Corrupt branch targets must fail at export, before generating invalid C.
    auto bad=Compile("class Bad : MipsBehaviour { void Update(){if(true)Log.Info(1);} }");
    bool checked=false;
    for(size_t i=0;i+4<bad.methods[0].code.size();++i) if(bad.methods[0].code[i]==static_cast<unsigned>(OpCode::JumpIfFalse)){
        for(int b=0;b<4;++b)bad.methods[0].code[i+1+b]=255;
        checked=true;break;
    }
    if(!checked || EmitScriptsDataC({bad},std::string(argv[1])+".invalid",stats,error))throw std::runtime_error("invalid native branch accepted");
    std::cout<<"Generated native C for 4 modules; malformed branches rejected.\n";return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
