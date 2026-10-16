#include <cstdio>
#include <cstdlib>
extern "C" {
#include "scene.h"
#include "physics.h"
}
static ps1_entity makeEntity(unsigned id, int parent = -1, bool trigger = false) {
    ps1_entity e{}; e.id = id; e.parent_entity_index = parent;
    e.rigid_root_entity_index = -1; e.scale[0] = e.scale[1] = e.scale[2] = FIX16_ONE;
    e.collider_shape = 0; e.collider_is_trigger = trigger;
    e.collider_half_extents[0] = e.collider_half_extents[1] = e.collider_half_extents[2] = FIX16_ONE;
    e.source_entity_id = id + 100; return e;
}
static const ps1_entity entities[] = {makeEntity(1, -1, true), makeEntity(2, 0), makeEntity(3), makeEntity(4)};
static ps1_scene makeScene() {
    ps1_scene s{}; s.entities = entities; s.entity_count = 4; s.camera_entity_index = -1; return s;
}
extern "C" const ps1_scene g_ps1_scene = makeScene();
extern "C" const ps1_mesh g_ps1_meshes[1] = {};
extern "C" const unsigned int g_ps1_mesh_count = 0;
static void check(bool value, const char* message) {
    if (!value) { std::fprintf(stderr, "%s\n", message); std::exit(1); }
}
int main() {
    ps1_scene_init(); ps1_physics_begin_frame();
    check(ps1_physics_trigger_overlap(0, 2), "PS1 trigger enter overlap was missed");
    auto* player = ps1_scene_mutable_entity(2);
    player->position[0] = 5 * FIX16_ONE;
    check(!ps1_physics_trigger_overlap(0, 2), "PS1 trigger exit overlap persisted");
    player->position[0] = 0;
    ps1_scene_request_destroy(0); ps1_scene_request_destroy(0); ps1_scene_request_destroy(99999);
    check(ps1_scene_destroy_pending(0) && ps1_scene_destroy_pending(1), "PS1 descendants not queued");
    check(!ps1_physics_trigger_overlap(0, 2), "PS1 pending target received another trigger");
    int callbacks = 0, index;
    while ((index = ps1_scene_next_destroy()) >= 0) {
        check(ps1_scene_entity(index) != nullptr, "PS1 OnDestroy lost entity before callback");
        ++callbacks;
        if (index == 0) ps1_scene_request_destroy(3); // OnDestroy may destroy another object.
        ps1_scene_finish_destroy(index);
    }
    check(callbacks == 3 && ps1_scene_next_destroy() == -1, "PS1 duplicate/chained OnDestroy failed");
    check(!ps1_scene_entity(0) && !ps1_scene_mutable_entity(1) && !ps1_scene_entity(3) && ps1_scene_entity(2),
        "PS1 tombstones corrupted stable entity indices or survivor");
    check(ps1_scene_source_id_destroyed(101) && !ps1_scene_source_id_destroyed(103), "PS1 UI source visibility wrong");
    // Static collider cache was populated before destruction. It must not ground
    // the survivor against old AABBs belonging to destroyed entities.
    ps1_physics_set_active_entity(2);
    check(!ps1_physics_is_grounded_within(FIX16_ONE), "PS1 destroyed collider remains in static cache");
    ps1_scene_init();
    check(ps1_scene_entity(0) && !ps1_scene_destroy_pending(1), "PS1 scene reload retained tombstones");
    std::puts("PS1 destroy/trigger hierarchy, deferred callbacks, cached colliders and reset passed");
}
