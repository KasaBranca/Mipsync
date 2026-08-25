#include "vm.h"
#include "host.h"
#include "fixedp.h"
#include "scene.h"
#include "audio.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

enum {
#define MIPSYNC_C_OPCODE(cName, cppName, value) OP_##cName = value,
    MIPSYNC_OPCODE_LIST(MIPSYNC_C_OPCODE)
#undef MIPSYNC_C_OPCODE
};

/* ------------------------------------------------------------------------ */
/* Stack helpers (visible to host.c).                                        */
void vm_push(vm_state* vm, host_value v) {
    if (vm->sp >= VM_STACK_CAP) { vm->error = 1; return; }
    vm->stack[vm->sp++] = v;
}
host_value vm_pop(vm_state* vm) {
    if (vm->sp == 0) { vm->error = 1; host_value z = {0,0,0}; return z; }
    return vm->stack[--vm->sp];
}
uint32_t vm_stack_size(const vm_state* vm) { return vm->sp; }

const char* vm_module_string(const vm_state* vm, uint16_t idx) {
    if (!vm || !vm->module || idx >= vm->module->string_count) return 0;
    return vm->module->strings[idx];
}

const char* vm_module_name(const vm_state* vm, uint16_t idx) {
    if (!vm || !vm->module || idx >= vm->module->name_count) return 0;
    return vm->module->names[idx];
}

int vm_instance_entity_index(const vm_state* vm) {
    if (!vm || !vm->instance) return -1;
    return (int)vm->instance->entity_index;
}

static fix16_t* entity_vec3(ps1_entity* ent, uint8_t member) {
    if (!ent) return 0;
    if (member == 0) return ent->position;
    if (member == 1) return ent->rotation;
    return ent->scale;
}

static host_value make_host(host_ref_kind kind, uint16_t entity_idx, uint8_t vec_member) {
    host_value v = { HOST_VAL_HOST, 0, 0, { kind, entity_idx, vec_member } };
    return v;
}

static int name_equals(const char* a, const char* b) {
    if (!a || !b) return 0;
    while (*a && *b) {
        if (*a != *b) return 0;
        ++a; ++b;
    }
    return *a == 0 && *b == 0;
}

/* ------------------------------------------------------------------------ */
/* Little-endian readers.                                                    */
static uint8_t  rd_u8 (const uint8_t* p)        { return p[0]; }
static uint16_t rd_u16(const uint8_t* p)        { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint32_t rd_u32(const uint8_t* p)        { return (uint32_t)(p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24)); }
static int32_t  rd_i32(const uint8_t* p)        { return (int32_t)rd_u32(p); }

/* Bounded string copy: copy at most (cap-1) bytes then NUL. */
static void str_copy_n(char* dst, int cap, const uint8_t* src, int n) {
    int written = 0;
    while (written < n && written < cap - 1) { dst[written] = (char)src[written]; ++written; }
    dst[written] = 0;
}

