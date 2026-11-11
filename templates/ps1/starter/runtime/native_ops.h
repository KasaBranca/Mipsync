#ifndef MIPSYNC_NATIVE_OPS_H
#define MIPSYNC_NATIVE_OPS_H
#ifndef MIPSYNC_SCRIPT_AOT
#error "Native script code requires MIPSYNC_SCRIPT_AOT=1"
#endif
/* Shared value operations used by generated native C. No instruction decoder. */
#include "vm.h"
#include "scene.h"
#include "audio.h"
#include <string.h>
static inline int name_equals(const char* a,const char* b) { return a && b && strcmp(a,b)==0; }
static inline fix16_t* entity_vec3(ps1_entity* e,uint8_t member) { return e?(member==0?e->position:member==1?e->rotation:e->scale):0; }
static inline host_value make_host(host_ref_kind kind, uint16_t entity_idx, uint8_t vec_member) {
    host_value v = { HOST_VAL_HOST, 0, 0, { kind, entity_idx, vec_member } };
    return v;
}

static inline fix16_t val_as_num(host_value v) {
    if (v.tag == HOST_VAL_NUMBER) return (fix16_t)v.ival;
    if (v.tag == HOST_VAL_BOOL)   return v.ival ? FIX16_ONE : 0;
    return 0;
}

static inline int val_as_bool(host_value v) {
    if (v.tag == HOST_VAL_BOOL)   return v.ival ? 1 : 0;
    if (v.tag == HOST_VAL_NUMBER) return v.ival != 0;
    if (v.tag == HOST_VAL_STRING) return 1;
    if (v.tag == HOST_VAL_HOST)   return vm_entity_index(v) >= 0;
    if (v.tag == HOST_VAL_VEC3 || v.tag == HOST_VAL_NATIVE_STRING) return 1;
    if (v.tag == HOST_VAL_ARRAY)  return 1;
    return 0;
}

static inline host_value make_num(fix16_t f) { host_value v = { HOST_VAL_NUMBER, (int32_t)f, 0 }; return v; }
static inline host_value make_bool(int b)    { host_value v = { HOST_VAL_BOOL,   b ? 1 : 0, 0 }; return v; }
static inline host_value make_nil(void)      { host_value v = { HOST_VAL_NIL,    0,         0 }; return v; }

static inline vm_array* value_array(script_instance* inst, host_value value) {
    int index;
    if (!inst || value.tag != HOST_VAL_ARRAY || value.ival <= 0) return 0;
    index = value.ival - 1;
    if (index < 0 || index >= inst->array_count || index >= VM_ARRAY_CAP) return 0;
    return &inst->arrays[index];
}




static inline void native_push_field(vm_state* vm, uint16_t idx) {
    script_instance* inst = vm->instance;
    mbc_module* module = vm->module;
    do {

                vm_push(vm, idx < VM_FIELD_CAP ? inst->runtime_fields[idx] : make_nil());
                break;
            } while (0);
}

static inline void native_set_field(vm_state* vm, uint16_t idx) {
    script_instance* inst = vm->instance;
    mbc_module* module = vm->module;
    do {

                host_value top = vm_pop(vm);
                if (idx < VM_FIELD_CAP) {
                    inst->runtime_fields[idx] = top;
                    if (top.tag == HOST_VAL_NUMBER || top.tag == HOST_VAL_BOOL)
                        inst->fields[idx] = val_as_num(top);
                }
                break;
            } while (0);
}

static inline void native_push_local(vm_state* vm, uint16_t idx) {
    script_instance* inst = vm->instance;
    mbc_module* module = vm->module;
    do {

                vm_push(vm, idx < VM_LOCAL_CAP ? vm->locals[idx] : make_nil());
                break;
            } while (0);
}

static inline void native_set_local(vm_state* vm, uint16_t idx) {
    script_instance* inst = vm->instance;
    mbc_module* module = vm->module;
    do {

                host_value top = vm_pop(vm);
                if (idx < VM_LOCAL_CAP) vm->locals[idx] = top;
                break;
            } while (0);
}

static inline void native_pop(vm_state* vm) { script_instance* inst=vm->instance; do { (void)vm_pop(vm);
                break; } while(0); }

