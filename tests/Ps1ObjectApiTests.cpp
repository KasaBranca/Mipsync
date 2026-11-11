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
#include "save.h"
#include "application.h"
#include "ui.h"
#include "scene_loader.h"
#ifdef MIPSYNC_SCRIPT_AOT
#include "scripts_data.h"
#endif
int FntPrint(int,const char*,...){return 0;}
void ps1_platform_quit(void){}
int ps1_input_key_held(const char*){return 0;} int ps1_input_key_down(const char*){return 0;}
int ps1_input_key_up(const char*){return 0;} int32_t ps1_input_axis_q16(const char*){return 0;}
int32_t ps1_input_look_delta_x_q16(){return 0;} int32_t ps1_input_look_delta_y_q16(){return 0;}
int ps1_audio_play(unsigned){return 0;} void ps1_audio_stop(unsigned){} void ps1_audio_pause(unsigned){}
void ps1_audio_unpause(unsigned){} int ps1_audio_is_playing(unsigned){return 0;} void ps1_audio_apply_entity(unsigned){}
int ps1_ui_set_text_visible(uint32_t,int){return 0;} int ps1_ui_set_image_visible(uint32_t,int){return 0;}
int ps1_ui_set_text(uint32_t,const char*){return 0;} int ps1_ui_set_color(uint32_t,uint8_t,uint8_t,uint8_t,uint8_t){return 0;}
int ps1_scene_request_load_path(const char*){return 0;} int ps1_scene_request_load_index(int){return 0;}
}
#include "Ps1SaveTestBackend.h"
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
extern "C" const unsigned g_ps1_audio_clip_count=2;
extern "C" const ps1_audio_clip g_ps1_audio_clips[2]={{nullptr,0,0,nullptr,nullptr,"assets/audio/one.wav"},{nullptr,0,0,nullptr,nullptr,"assets/audio/two.wav"}};
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
#ifdef MIPSYNC_SCRIPT_AOT
    mbc_module* decoded=g_mipsync_scripts;
    check(g_mipsync_script_count==4,"native module table missing");
#else
    static mbc_module decoded[2];check(mbc_decode(data.data(),data.size(),&decoded[0]),"PS1 MBC decode failed");
    check(mbc_decode(hd.data(),hd.size(),&decoded[1]),"PS1 Health decode failed");
#endif
    static script_instance instances[2];static vm_state vm{};
    vm.instances=instances;vm.instance_count=2;ps1_scene_init();host_init();
    for(int i=0;i<2;++i){instances[i].module=&decoded[i];instances[i].entity_index=i?149:0;
        for(unsigned j=0;j<decoded[i].field_count;++j){auto value=ToFixed16((i?health:module)->numberConstants[(i?health:module)->fields[j].defaultConstIndex]);
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
#ifdef MIPSYNC_SCRIPT_AOT
    static script_instance extra[2];
    for(unsigned n=0;n<2;++n){
        auto& instance=extra[n]; instance.module=&decoded[n+2]; instance.entity_index=0;
        check(vm_run_method(&vm,instance.module,&instance,mbc_find_method(instance.module,"Start")),"native control flow failed");
        if(n==0){
            check(instance.runtime_fields[0].ival==31*FIX16_ONE,"native arrays mismatch");
            for(int frame=0;frame<20;++frame)vm_resume_coroutines(&vm,instance.module,&instance,FIX16_ONE/60);
            check(instance.runtime_fields[0].ival==3142*FIX16_ONE,"native coroutine resume mismatch");
        }else{
            check(instance.runtime_fields[0].ival==8*FIX16_ONE && instance.runtime_fields[1].ival,"native branches/loops/operators mismatch");
            check(vm_run_method(&vm,instance.module,&instance,mbc_find_method(instance.module,"OnDestroy")),"native lifecycle failed");
            check(instance.runtime_fields[0].ival==108*FIX16_ONE,"native lifecycle result mismatch");
            instance.runtime_fields[5].tag=HOST_VAL_NUMBER;instance.runtime_fields[5].ival=FIX16_ONE;
            check(vm_run_method(&vm,instance.module,&instance,mbc_find_method(instance.module,"ApiParity")),"native API execution failed");
            check(instance.runtime_fields[2].ival,"native Save/string/component/audio APIs diverged");
            check(ps1_application_quit_requested(),"Application.Quit was ignored");
            for(int frame=0;frame<200;++frame)check(vm_run_method(&vm,instance.module,&instance,mbc_find_method(instance.module,"AllocationStep")),"transient native values exhausted capacity");
            check(vm_run_method(&vm,instance.module,&instance,mbc_find_method(instance.module,"AllocationCheck"))&&instance.runtime_fields[2].ival,"native GC damaged live field strings or arrays");
            owner->mesh=PS1_MESH_CUBE;owner->mesh_enabled=1;owner->mesh_size=2*FIX16_ONE;
            owner->position[0]=owner->position[1]=0;owner->position[2]=5*FIX16_ONE;
            owner->scale[0]=owner->scale[1]=owner->scale[2]=FIX16_ONE;
            fix16_t origin[3]={0},direction[3]={0,0,3*FIX16_ONE};
            check(ps1_physics_raycast(origin,direction,6*FIX16_ONE)==1,"normalized raycast missed mesh without collider");
            check(ps1_physics_raycast(origin,direction,3*FIX16_ONE)==0,"raycast ignored max distance");
            origin[2]=5*FIX16_ONE;check(ps1_physics_raycast(origin,direction,2*FIX16_ONE)==1,"raycast inside bounds mismatch");
            direction[2]=0;check(ps1_physics_raycast(origin,direction,2*FIX16_ONE)==0,"zero direction raycast hit");
            ps1_application_process_quit();check(!ps1_application_quit_requested(),"Quit not consumed at frame boundary");
        }
    }
    check(sizeof(mbc_module)<64,"native image still reserves decoded constant pools");
    std::cout<<"Native C: arrays, coroutines, branches, operators and lifecycle passed\n";
#endif
    std::cout<<"PS1 script runtime: references, components, world vectors, distance and destroyed references passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
