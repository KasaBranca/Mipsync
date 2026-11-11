/* Runtime services for generated C/MIPS functions. No bytecode interpreter. */
#include "native_ops.h"
#include "physics.h"
void vm_push(vm_state* vm, host_value v) {
    if (vm->sp >= VM_STACK_CAP) { vm->error = 1; return; }
    vm->stack[vm->sp++] = v;
}
host_value vm_pop(vm_state* vm) {
    if (vm->sp == 0) { vm->error = 1; host_value z = {0,0,0}; return z; }
    return vm->stack[--vm->sp];
}
uint32_t vm_stack_size(const vm_state* vm) { return vm->sp; }

const char* vm_module_string(const vm_state* vm, uint16_t idx) { (void)vm; (void)idx; return 0; }

const char* vm_module_name(const vm_state* vm, uint16_t idx) { (void)vm; (void)idx; return 0; }

int vm_instance_entity_index(const vm_state* vm) {
    if (!vm || !vm->instance) return -1;
    return (int)vm->instance->entity_index;
}

int vm_entity_index(host_value value) {
    unsigned int i;
    if (value.tag == HOST_VAL_HOST && value.ref.kind != HOST_REF_NONE)
        return ps1_scene_entity(value.ref.entity_idx) ? (int)value.ref.entity_idx : -1;
    if (value.tag == HOST_VAL_NUMBER && value.ival > 0 && value.ival % FIX16_ONE == 0)
        for (i = 0; i < ps1_scene_entity_count(); ++i) {
            const ps1_entity* e = ps1_scene_entity(i);
            if (e && e->id == (uint32_t)(value.ival / FIX16_ONE)) return (int)i;
        }
    return -1;
}

host_value vm_make_vec3(fix16_t x, fix16_t y, fix16_t z) {
    host_value value = {0}; value.tag = HOST_VAL_VEC3;
    value.vector[0] = x; value.vector[1] = y; value.vector[2] = z; return value;
}

int vm_read_vec3(host_value value, fix16_t out[3]) {
    const ps1_entity* entity;
    const fix16_t* vector;
    unsigned int axis;
    if (value.tag == HOST_VAL_VEC3) { for (axis=0;axis<3;++axis) out[axis]=value.vector[axis]; return 1; }
    if (value.tag != HOST_VAL_HOST || (value.ref.kind != HOST_REF_VEC3 && value.ref.kind != HOST_REF_WORLD_VEC3)) return 0;
    entity = ps1_scene_entity(value.ref.entity_idx);
    if (!entity) return 0;
    vector = value.ref.vec_member == 1 ? entity->rotation : value.ref.vec_member == 2 ? entity->scale : entity->position;
    for (axis=0;axis<3;++axis) out[axis]=vector[axis]; return 1;
}

const char* vm_value_string(const vm_state* vm, host_value value) {
    return value.tag == HOST_VAL_NATIVE_STRING ? (value.text ? value.text : "") :
        value.tag == HOST_VAL_STRING ? vm_module_string(vm,value.str_idx) : "";
}


const mbc_method* mbc_find_method(const mbc_module* m, const char* name) {
    if (!m || !name) return 0;
    for (uint16_t i = 0; i < m->method_count; ++i) {
        if (strcmp(m->methods[i].name, name) == 0) return &m->methods[i];
    }
    return 0;
}

static int native_execute(vm_state* vm, mbc_module* module, script_instance* inst,
                          const mbc_method* method, vm_coroutine* coroutine) {
    if (!vm || !module || !inst || !method) return 0;
    vm->module = module;
    vm->instance = inst;
    vm->error = 0;
    if (coroutine) {
        vm->sp = coroutine->sp;
        for (int i = 0; i < VM_STACK_CAP; ++i) vm->stack[i] = coroutine->stack[i];
        for (int i = 0; i < VM_LOCAL_CAP; ++i) vm->locals[i] = coroutine->locals[i];
    } else {
        vm->sp = 0;
        for (int i = 0; i < VM_LOCAL_CAP; ++i) vm->locals[i] = make_nil();
    }

    return method->entry ? method->entry(vm, inst, coroutine) : 0;
}
int vm_run_method(vm_state* vm, mbc_module* module, script_instance* inst, const mbc_method* method) {
    return native_execute(vm,module,inst,method,0);
}
void vm_resume_coroutines(vm_state* vm, mbc_module* module, script_instance* inst,
                          fix16_t delta_time) {
    int i;
    if (!vm || !module || !inst) return;
    for (i = 0; i < VM_COROUTINE_CAP; ++i) {
        vm_coroutine* co = &inst->coroutines[i];
        if (!co->active || !co->method) continue;
        if (co->wait_frames > 0) {
            --co->wait_frames;
            continue;
        }
        if (co->wait_seconds > 0) {
            co->wait_seconds = fix16_sub(co->wait_seconds, delta_time);
            if (co->wait_seconds > 0) continue;
            co->wait_seconds = 0;
        }
        native_execute(vm, module, inst, co->method, co);
    }
}