static inline void native_get_global(vm_state* vm, const char* name) {
    script_instance* inst = vm->instance;
    mbc_module* module = vm->module;
    do {


                if (name_equals(name, "transform"))
                    vm_push(vm, make_host(HOST_REF_TRANSFORM, vm->instance->entity_index, 0));
                else if (name_equals(name, "gameObject"))
                    vm_push(vm, make_host(HOST_REF_ENTITY, vm->instance->entity_index, 0));
                else if (name_equals(name, "AudioSource"))
                    vm_push(vm, make_host(HOST_REF_AUDIO_SOURCE, vm->instance->entity_index, 0));
                else
                    vm_push(vm, make_nil());
                break;
            } while (0);
}

static inline void native_get_component_on(vm_state* vm, const char* className, int targeted) {
    script_instance* inst = vm->instance;
    mbc_module* module = vm->module;
    do {


                int target = targeted ? vm_entity_index(vm_pop(vm)) : (int)inst->entity_index;
                host_value result = make_nil();
                unsigned int i;
                if (target >= 0 && ps1_scene_entity((unsigned)target)) {
                    if (name_equals(className, "Transform")) result = make_host(HOST_REF_TRANSFORM,(uint16_t)target,0);
                    else if (name_equals(className, "AudioSource")) result = make_host(HOST_REF_AUDIO_SOURCE,(uint16_t)target,0);
                    else if (name_equals(className, "Animator")) result = make_host(HOST_REF_ANIMATOR,(uint16_t)target,0);
                    else if (name_equals(className, "Collider")) result = make_host(HOST_REF_COLLIDER,(uint16_t)target,0);
                    else if (name_equals(className, "Rigidbody")) result = make_host(HOST_REF_RIGIDBODY,(uint16_t)target,0);
                    else for (i=0;i<vm->instance_count;++i)
                        if (vm->instances[i].entity_index == target && name_equals(vm->instances[i].module->class_name,className)) {
                            result = make_host(HOST_REF_SCRIPT,(uint16_t)target,(uint8_t)i); break;
                        }
                }
                vm_push(vm,result); break;
            } while (0);
}

