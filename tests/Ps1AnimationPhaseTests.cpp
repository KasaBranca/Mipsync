#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
extern "C" {
#include "scene.h"
}
static uint16_t hash(const char* name) {
    uint32_t value=2166136261u;while(*name){value^=uint8_t(*name++);value*=16777619u;}
    value^=value>>16;return (value&65535u)?uint16_t(value):1;
}
static ps1_entity root() {
    ps1_entity e{};e.parent_entity_index=-1;e.rigid_root_entity_index=-1;
    e.scale[0]=e.scale[1]=e.scale[2]=FIX16_ONE;return e;
}
static ps1_entity vertex() {
    auto e=root();e.rigid_root_entity_index=0;e.mesh_enabled=1;e.mesh=PS1_MESH_CUSTOM;
    e.vertex_idle_first_mesh_index=e.vertex_anim_first_mesh_index=1;
    e.vertex_idle_frame_count=e.vertex_anim_frame_count=4;e.vertex_idle_fps=e.vertex_anim_fps=8;
    e.vertex_walk_first_mesh_index=11;e.vertex_walk_frame_count=9;e.vertex_walk_fps=10;
    e.animator_state_hashes[0]=hash("Rest");e.animator_state_hashes[1]=hash("Stride");
    e.animator_state_hashes[3]=hash("Punch");e.animator_trigger_parameter_hash=hash("Punch");
    e.animator_action_condition=1;e.animator_action_threshold=FIX16_ONE/2;
    e.vertex_trigger_first_mesh_index=30;e.vertex_trigger_frame_count=4;e.vertex_trigger_fps=8;
    return e;
}
static ps1_entity rigid() {
    auto e=root();e.rigid_root_entity_index=2;
    e.rigid_idle_frame_count=e.rigid_anim_frame_count=4;e.rigid_idle_fps=e.rigid_anim_fps=8;
    e.rigid_walk_first_frame=4;e.rigid_walk_frame_count=9;e.rigid_walk_fps=10;
    e.animator_state_hashes[0]=hash("Rest");e.animator_state_hashes[1]=hash("Stride");
    e.animator_state_hashes[3]=hash("Punch");e.animator_trigger_parameter_hash=hash("Punch");
    e.animator_action_condition=1;e.animator_action_threshold=FIX16_ONE/2;
    e.rigid_trigger_first_frame=13;e.rigid_trigger_frame_count=4;e.rigid_trigger_fps=8;
    return e;
}
static const ps1_entity entities[]={root(),vertex(),root(),rigid()};
static const ps1_rigid_anim_frame frames[20]{};
static ps1_scene makeScene() {
    ps1_scene s{};s.entities=entities;s.entity_count=4;s.camera_entity_index=-1;
    s.rigid_anim_frames=frames;s.rigid_anim_frame_count=20;return s;
}
extern "C" const ps1_scene g_ps1_scene=makeScene();
static void check(bool value,const char* message){if(!value){std::fprintf(stderr,"%s\n",message);std::exit(1);}}
int main() {
    static_assert(PS1_MAX_FIELD_VALUES==VM_FIELD_CAP && VM_FIELD_CAP>=39,
        "scene binding fields and VM instance fields must share capacity");
    ps1_scene_init();
    check(ps1_scene_animator_normalized_time(0,"Stride")==-FIX16_ONE,"inactive clip must not emit contacts");
    ps1_scene_set_animator_float(0,"Speed",FIX16_ONE);
    ps1_scene_set_animator_float(2,"Speed",FIX16_ONE);
    ps1_scene_update_vertex_anims(FIX16_ONE/3);
    const auto* v=ps1_scene_entity(1);const auto* r=ps1_scene_entity(3);
    double phase=ps1_scene_animator_normalized_time(0,"Stride")/65536.0;
    double visible=(v->mesh_index-v->vertex_anim_first_mesh_index+v->vertex_anim_lerp_q8/256.0)/v->vertex_anim_frame_count;
    check(std::abs(phase-visible)<0.001,"vertex sound clock must match the rendered pose");
    phase=ps1_scene_animator_normalized_time(2,"Stride")/65536.0;
    visible=(r->rigid_anim_current_frame+r->rigid_anim_lerp_q8/256.0)/r->rigid_anim_frame_count;
    check(std::abs(phase-visible)<0.001,"rigid sound clock must match the rendered pose");
    ps1_scene_update_vertex_anims(FIX16_ONE);
    phase=ps1_scene_animator_normalized_time(0,"Stride")/65536.0;
    check(phase>0.48 && phase<0.49,"phase must wrap with the animation loop");
    ps1_scene_set_animator_float(0,"Speed",0);
    check(ps1_scene_animator_normalized_time(0,"Stride")==-FIX16_ONE,"stopping must disable locomotion contacts");
    check(ps1_scene_animator_normalized_time(0,"Rest")==0,"clip switch must reset the sampled phase");
    check(ps1_scene_animator_normalized_time(999,"Stride")==-FIX16_ONE,"missing animator must be safe");
    for(unsigned int rootIndex:{0u,2u}) {
        ps1_scene_init();
        ps1_scene_set_animator_float(rootIndex,"Punch",FIX16_ONE/4);
        check(ps1_scene_animator_normalized_time(rootIndex,"Punch")==-FIX16_ONE,"action threshold must be respected");
        ps1_scene_set_animator_float(rootIndex,"Punch",FIX16_ONE);
        check(ps1_scene_animator_normalized_time(rootIndex,"Punch")==0,"custom float must start action");
        ps1_scene_update_vertex_anims(FIX16_ONE/8);
        const auto phaseBefore=ps1_scene_animator_normalized_time(rootIndex,"Punch");
        ps1_scene_set_animator_float(rootIndex,"Punch",FIX16_ONE);
        check(ps1_scene_animator_normalized_time(rootIndex,"Punch")==phaseBefore,"held parameter must not restart the action");
        ps1_scene_set_animator_float(rootIndex,"Punch",0);
        check(ps1_scene_animator_normalized_time(rootIndex,"Punch")>=0,"resetting the parameter must not cancel one-shot");
        ps1_scene_update_vertex_anims(FIX16_ONE);
        check(ps1_scene_animator_normalized_time(rootIndex,"Punch")==-FIX16_ONE,"one-shot must return to idle");
        ps1_scene_set_animator_float(rootIndex,"Punch",FIX16_ONE);
        check(ps1_scene_animator_normalized_time(rootIndex,"Punch")==0,"next press must restart action");
    }
    std::puts("PS1 vertex/rigid animation phase, loop wrapping, state filtering and shared field capacity passed.");
}
