#ifndef MIPSYNC_VM_H
#define MIPSYNC_VM_H

#include "fixedp.h"
#include "host.h"
#include <stdint.h>

/*
 * PS1 mini-VM for Mipsync bytecode (.mbc format). Stack machine, fixed-
 * point numerics, bounded buffers — no heap, no STL.
 *
 * Memory budget (per instance):
 *   stack:  64 host_value × 16 bytes ≈ 1 KiB
 *   locals: 64 host_value           ≈ 1 KiB
 *   fields: 64 fix16_t              ≈ 256 B
 * Plus the immutable module data (decoded once at boot).
 *
 * Hard limits (raise as needed; static asserts in vm.c catch overflow):
 *   numbers : 128 constants
 *   strings : 128 strings (max len 96)
 *   names   : 128 names   (max len 32)
 *   fields  : 32
 *   methods : 16
 *   code    : 4 KiB per method
 */

#define VM_STACK_CAP         64
#define VM_LOCAL_CAP         64
#define VM_FIELD_CAP         32
#define VM_NUMBER_CAP        128
#define VM_STRING_CAP        128
#define VM_STRING_LEN        96
#define VM_NAME_CAP          128
#define VM_NAME_LEN          32
#define VM_METHOD_CAP        16
#define VM_METHOD_NAME_LEN   24
#define VM_FIELD_NAME_LEN    24
#define VM_ARRAY_CAP          8
#define VM_ARRAY_LENGTH      32
#define VM_COROUTINE_CAP     4

typedef struct mbc_method {
    char            name[VM_METHOD_NAME_LEN];
    const uint8_t*  code;
    uint32_t        code_size;
    uint16_t        local_count;
} mbc_method;

typedef struct mbc_field {
    char     name[VM_FIELD_NAME_LEN];
    uint16_t default_const;
    uint8_t  value_kind;   /* 0=Number, 1=Bool */
} mbc_field;

typedef struct mbc_module {
    char        class_name[32];
    uint16_t    number_count;
    fix16_t     numbers[VM_NUMBER_CAP];
    uint16_t    string_count;
    char        strings[VM_STRING_CAP][VM_STRING_LEN];
    uint16_t    name_count;
    char        names[VM_NAME_CAP][VM_NAME_LEN];
    uint16_t    field_count;
    mbc_field   fields[VM_FIELD_CAP];
    uint16_t    method_count;
    mbc_method  methods[VM_METHOD_CAP];
} mbc_module;

typedef struct vm_array {
    uint8_t length;
    host_value values[VM_ARRAY_LENGTH];
} vm_array;

typedef struct vm_coroutine {
    const mbc_method* method;
    uint32_t pc;
    host_value stack[VM_STACK_CAP];
    uint8_t sp;
    host_value locals[VM_LOCAL_CAP];
    fix16_t wait_seconds;
    uint8_t wait_frames;
    uint8_t active;
} vm_coroutine;

typedef struct script_instance {
    mbc_module* module;
    uint16_t    entity_index;
    fix16_t     fields[VM_FIELD_CAP];
    host_value  runtime_fields[VM_FIELD_CAP];
    vm_array    arrays[VM_ARRAY_CAP];
    uint8_t     array_count;
    vm_coroutine coroutines[VM_COROUTINE_CAP];
} script_instance;

typedef struct vm_state {
    mbc_module*       module;
    script_instance*  instance;
    host_value        stack[VM_STACK_CAP];
    uint32_t          sp;
    host_value        locals[VM_LOCAL_CAP];
    int               error;
} vm_state;

/*
 * Decode a single .mbc blob into the supplied mbc_module. Returns 1 on
 * success, 0 if the blob is malformed or exceeds the static caps above.
 * Errors are written to the host log so the user sees them at boot.
 */
int mbc_decode(const uint8_t* data, uint32_t size, mbc_module* out);

/* Find a method by exact name match. NULL on miss. */
const mbc_method* mbc_find_method(const mbc_module* m, const char* name);

/* Run a method. Returns 1 on success, 0 on VM error (already logged). */
int vm_run_method(vm_state* vm, mbc_module* module, script_instance* inst,
                  const mbc_method* method);
void vm_resume_coroutines(vm_state* vm, mbc_module* module, script_instance* inst,
                          fix16_t delta_time);

/* Stack helpers used by host.c. */
void        vm_push(vm_state* vm, host_value v);
host_value  vm_pop (vm_state* vm);
uint32_t    vm_stack_size(const vm_state* vm);

/* Resolve a string constant from the module currently bound to `vm`. */
const char* vm_module_string(const vm_state* vm, uint16_t idx);
const char* vm_module_name(const vm_state* vm, uint16_t idx);
int         vm_instance_entity_index(const vm_state* vm);

#endif /* MIPSYNC_VM_H */