int mbc_decode(const uint8_t* data, uint32_t size, mbc_module* out) {
    if (!data || !out || size < 20) { host_log("[VM] mbc too small"); return 0; }
    if (data[0] != 'M' || data[1] != 'B' || data[2] != 'C' || data[3] != '1') {
        host_log("[VM] bad mbc magic");
        return 0;
    }
    /* Header. */
    uint32_t off = 4;
    uint16_t version       = rd_u16(data + off); off += 2;
    uint16_t classNameLen  = rd_u16(data + off); off += 2;
    uint16_t numberCount   = rd_u16(data + off); off += 2;
    uint16_t stringCount   = rd_u16(data + off); off += 2;
    uint16_t nameCount     = rd_u16(data + off); off += 2;
    uint16_t fieldCount    = rd_u16(data + off); off += 2;
    uint16_t methodCount   = rd_u16(data + off); off += 2;
    off += 2; /* reserved */
    if (version != 1) { host_log("[VM] unsupported mbc version"); return 0; }

    if (numberCount > VM_NUMBER_CAP || stringCount > VM_STRING_CAP ||
        nameCount   > VM_NAME_CAP   || fieldCount  > VM_FIELD_CAP  ||
        methodCount > VM_METHOD_CAP) {
        host_log("[VM] mbc exceeds caps");
        return 0;
    }

    /* Class name. */
    if (classNameLen >= VM_CLASS_NAME_LEN) { host_log("[VM] class name too long"); return 0; }
    if (off + classNameLen > size) { host_log("[VM] mbc trunc cls"); return 0; }
    str_copy_n(out->class_name, sizeof(out->class_name), data + off, classNameLen);
    off += classNameLen;

    /* Numbers. */
    if (off + (uint32_t)numberCount * 4 > size) { host_log("[VM] mbc trunc nums"); return 0; }
    out->number_count = numberCount;
    for (uint16_t i = 0; i < numberCount; ++i) {
        out->numbers[i] = (fix16_t)rd_i32(data + off);
        off += 4;
    }

    /* Strings. */
    out->string_count = stringCount;
    for (uint16_t i = 0; i < stringCount; ++i) {
        if (off + 2 > size) { host_log("[VM] mbc trunc str"); return 0; }
        uint16_t len = rd_u16(data + off); off += 2;
        if (len >= VM_STRING_LEN) { host_log("[VM] string too long"); return 0; }
        if (off + len > size) { host_log("[VM] mbc trunc str body"); return 0; }
        str_copy_n(out->strings[i], VM_STRING_LEN, data + off, len);
        off += len;
    }

    /* Names. */
    out->name_count = nameCount;
    for (uint16_t i = 0; i < nameCount; ++i) {
        if (off + 2 > size) { host_log("[VM] mbc trunc name"); return 0; }
        uint16_t len = rd_u16(data + off); off += 2;
        if (len >= VM_NAME_LEN) { host_log("[VM] name too long"); return 0; }
        if (off + len > size) { host_log("[VM] mbc trunc name body"); return 0; }
        str_copy_n(out->names[i], VM_NAME_LEN, data + off, len);
        off += len;
    }

    /* Fields. */
    out->field_count = fieldCount;
    for (uint16_t i = 0; i < fieldCount; ++i) {
        if (off + 2 > size) { host_log("[VM] mbc trunc field"); return 0; }
        uint16_t nameLen = rd_u16(data + off); off += 2;
        if (nameLen >= VM_FIELD_NAME_LEN) { host_log("[VM] field name too long"); return 0; }
        if (off + nameLen + 4 > size) { host_log("[VM] mbc trunc field body"); return 0; }
        str_copy_n(out->fields[i].name, VM_FIELD_NAME_LEN, data + off, nameLen);
        off += nameLen;
        out->fields[i].default_const = rd_u16(data + off); off += 2;
        out->fields[i].value_kind    = rd_u8(data + off);  off += 1;
        off += 1; /* flags (hasGetter/hasSetter — runtime ignores) */
    }

    /* Methods. */
    out->method_count = methodCount;
    for (uint16_t i = 0; i < methodCount; ++i) {
        if (off + 2 > size) { host_log("[VM] mbc trunc m"); return 0; }
        uint16_t nameLen = rd_u16(data + off); off += 2;
        if (nameLen >= VM_METHOD_NAME_LEN) { host_log("[VM] method name too long"); return 0; }
        if (off + nameLen + 4 > size) { host_log("[VM] mbc trunc m head"); return 0; }
        str_copy_n(out->methods[i].name, VM_METHOD_NAME_LEN, data + off, nameLen);
        off += nameLen;
        uint32_t codeLen = rd_u32(data + off); off += 4;
        if (codeLen > VM_METHOD_CODE_CAP) { host_log("[VM] method code too large"); return 0; }
        if (off + codeLen + 4 > size) { host_log("[VM] mbc trunc m body"); return 0; }
        out->methods[i].code = data + off;
        out->methods[i].code_size = codeLen;
        off += codeLen;
        out->methods[i].local_count = rd_u16(data + off); off += 2;
        if (out->methods[i].local_count > VM_LOCAL_CAP) {
            host_log("[VM] too many method locals");
            return 0;
        }
        off += 2; /* pad */
    }

    return 1;
}