static inline void native_get_member(vm_state* vm, const char* member) {
    script_instance* inst = vm->instance;
    mbc_module* module = vm->module;
    do {


                host_value obj = vm_pop(vm);
                if(obj.tag==HOST_VAL_HOST && (name_equals(member,"gameObject")||name_equals(member,"transform")) &&
                   ps1_scene_entity(obj.ref.entity_idx)) {
                    vm_push(vm,make_host(name_equals(member,"gameObject")?HOST_REF_ENTITY:HOST_REF_TRANSFORM,obj.ref.entity_idx,0));
                    break;
                }
                if (obj.tag == HOST_VAL_HOST && obj.ref.kind == HOST_REF_TRANSFORM) {
                    if (name_equals(member, "position")) {
                        vm_push(vm, make_host(HOST_REF_VEC3, obj.ref.entity_idx, 0));
                    } else if (name_equals(member, "rotation")) {
                        vm_push(vm, make_host(HOST_REF_VEC3, obj.ref.entity_idx, 1));
                    } else if (name_equals(member, "scale")) {
                        vm_push(vm, make_host(HOST_REF_VEC3, obj.ref.entity_idx, 2));
                    } else if (name_equals(member, "worldPosition")) {
                        vm_push(vm, make_host(HOST_REF_WORLD_VEC3, obj.ref.entity_idx, 0));
                    } else {
                        vm_push(vm, make_nil());
                    }
                } else if (obj.tag == HOST_VAL_HOST && obj.ref.kind == HOST_REF_ENTITY) {
                    if (name_equals(member, "id")) {
                        const ps1_entity* ent = ps1_scene_entity(obj.ref.entity_idx);
                        vm_push(vm, make_num(ent ? FIX16_FROM_INT((int32_t)ent->id) : 0));
                    } else if (name_equals(member,"transform") && ps1_scene_entity(obj.ref.entity_idx)) {
                        vm_push(vm,make_host(HOST_REF_TRANSFORM,obj.ref.entity_idx,0));
                    } else if (name_equals(member,"name")) {
                        const ps1_entity* e=ps1_scene_entity(obj.ref.entity_idx); host_value value=make_nil();
                        if(e){value.tag=HOST_VAL_NATIVE_STRING;value.text=e->name;} vm_push(vm,value);
                    } else {
                        vm_push(vm, make_nil());
                    }
                } else if (obj.tag == HOST_VAL_HOST && obj.ref.kind == HOST_REF_SCRIPT) {
                    host_value result=make_nil();
                    if (obj.ref.vec_member < vm->instance_count && ps1_scene_entity(obj.ref.entity_idx)) {
                        script_instance* target=&vm->instances[obj.ref.vec_member]; unsigned int i;
                        if(name_equals(member,"transform")) result=make_host(HOST_REF_TRANSFORM,obj.ref.entity_idx,0);
                        else if(name_equals(member,"gameObject")) result=make_host(HOST_REF_ENTITY,obj.ref.entity_idx,0);
                        else for(i=0;i<target->module->field_count;++i)
                            if(name_equals(member,target->module->fields[i].name)){result=target->runtime_fields[i];break;}
                    }
                    vm_push(vm,result);
                } else if (obj.tag == HOST_VAL_HOST && obj.ref.kind == HOST_REF_AUDIO_SOURCE) {
                    const ps1_entity* ent = ps1_scene_entity(obj.ref.entity_idx);
                    if (!ent) {
                        vm_push(vm, make_nil());
                    } else if (name_equals(member, "clip")) {
                        host_value value={0};value.tag=HOST_VAL_NATIVE_STRING;value.text=ps1_audio_clip_path(ent->audio_clip_index);vm_push(vm,value);
                    } else if (name_equals(member, "volume")) {
                        vm_push(vm, make_num((fix16_t)(((int32_t)ent->audio_volume_q8 * FIX16_ONE) / 255)));
                    } else if (name_equals(member, "loop")) {
                        vm_push(vm, make_bool(ent->audio_loop));
                    } else if (name_equals(member, "mute")) {
                        vm_push(vm, make_bool(ent->audio_mute));
                    } else if (name_equals(member, "playOnAwake")) {
                        vm_push(vm, make_bool(ent->audio_play_on_awake));
                    } else if (name_equals(member, "enabled")) {
                        vm_push(vm, make_bool(ent->audio_enabled));
                    } else if (name_equals(member, "isPlaying")) {
                        vm_push(vm, make_bool(ps1_audio_is_playing(obj.ref.entity_idx)));
                    } else {
                        vm_push(vm, make_nil());
                    }
                } else {
                    vm_push(vm, make_nil());
                }
                break;
            } while (0);
}

static inline void native_get_vec3_axis(vm_state* vm, uint8_t axis) {
    script_instance* inst = vm->instance;
    mbc_module* module = vm->module;
    do {
                 fix16_t vector[3]={0,0,0};
                host_value value=vm_pop(vm);
                if(value.tag==HOST_VAL_HOST && value.ref.kind==HOST_REF_SCRIPT && value.ref.vec_member<vm->instance_count &&
                   ps1_scene_entity(value.ref.entity_idx)) {
                    script_instance* target=&vm->instances[value.ref.vec_member];unsigned int i;host_value result=make_nil();
                    for(i=0;i<target->module->field_count;++i)
                        if(name_equals(target->module->fields[i].name,axis==0?"x":axis==1?"y":"z")){result=target->runtime_fields[i];break;}
                    vm_push(vm,result);break;
                }
                vm_read_vec3(value,vector); vm_push(vm,make_num(axis<3?vector[axis]:0)); break;
            } while (0);
}

static inline void native_set_vec3_axis(vm_state* vm, uint8_t axis) {
    script_instance* inst = vm->instance;
    mbc_module* module = vm->module;
    do {
                /* Stack: [value, vec3Host] (vec3Host on top). Matches desktop VM. */

                host_value vecHost = vm_pop(vm);
                host_value val     = vm_pop(vm);
                if (vecHost.tag == HOST_VAL_HOST &&
                    (vecHost.ref.kind == HOST_REF_VEC3 || vecHost.ref.kind == HOST_REF_WORLD_VEC3) &&
                    axis < 3) {
                    ps1_entity* ent = ps1_scene_mutable_entity(vecHost.ref.entity_idx);
                    fix16_t* vec = entity_vec3(ent, vecHost.ref.vec_member);
                    if (vec) vec[axis] = val_as_num(val);
                    else { host_log("[VM] mutable entity capacity exhausted"); vm->error=1; }
                }
                break;
            } while (0);
}

