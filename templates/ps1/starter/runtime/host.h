#ifndef MIPSYNC_HOST_H
#define MIPSYNC_HOST_H

#include "fixedp.h"
#include <stdint.h>

/*
 * Host-function dispatch for the PS1 mini-VM. Mirrors HostFunc enum from
 * src/mips/Bytecode.h. Each handler pops the right number of args from the
 * VM stack and pushes its return value. The desktop exporter rejects known
 * unsupported calls before generating a PS1 build. The fallback diagnostic
 * remains as a defensive check for malformed or version-mismatched bytecode.
 */

/* Stack value tag. Matches the Tag enum from src/mips/Value.h with the
 * cosmetic adjustment that Number is Q16.16 (not double) on the PS1. */
typedef enum {
    HOST_VAL_NIL    = 0,
    HOST_VAL_BOOL   = 1,
    HOST_VAL_NUMBER = 2,
    HOST_VAL_STRING = 3,
    HOST_VAL_HOST   = 4,
    HOST_VAL_ARRAY  = 5
} host_val_tag;

typedef enum {
    HOST_REF_NONE = 0,
    HOST_REF_ENTITY,
    HOST_REF_TRANSFORM,
    HOST_REF_VEC3,
    HOST_REF_WORLD_VEC3,
    HOST_REF_AUDIO_SOURCE,
} host_ref_kind;

typedef struct host_ref {
    host_ref_kind kind;
    uint16_t      entity_idx;
    uint8_t       vec_member; /* 0=position,1=rotation,2=scale */
} host_ref;

typedef struct host_value {
    host_val_tag tag;
    int32_t      ival;
    uint16_t     str_idx;
    host_ref     ref;
} host_value;

/* Forward decl from vm.h. */
struct vm_state;

/* Initialise the host: wire up the on-screen log buffer, clear deltatime,
 * etc. Call once before the game loop. */
void host_init(void);

/* Adjust the simulation timestep that Time_DeltaTime returns. */
void host_set_delta_q16_16(fix16_t dt);

/* Push a log line (NUL-terminated) onto the visible log. The log is a
 * fixed ring of 12 lines × 40 chars. */
void host_log(const char* line);

/* Read a printable form of a host_value into `out`. */
int host_value_to_string(struct vm_state* vm, host_value v, char* out, int cap);

/* Render the current log buffer + status header into the active FNT
 * stream so the next FntFlush() draws it. */
void host_render(uint32_t frame_index, uint32_t scripts_loaded, uint32_t scripts_with_update);

/* Called by the VM on `CallHost u16 hostId, u8 argc`. Pops `argc` values
 * from `vm`'s stack and pushes the result (push HOST_VAL_NIL if a host has
 * no return value). Returns 1 on success, 0 on hard failure (already
 * logged). */
int host_dispatch(struct vm_state* vm, uint16_t host_id, uint8_t argc);

#endif /* MIPSYNC_HOST_H */