const mbc_method* mbc_find_method(const mbc_module* m, const char* name) {
    if (!m || !name) return 0;
    for (uint16_t i = 0; i < m->method_count; ++i) {
        if (strcmp(m->methods[i].name, name) == 0) return &m->methods[i];
    }
    return 0;
}

/* ------------------------------------------------------------------------ */
/* Numeric helpers.                                                          */
static fix16_t val_as_num(host_value v) {
    if (v.tag == HOST_VAL_NUMBER) return (fix16_t)v.ival;
    if (v.tag == HOST_VAL_BOOL)   return v.ival ? FIX16_ONE : 0;
    return 0;
}

static int val_as_bool(host_value v) {
    if (v.tag == HOST_VAL_BOOL)   return v.ival ? 1 : 0;
    if (v.tag == HOST_VAL_NUMBER) return v.ival != 0;
    if (v.tag == HOST_VAL_STRING) return 1;
    if (v.tag == HOST_VAL_HOST)   return 1;
    if (v.tag == HOST_VAL_ARRAY)  return 1;
    return 0;
}

static host_value make_num(fix16_t f) { host_value v = { HOST_VAL_NUMBER, (int32_t)f, 0 }; return v; }
static host_value make_bool(int b)    { host_value v = { HOST_VAL_BOOL,   b ? 1 : 0, 0 }; return v; }
static host_value make_nil(void)      { host_value v = { HOST_VAL_NIL,    0,         0 }; return v; }

static vm_array* value_array(script_instance* inst, host_value value) {
    int index;
    if (!inst || value.tag != HOST_VAL_ARRAY || value.ival <= 0) return 0;
    index = value.ival - 1;
    if (index < 0 || index >= inst->array_count || index >= VM_ARRAY_CAP) return 0;
    return &inst->arrays[index];
}

static host_value alloc_array(script_instance* inst, int length) {
    host_value value = make_nil();
    vm_array* array;
    int i;
    if (!inst || inst->array_count >= VM_ARRAY_CAP) {
        host_log("[VM] array pool full");
        return value;
    }
    if (length < 0) length = 0;
    if (length > VM_ARRAY_LENGTH) length = VM_ARRAY_LENGTH;
    array = &inst->arrays[inst->array_count];
    array->length = (uint8_t)length;
    for (i = 0; i < VM_ARRAY_LENGTH; ++i) array->values[i] = make_nil();
    value.tag = HOST_VAL_ARRAY;
    value.ival = (int32_t)(++inst->array_count);
    return value;
}