static inline void native_set_vec3_from_value(vm_state* vm) {
    script_instance* inst = vm->instance;
    mbc_module* module = vm->module;
    do {
                host_value target=vm_pop(vm), source=vm_pop(vm); fix16_t vector[3];
                if(target.tag==HOST_VAL_HOST && vm_read_vec3(source,vector)) {
                    ps1_entity* entity=ps1_scene_mutable_entity(target.ref.entity_idx); unsigned int axis;
                    fix16_t* dest=entity?(target.ref.vec_member==1?entity->rotation:target.ref.vec_member==2?entity->scale:entity->position):0;
                    if(dest)for(axis=0;axis<3;++axis)dest[axis]=vector[axis];
                    else { host_log("[VM] mutable entity capacity exhausted"); vm->error=1; }
                }
                break;
            } while (0);
}

static inline void native_add(vm_state* vm) {
    script_instance* inst = vm->instance;
    mbc_module* module = vm->module;
    do { host_value b = vm_pop(vm); host_value a = vm_pop(vm); vm_push(vm, vm_add_values(vm,a,b)); break; } while (0);
}

static inline void native_sub(vm_state* vm) {
    script_instance* inst = vm->instance;
    mbc_module* module = vm->module;
    do { host_value b = vm_pop(vm); host_value a = vm_pop(vm); vm_push(vm, make_num(fix16_sub(val_as_num(a), val_as_num(b)))); break; } while (0);
}

static inline void native_mul(vm_state* vm) {
    script_instance* inst = vm->instance;
    mbc_module* module = vm->module;
    do { host_value b = vm_pop(vm); host_value a = vm_pop(vm); vm_push(vm, make_num(fix16_mul(val_as_num(a), val_as_num(b)))); break; } while (0);
}

static inline void native_div(vm_state* vm) {
    script_instance* inst = vm->instance;
    mbc_module* module = vm->module;
    do { host_value b = vm_pop(vm); host_value a = vm_pop(vm); vm_push(vm, make_num(fix16_div(val_as_num(a), val_as_num(b)))); break; } while (0);
}

static inline void native_mod(vm_state* vm) {
    script_instance* inst = vm->instance;
    mbc_module* module = vm->module;
    do {
                host_value b = vm_pop(vm); host_value a = vm_pop(vm);
                fix16_t av = val_as_num(a), bv = val_as_num(b);
                vm_push(vm, make_num(bv == 0 ? 0 : (fix16_t)(av % bv)));
                break;
            } while (0);
}

static inline void native_neg(vm_state* vm) {
    script_instance* inst = vm->instance;
    mbc_module* module = vm->module;
    do { host_value a = vm_pop(vm); vm_push(vm, make_num(fix16_neg(val_as_num(a)))); break; } while (0);
}

static inline void native_not(vm_state* vm) {
    script_instance* inst = vm->instance;
    mbc_module* module = vm->module;
    do { host_value a = vm_pop(vm); vm_push(vm, make_bool(!val_as_bool(a))); break; } while (0);
}

static inline void native_eq(vm_state* vm) {
    script_instance* inst = vm->instance;
    mbc_module* module = vm->module;
    do {
                host_value b=vm_pop(vm), a=vm_pop(vm); int equal;
                if(a.tag==HOST_VAL_HOST || b.tag==HOST_VAL_HOST) equal=vm_entity_index(a)==vm_entity_index(b);
                else if((a.tag==HOST_VAL_STRING||a.tag==HOST_VAL_NATIVE_STRING) && (b.tag==HOST_VAL_STRING||b.tag==HOST_VAL_NATIVE_STRING))
                    equal=strcmp(vm_value_string(vm,a),vm_value_string(vm,b))==0;
                else equal=val_as_num(a)==val_as_num(b);
                vm_push(vm,make_bool(equal));break;
            } while (0);
}

