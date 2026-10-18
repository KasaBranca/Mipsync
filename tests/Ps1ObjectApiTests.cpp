#include "mips/Lexer.h"
#include "mips/Parser.h"
#include "mips/Compiler.h"
#include "mips/PS1Export.h"
#include <fstream>
#include <iostream>
#include <cmath>
#include <stdexcept>
#include <array>
extern "C" {
#include "vm.h"
#include "scene.h"
#include "physics.h"
#include "input.h"
#include "audio.h"
#include "ui.h"
#include "scene_loader.h"
int FntPrint(int,const char*,...){return 0;}
int ps1_input_key_held(const char*){return 0;} int ps1_input_key_down(const char*){return 0;}
int ps1_input_key_up(const char*){return 0;} int32_t ps1_input_axis_q16(const char*){return 0;}
int32_t ps1_input_look_delta_x_q16(){return 0;} int32_t ps1_input_look_delta_y_q16(){return 0;}
int ps1_audio_play(unsigned){return 0;} void ps1_audio_stop(unsigned){} void ps1_audio_pause(unsigned){}
void ps1_audio_unpause(unsigned){} int ps1_audio_is_playing(unsigned){return 0;} void ps1_audio_apply_entity(unsigned){}
int ps1_ui_set_text_visible(uint32_t,int){return 0;} int ps1_ui_set_image_visible(uint32_t,int){return 0;}
int ps1_ui_set_text(uint32_t,const char*){return 0;} int ps1_ui_set_color(uint32_t,uint8_t,uint8_t,uint8_t,uint8_t){return 0;}
int ps1_scene_request_load_path(const char*){return 0;} int ps1_scene_request_load_index(int){return 0;}
}
static ps1_entity makeEntity(unsigned id,const char* name,fix16_t x,fix16_t y){
    ps1_entity e{};e.id=id;e.name=name;e.parent_entity_index=-1;e.rigid_root_entity_index=-1;
    e.position[0]=x;e.position[1]=y;e.scale[0]=e.scale[1]=e.scale[2]=FIX16_ONE;e.collider_shape=-1;return e;
}
static std::array<ps1_entity,150> makeEntities(){
    std::array<ps1_entity,150> result{};
    for(unsigned i=0;i<150;++i)result[i]=makeEntity(i+2,"Static map",0,0);
    result[0]=makeEntity(1,"Owner",10*FIX16_ONE,0);
    result[149]=makeEntity(2,"Target",13*FIX16_ONE,4*FIX16_ONE);return result;
}
static const auto entities=makeEntities();
static ps1_scene makeScene(){ps1_scene s{};s.entities=entities.data();s.entity_count=150;s.camera_entity_index=-1;return s;}
extern "C" const ps1_scene g_ps1_scene=makeScene();
extern "C" const ps1_mesh g_ps1_meshes[1]={};
extern "C" const unsigned g_ps1_mesh_count=0;
extern "C" const unsigned g_ps1_audio_clip_count=0;
static void check(bool ok,const char* msg){if(!ok)throw std::runtime_error(msg);}
static std::unique_ptr<MipsyncEngine::Mips::CompiledModule> compile(const std::string& source){
    using namespace MipsyncEngine::Mips;
    Lexer lexer(source);auto tokens=lexer.Tokenize();check(lexer.GetErrors().empty(),"PS1 fixture lexer failed");
    Parser parser(tokens);auto ast=parser.ParseProgram();check(parser.GetErrors().empty(),"PS1 fixture parser failed");
    Compiler compiler(*ast->classes[0],ast.get());auto module=compiler.Compile();
    if(!compiler.GetErrors().empty())throw std::runtime_error(compiler.GetErrors().front());return module;
}
int main(int argc,char** argv){try{
    using namespace MipsyncEngine::Mips;
    check(argc==2,"fixture path required");std::ifstream f(argv[1]);std::string source{std::istreambuf_iterator<char>(f),{}};
    auto module=compile(source), health=compile("class Health : MipsBehaviour { public float hp = 7; public float x = 3; }");
    std::string error;if(!ValidatePs1Target({*module,*health},2,error))throw std::runtime_error(error);
    auto data=EncodeMbc(*module),hd=EncodeMbc(*health);
    static mbc_module decoded[2];check(mbc_decode(data.data(),data.size(),&decoded[0]),"PS1 MBC decode failed");
    check(mbc_decode(hd.data(),hd.size(),&decoded[1]),"PS1 Health decode failed");
    static script_instance instances[2];static vm_state vm{};
    vm.instances=instances;vm.instance_count=2;ps1_scene_init();host_init();
    for(int i=0;i<2;++i){instances[i].module=&decoded[i];instances[i].entity_index=i?149:0;
        for(unsigned j=0;j<decoded[i].field_count;++j){auto value=decoded[i].numbers[decoded[i].fields[j].default_const];
            if(i==0 && (std::string(decoded[i].fields[j].name)=="target"||std::string(decoded[i].fields[j].name)=="targetTransform"))value=2*FIX16_ONE;
            instances[i].fields[j]=value;instances[i].runtime_fields[j].tag=HOST_VAL_NUMBER;instances[i].runtime_fields[j].ival=value;
        }
    }
    check(vm_run_method(&vm,&decoded[0],&instances[0],mbc_find_method(&decoded[0],"Start")),"PS1 object reference execution failed");
    auto value=[&](const char* name){int field=module->FindFieldIndex(name);auto v=instances[0].runtime_fields[field];return v.tag==HOST_VAL_BOOL?(v.ival?1.:0.):v.ival/65536.;};
    check(value("referencesOK")==1 && value("health")==7,"PS1 lookup/null/equality/GetComponent mismatch");
    check(std::abs(value("before")-5)<.001 && std::abs(value("after")-10)<.001,"PS1 distance/transform mismatch");
    auto* owner=ps1_scene_mutable_entity(0);auto* target=ps1_scene_mutable_entity(149);
    check(target!=nullptr,"late static target was not promoted to a mutable slot");
    owner->collider_shape=target->collider_shape=0;
    for(int axis=0;axis<3;++axis)owner->collider_half_extents[axis]=target->collider_half_extents[axis]=FIX16_ONE;
    target->position[0]=owner->position[0]+2*FIX16_ONE;target->position[1]=owner->position[1];
    check(ps1_physics_contact_kind(0,149)==1,"solid touching faces must produce a collision contact");
    target->collider_is_trigger=1;target->position[0]-=FIX16_ONE;
    check(ps1_physics_contact_kind(0,149)==2,"trigger contact classified as solid");
    target->position[0]+=10*FIX16_ONE;check(ps1_physics_contact_kind(0,149)==0,"exit contact did not clear");
    ps1_scene_request_destroy(149);ps1_scene_finish_destroy(149);
    check(vm_run_method(&vm,&decoded[0],&instances[0],mbc_find_method(&decoded[0],"Update")) && value("cleared")==1,"PS1 destroyed reference not null");
    std::cout<<"PS1 production VM: references, components, world vectors, distance and destroyed references passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