static int vm_execute(vm_state* vm, mbc_module* module, script_instance* inst,
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

    const uint8_t* code = method->code;
    const uint32_t end  = method->code_size;
    uint32_t pc = coroutine ? coroutine->pc : 0;

    while (pc < end && !vm->error) {
        uint8_t op = code[pc++];
        switch (op) {
            case OP_PUSH_CONST: {
                uint16_t idx = rd_u16(code + pc); pc += 2;
                fix16_t v = idx < module->number_count ? module->numbers[idx] : 0;
                vm_push(vm, make_num(v));
                break;
            }
            case OP_PUSH_BOOL: {
                uint8_t b = code[pc++];
                vm_push(vm, make_bool(b));
                break;
            }
            case OP_PUSH_STRING: {
                uint16_t idx = rd_u16(code + pc); pc += 2;
                host_value v = { HOST_VAL_STRING, 0, idx };
                vm_push(vm, v);
                break;
            }
            case OP_PUSH_FIELD: {
                uint16_t idx = rd_u16(code + pc); pc += 2;
                vm_push(vm, idx < VM_FIELD_CAP ? inst->runtime_fields[idx] : make_nil());
                break;
            }
            case OP_SET_FIELD: {
                uint16_t idx = rd_u16(code + pc); pc += 2;
                host_value top = vm_pop(vm);
                if (idx < VM_FIELD_CAP) {
                    inst->runtime_fields[idx] = top;
                    if (top.tag == HOST_VAL_NUMBER || top.tag == HOST_VAL_BOOL)
                        inst->fields[idx] = val_as_num(top);
                }
                break;
            }
            case OP_PUSH_LOCAL: {
                uint16_t idx = rd_u16(code + pc); pc += 2;
                vm_push(vm, idx < VM_LOCAL_CAP ? vm->locals[idx] : make_nil());
                break;
            }
            case OP_SET_LOCAL: {
                uint16_t idx = rd_u16(code + pc); pc += 2;
                host_value top = vm_pop(vm);
                if (idx < VM_LOCAL_CAP) vm->locals[idx] = top;
                break;
            }
            case OP_POP:
                (void)vm_pop(vm);
                break;
            case OP_GET_GLOBAL: {
                uint16_t nameIdx = rd_u16(code + pc); pc += 2;
                const char* name = vm_module_name(vm, nameIdx);
                if (name_equals(name, "transform"))
                    vm_push(vm, make_host(HOST_REF_TRANSFORM, vm->instance->entity_index, 0));
                else if (name_equals(name, "gameObject"))
                    vm_push(vm, make_host(HOST_REF_ENTITY, vm->instance->entity_index, 0));
                else if (name_equals(name, "AudioSource"))
                    vm_push(vm, make_host(HOST_REF_AUDIO_SOURCE, vm->instance->entity_index, 0));
                else
                    vm_push(vm, make_nil());
                break;
            }
            case OP_GET_COMPONENT: {
                uint16_t nameIdx = rd_u16(code + pc); pc += 2;
                const char* className = vm_module_name(vm, nameIdx);
                if (vm->module && name_equals(className, vm->module->class_name))
                    vm_push(vm, make_host(HOST_REF_ENTITY, vm->instance->entity_index, 0));
                else if (name_equals(className, "Transform"))
                    vm_push(vm, make_host(HOST_REF_TRANSFORM, vm->instance->entity_index, 0));
                else if (name_equals(className, "AudioSource"))
                    vm_push(vm, make_host(HOST_REF_AUDIO_SOURCE, vm->instance->entity_index, 0));
                else
                    vm_push(vm, make_nil());
                break;
            }
            case OP_GET_MEMBER: {
                uint16_t memberIdx = rd_u16(code + pc); pc += 2;
                const char* member = vm_module_name(vm, memberIdx);
                host_value obj = vm_pop(vm);
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
                        vm_push(vm, make_num(ent ? (fix16_t)ent->id : 0));
                    } else {
                        vm_push(vm, make_nil());
                    }
                } else if (obj.tag == HOST_VAL_HOST && obj.ref.kind == HOST_REF_AUDIO_SOURCE) {
                    const ps1_entity* ent = ps1_scene_entity(obj.ref.entity_idx);
                    if (!ent) {
                        vm_push(vm, make_nil());
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
            }
            case OP_GET_VEC3_AXIS: {
                uint8_t axis = code[pc++];
                host_value obj = vm_pop(vm);
                fix16_t val = 0;
                if (obj.tag == HOST_VAL_HOST &&
                    (obj.ref.kind == HOST_REF_VEC3 || obj.ref.kind == HOST_REF_WORLD_VEC3)) {
                    ps1_entity* ent = ps1_scene_mutable_entity(obj.ref.entity_idx);
                    fix16_t* vec = entity_vec3(ent, obj.ref.vec_member);
                    if (vec && axis < 3) val = vec[axis];
                }
                vm_push(vm, make_num(val));
                break;
            }
            case OP_SET_VEC3_AXIS: {
                /* Stack: [value, vec3Host] (vec3Host on top). Matches desktop VM. */
                uint8_t axis = code[pc++];
                host_value vecHost = vm_pop(vm);
                host_value val     = vm_pop(vm);
                if (vecHost.tag == HOST_VAL_HOST &&
                    (vecHost.ref.kind == HOST_REF_VEC3 || vecHost.ref.kind == HOST_REF_WORLD_VEC3) &&
                    axis < 3) {
                    ps1_entity* ent = ps1_scene_mutable_entity(vecHost.ref.entity_idx);
                    fix16_t* vec = entity_vec3(ent, vecHost.ref.vec_member);
                    if (vec) vec[axis] = val_as_num(val);
                }
                break;
            }
            case OP_SET_VEC3_FROM_VALUE:
                /* Full vec3 assign (transform.position = Vector3...) — not used yet on PS1. */
                (void)vm_pop(vm);
                (void)vm_pop(vm);
                break;
            case OP_ADD: { host_value b = vm_pop(vm); host_value a = vm_pop(vm); vm_push(vm, make_num(fix16_add(val_as_num(a), val_as_num(b)))); break; }
            case OP_SUB: { host_value b = vm_pop(vm); host_value a = vm_pop(vm); vm_push(vm, make_num(fix16_sub(val_as_num(a), val_as_num(b)))); break; }
            case OP_MUL: { host_value b = vm_pop(vm); host_value a = vm_pop(vm); vm_push(vm, make_num(fix16_mul(val_as_num(a), val_as_num(b)))); break; }
            case OP_DIV: { host_value b = vm_pop(vm); host_value a = vm_pop(vm); vm_push(vm, make_num(fix16_div(val_as_num(a), val_as_num(b)))); break; }
            case OP_MOD: {
                host_value b = vm_pop(vm); host_value a = vm_pop(vm);
                fix16_t av = val_as_num(a), bv = val_as_num(b);
                vm_push(vm, make_num(bv == 0 ? 0 : (fix16_t)(av % bv)));
                break;
            }
            case OP_NEG: { host_value a = vm_pop(vm); vm_push(vm, make_num(fix16_neg(val_as_num(a)))); break; }
            case OP_NOT: { host_value a = vm_pop(vm); vm_push(vm, make_bool(!val_as_bool(a))); break; }
            case OP_EQ:  { host_value b = vm_pop(vm); host_value a = vm_pop(vm); vm_push(vm, make_bool(val_as_num(a) == val_as_num(b))); break; }
            case OP_NE:  { host_value b = vm_pop(vm); host_value a = vm_pop(vm); vm_push(vm, make_bool(val_as_num(a) != val_as_num(b))); break; }
            case OP_LT:  { host_value b = vm_pop(vm); host_value a = vm_pop(vm); vm_push(vm, make_bool(val_as_num(a) <  val_as_num(b))); break; }
            case OP_GT:  { host_value b = vm_pop(vm); host_value a = vm_pop(vm); vm_push(vm, make_bool(val_as_num(a) >  val_as_num(b))); break; }
            case OP_LE:  { host_value b = vm_pop(vm); host_value a = vm_pop(vm); vm_push(vm, make_bool(val_as_num(a) <= val_as_num(b))); break; }
            case OP_GE:  { host_value b = vm_pop(vm); host_value a = vm_pop(vm); vm_push(vm, make_bool(val_as_num(a) >= val_as_num(b))); break; }
            case OP_AND: { host_value b = vm_pop(vm); host_value a = vm_pop(vm); vm_push(vm, make_bool(val_as_bool(a) && val_as_bool(b))); break; }
            case OP_OR:  { host_value b = vm_pop(vm); host_value a = vm_pop(vm); vm_push(vm, make_bool(val_as_bool(a) || val_as_bool(b))); break; }
            case OP_CALL_HOST: {
                uint16_t hid = rd_u16(code + pc); pc += 2;
                uint8_t  ac  = code[pc++];
                host_dispatch(vm, hid, ac);
                break;
            }
            case OP_RETURN:
                if (coroutine) coroutine->active = 0;
                return 1;
            case OP_JUMP: {
                int32_t off32 = rd_i32(code + pc); pc += 4;
                int64_t next = (int64_t)pc + off32;
                if (next < 0 || next > end) { vm->error = 1; break; }
                pc = (uint32_t)next;
                break;
            }
            case OP_JUMP_IF_FALSE: {
                int32_t off32 = rd_i32(code + pc); pc += 4;
                host_value v = vm_pop(vm);
                if (!val_as_bool(v)) {
                    int64_t next = (int64_t)pc + off32;
                    if (next < 0 || next > end) { vm->error = 1; break; }
                    pc = (uint32_t)next;
                }
                break;
            }
            case OP_NEW_ARRAY: {
                uint16_t count = rd_u16(code + pc); pc += 2;
                host_value array_value = alloc_array(inst, count);
                vm_array* array = value_array(inst, array_value);
                int i;
                for (i = (int)count - 1; i >= 0; --i) {
                    host_value element = vm_pop(vm);
                    if (array && i < array->length)
                        array->values[i] = element;
                }
                vm_push(vm, array_value);
                break;
            }
            case OP_NEW_ARRAY_SIZED: {
                int size = FIX16_TO_INT(val_as_num(vm_pop(vm)));
                vm_push(vm, alloc_array(inst, size));
                break;
            }
            case OP_GET_INDEX: {
                int index = FIX16_TO_INT(val_as_num(vm_pop(vm)));
                vm_array* array = value_array(inst, vm_pop(vm));
                if (array && index >= 0 && index < array->length)
                    vm_push(vm, array->values[index]);
                else {
                    host_log("[VM] array index OOB");
                    vm_push(vm, make_nil());
                }
                break;
            }
            case OP_SET_INDEX: {
                host_value value = vm_pop(vm);
                int index = FIX16_TO_INT(val_as_num(vm_pop(vm)));
                vm_array* array = value_array(inst, vm_pop(vm));
                if (array && index >= 0 && index < array->length)
                    array->values[index] = value;
                else
                    host_log("[VM] array index OOB");
                break;
            }
            case OP_ARRAY_LENGTH: {
                vm_array* array = value_array(inst, vm_pop(vm));
                vm_push(vm, make_num(array ? FIX16_FROM_INT(array->length) : 0));
                break;
            }
            case OP_ARRAY_ADD: {
                host_value value = vm_pop(vm);
                vm_array* array = value_array(inst, vm_pop(vm));
                if (array && array->length < VM_ARRAY_LENGTH)
                    array->values[array->length++] = value;
                else
                    host_log("[VM] array Add full");
                vm_push(vm, make_nil());
                break;
            }
            case OP_ARRAY_REMOVE_AT: {
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
            }
            case OP_ARRAY_CLEAR: {
                vm_array* array = value_array(inst, vm_pop(vm));
                if (array) array->length = 0;
                vm_push(vm, make_nil());
                break;
            }
            case OP_START_COROUTINE: {
                uint16_t name_index = rd_u16(code + pc); pc += 2;
                const char* name = vm_module_name(vm, name_index);
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
            }
            case OP_STOP_ALL_COROUTINES: {
                int i;
                for (i = 0; i < VM_COROUTINE_CAP; ++i) inst->coroutines[i].active = 0;
                vm_push(vm, make_nil());
                break;
            }
            case OP_YIELD_NEXT:
            case OP_YIELD_SECONDS:
            case OP_YIELD_BREAK: {
                int i;
                if (!coroutine) {
                    host_log("[VM] yield outside coroutine");
                    vm->error = 1;
                    break;
                }
                if (op == OP_YIELD_BREAK) {
                    coroutine->active = 0;
                    return 1;
                }
                coroutine->pc = pc;
                coroutine->sp = (uint8_t)vm->sp;
                for (i = 0; i < VM_STACK_CAP; ++i) coroutine->stack[i] = vm->stack[i];
                for (i = 0; i < VM_LOCAL_CAP; ++i) coroutine->locals[i] = vm->locals[i];
                if (op == OP_YIELD_SECONDS) {
                    coroutine->wait_seconds = val_as_num(vm_pop(vm));
                    coroutine->sp = (uint8_t)vm->sp;
                    coroutine->wait_frames = 0;
                } else {
                    coroutine->wait_frames = 1;
                    coroutine->wait_seconds = 0;
                }
                return 1;
            }
            default:
                /* Unknown opcode — fail soft so a future bytecode revision
                 * doesn't brick the player; the user just sees the log. */
                host_log("[VM] unknown opcode");
                vm->error = 1;
                break;
        }
    }
    if (coroutine) coroutine->active = 0;
    return !vm->error;
}

int vm_run_method(vm_state* vm, mbc_module* module, script_instance* inst,
                  const mbc_method* method) {
    return vm_execute(vm, module, inst, method, 0);
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
        vm_execute(vm, module, inst, co->method, co);
    }
}