static inline void native_ne(vm_state* vm) {
    script_instance* inst = vm->instance;
    mbc_module* module = vm->module;
    do {
                host_value b=vm_pop(vm), a=vm_pop(vm); int equal;
                if(a.tag==HOST_VAL_HOST || b.tag==HOST_VAL_HOST) equal=vm_entity_index(a)==vm_entity_index(b);
                else if((a.tag==HOST_VAL_STRING||a.tag==HOST_VAL_NATIVE_STRING) && (b.tag==HOST_VAL_STRING||b.tag==HOST_VAL_NATIVE_STRING))
                    equal=strcmp(vm_value_string(vm,a),vm_value_string(vm,b))==0;
                else equal=val_as_num(a)==val_as_num(b);
                vm_push(vm,make_bool(!equal));break;
            } while (0);
}

static inline void native_lt(vm_state* vm) {
    script_instance* inst = vm->instance;
    mbc_module* module = vm->module;
    do { host_value b = vm_pop(vm); host_value a = vm_pop(vm); vm_push(vm, make_bool(val_as_num(a) <  val_as_num(b))); break; } while (0);
}

static inline void native_gt(vm_state* vm) {
    script_instance* inst = vm->instance;
    mbc_module* module = vm->module;
    do { host_value b = vm_pop(vm); host_value a = vm_pop(vm); vm_push(vm, make_bool(val_as_num(a) >  val_as_num(b))); break; } while (0);
}

static inline void native_le(vm_state* vm) {
    script_instance* inst = vm->instance;
    mbc_module* module = vm->module;
    do { host_value b = vm_pop(vm); host_value a = vm_pop(vm); vm_push(vm, make_bool(val_as_num(a) <= val_as_num(b))); break; } while (0);
}

static inline void native_ge(vm_state* vm) {
    script_instance* inst = vm->instance;
    mbc_module* module = vm->module;
    do { host_value b = vm_pop(vm); host_value a = vm_pop(vm); vm_push(vm, make_bool(val_as_num(a) >= val_as_num(b))); break; } while (0);
}

static inline void native_and(vm_state* vm) {
    script_instance* inst = vm->instance;
    mbc_module* module = vm->module;
    do { host_value b = vm_pop(vm); host_value a = vm_pop(vm); vm_push(vm, make_bool(val_as_bool(a) && val_as_bool(b))); break; } while (0);
}

static inline void native_or(vm_state* vm) {
    script_instance* inst = vm->instance;
    mbc_module* module = vm->module;
    do { host_value b = vm_pop(vm); host_value a = vm_pop(vm); vm_push(vm, make_bool(val_as_bool(a) || val_as_bool(b))); break; } while (0);
}

static inline void native_new_array(vm_state* vm, uint16_t count) {
    script_instance* inst = vm->instance;
    mbc_module* module = vm->module;
    do {

                host_value array_value = vm_allocate_array(vm, inst, count);
                vm_array* array = value_array(inst, array_value);
                int i;
                for (i = (int)count - 1; i >= 0; --i) {
                    host_value element = vm_pop(vm);
                    if (array && i < array->length)
                        array->values[i] = element;
                }
                vm_push(vm, array_value);
                break;
            } while (0);
}

static inline void native_new_array_sized(vm_state* vm) {
    script_instance* inst = vm->instance;
    mbc_module* module = vm->module;
    do {
                int size = FIX16_TO_INT(val_as_num(vm_pop(vm)));
                vm_push(vm, vm_allocate_array(vm, inst, size));
                break;
            } while (0);
}

static inline void native_get_index(vm_state* vm) {
    script_instance* inst = vm->instance;
    mbc_module* module = vm->module;
    do {
                int index = FIX16_TO_INT(val_as_num(vm_pop(vm)));
                vm_array* array = value_array(inst, vm_pop(vm));
                if (array && index >= 0 && index < array->length)
                    vm_push(vm, array->values[index]);
                else {
                    host_log("[VM] array index OOB");
                    vm_push(vm, make_nil());
                }
                break;
            } while (0);
}

static inline void native_set_index(vm_state* vm) {
    script_instance* inst = vm->instance;
    mbc_module* module = vm->module;
    do {
                host_value value = vm_pop(vm);
                int index = FIX16_TO_INT(val_as_num(vm_pop(vm)));
                vm_array* array = value_array(inst, vm_pop(vm));
                if (array && index >= 0 && index < array->length)
                    array->values[index] = value;
                else
                    host_log("[VM] array index OOB");
                break;
            } while (0);
}

static inline void native_array_length(vm_state* vm) {
    script_instance* inst = vm->instance;
    mbc_module* module = vm->module;
    do {
                vm_array* array = value_array(inst, vm_pop(vm));
                vm_push(vm, make_num(array ? FIX16_FROM_INT(array->length) : 0));
                break;
            } while (0);
}

static inline void native_array_add(vm_state* vm) {
    script_instance* inst = vm->instance;
    mbc_module* module = vm->module;
    do {
                host_value value = vm_pop(vm);
                vm_array* array = value_array(inst, vm_pop(vm));
                if (array && array->length < VM_ARRAY_LENGTH)
                    array->values[array->length++] = value;
                else
                    host_log("[VM] array Add full");
                vm_push(vm, make_nil());
                break;
            } while (0);
}

static inline void native_array_remove_at(vm_state* vm) {
    script_instance* inst = vm->instance;
    mbc_module* module = vm->module;
    do {
                int index = FIX16_TO_INT(val_as_num(vm_pop(vm)));
                vm_array* array = value_array(inst, vm_pop(vm));
                if (array && index >= 0 && index < array->length) {
                    int i;
                    for (i = index; i + 1 < array->length; ++i)
                        array->values[i] = array->values[i + 1];
                    --array->length;
                    array->values[array->length] = make_nil();
                } else {
                    host_log("[VM] RemoveAt OOB");
                }
                vm_push(vm, make_nil());
                break;
            } while (0);
}

static inline void native_array_clear(vm_state* vm) {
    script_instance* inst = vm->instance;
    mbc_module* module = vm->module;
    do {
                vm_array* array = value_array(inst, vm_pop(vm));
                if (array) array->length = 0;
                vm_push(vm, make_nil());
                break;
            } while (0);
}

static inline void native_start_coroutine(vm_state* vm, const char* name) {
    script_instance* inst = vm->instance;
    mbc_module* module = vm->module;
    do {


                const mbc_method* target = mbc_find_method(module, name);
                int slot = -1;
                int i;
                for (i = 0; i < VM_COROUTINE_CAP; ++i) {
                    if (!inst->coroutines[i].active) { slot = i; break; }
                }
                if (target && slot >= 0) {
                    vm_coroutine* co = &inst->coroutines[slot];
                    co->method = target;
                    co->pc = 0;
                    co->sp = 0;
                    co->wait_seconds = 0;
                    co->wait_frames = 0;
                    co->active = 1;
                    for (i = 0; i < VM_LOCAL_CAP; ++i) co->locals[i] = make_nil();
                } else {
                    host_log("[VM] coroutine unavailable");
                }
                vm_push(vm, make_nil());
                break;
            } while (0);
}

static inline void native_stop_all_coroutines(vm_state* vm) {
    script_instance* inst = vm->instance;
    mbc_module* module = vm->module;
    do {
                int i;
                for (i = 0; i < VM_COROUTINE_CAP; ++i) inst->coroutines[i].active = 0;
                vm_push(vm, make_nil());
                break;
            } while (0);
}

static inline int native_yield(vm_state* vm, vm_coroutine* co, unsigned pc, int seconds) {
    int i;
    if (!co) { host_log("yield outside coroutine"); vm->error=1; return 0; }
    co->pc=pc;
    co->wait_seconds=seconds?val_as_num(vm_pop(vm)):0;
    co->wait_frames=seconds?0:1;
    co->sp=(uint8_t)vm->sp;
    for(i=0;i<VM_STACK_CAP;++i)co->stack[i]=vm->stack[i];
    for(i=0;i<VM_LOCAL_CAP;++i)co->locals[i]=vm->locals[i];
    return !vm->error;
}
#endif
