#include "host.h"
#include "vm.h"
#include "fixedp.h"
#include "input.h"
#include "physics.h"
#include "scene.h"
#include "audio.h"

#include <psxgpu.h>
#include <psxapi.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define LOG_LINES 12
#define LOG_COLS  40

static char     s_log[LOG_LINES][LOG_COLS + 1];
static uint32_t s_log_head;   /* next write slot */
static uint32_t s_log_count;
static fix16_t  s_delta_q16_16 = FIX16_ONE / 60; /* assume 60 Hz field rate */

void host_init(void) {
    s_log_head = 0;
    s_log_count = 0;
    for (int i = 0; i < LOG_LINES; ++i) s_log[i][0] = 0;
}

void host_set_delta_q16_16(fix16_t dt) {
    s_delta_q16_16 = dt;
}

void host_log(const char* line) {
    if (!line) return;
    int n = 0;
    while (n < LOG_COLS && line[n]) {
        s_log[s_log_head][n] = line[n];
        ++n;
    }
    s_log[s_log_head][n] = 0;
    s_log_head = (s_log_head + 1) % LOG_LINES;
    if (s_log_count < LOG_LINES) ++s_log_count;
}

/* Format an unsigned uint into out, returning chars written (excluding NUL). */
static int fmt_uint_local(uint32_t v, char* out, int cap) {
    char tmp[12];
    int n = 0;
    if (v == 0) tmp[n++] = '0';
    while (v > 0 && n < 11) { tmp[n++] = (char)('0' + (v % 10)); v /= 10; }
    int written = 0;
    for (int i = n - 1; i >= 0 && written < cap; --i) out[written++] = tmp[i];
    if (written < cap) out[written] = 0;
    return written;
}

int host_value_to_string(struct vm_state* vm, host_value v, char* out, int cap) {
    if (cap <= 0) return 0;
    switch (v.tag) {
        case HOST_VAL_NIL: {
            const char* k = "nil";
            int i = 0; while (i < cap - 1 && k[i]) { out[i] = k[i]; ++i; }
            out[i] = 0; return i;
        }
        case HOST_VAL_BOOL: {
            const char* k = v.ival ? "true" : "false";
            int i = 0; while (i < cap - 1 && k[i]) { out[i] = k[i]; ++i; }
            out[i] = 0; return i;
        }
        case HOST_VAL_NUMBER:
            return fix16_format((fix16_t)v.ival, out, cap);
        case HOST_VAL_STRING: {
            const char* s = vm_module_string(vm, v.str_idx);
            if (!s) { out[0] = 0; return 0; }
            int i = 0; while (i < cap - 1 && s[i]) { out[i] = s[i]; ++i; }
            out[i] = 0; return i;
        }
        case HOST_VAL_HOST: {
            const char* k = "<host>";
            int i = 0; while (i < cap - 1 && k[i]) { out[i] = k[i]; ++i; }
            out[i] = 0; return i;
        }
    }
    out[0] = 0; return 0;
}

void host_render(uint32_t frame_index, uint32_t scripts_loaded, uint32_t scripts_with_update) {
    char hdr[80];
    int n = 0;
    const char* head = "MIPSYNC ENGINE  scripts=";
    while (head[n] && n < 60) { hdr[n] = head[n]; ++n; }
    n += fmt_uint_local(scripts_loaded, hdr + n, 70 - n);
    if (n < 70) hdr[n++] = '/';
    n += fmt_uint_local(scripts_with_update, hdr + n, 70 - n);
    if (n < 70) hdr[n++] = ' ';
    if (n < 70) hdr[n++] = 'f';
    n += fmt_uint_local(frame_index, hdr + n, 70 - n);
    hdr[n] = 0;
    FntPrint(-1, "%s\n", hdr);
    FntPrint(-1, "----------------------\n");

    /* Print log lines in chronological order (oldest first). */
    uint32_t start = s_log_count < LOG_LINES ? 0 : s_log_head;
    for (uint32_t i = 0; i < s_log_count; ++i) {
        uint32_t idx = (start + i) % LOG_LINES;
        if (s_log[idx][0])
            FntPrint(-1, "%s\n", s_log[idx]);
    }
}

/* Pop helper: read N values from the VM stack into `out` (LIFO order
 * matches push order: out[0] is the FIRST argument the script pushed). */
static int pop_args(struct vm_state* vm, host_value* out, int n) {
    if (vm_stack_size(vm) < (uint32_t)n) return 0;
    for (int i = n - 1; i >= 0; --i) out[i] = vm_pop(vm);
    return 1;
}

static void push_nil(struct vm_state* vm) {
    host_value v = { HOST_VAL_NIL, 0, 0 };
    vm_push(vm, v);
}

static void push_number(struct vm_state* vm, fix16_t f) {
    host_value v = { HOST_VAL_NUMBER, (int32_t)f, 0 };
    vm_push(vm, v);
}

static void push_bool(struct vm_state* vm, int b) {
    host_value v = { HOST_VAL_BOOL, b ? 1 : 0, 0 };
    vm_push(vm, v);
}

/* HostFunc enum mirror (must match src/mips/Bytecode.h). Only the calls we
 * actually service in Milestone A are handled by name; everything else
 * falls through to the unimplemented stub. */
enum {
    HF_LOG_INFO            = 0,
    HF_TIME_DELTATIME      = 1,
    HF_INPUT_GETKEY        = 2,
    HF_INPUT_MOUSEDX       = 3,
    HF_INPUT_MOUSEDY       = 4,
    HF_INPUT_SETCURSORLOCK = 5,
    HF_INPUT_GETCURSORLOCK = 6,
    HF_MATHF_SIN           = 7,
    HF_MATHF_COS           = 8,
    HF_MATHF_SQRT          = 9,
    HF_MATHF_ABS           = 10,
    HF_MATHF_CLAMP         = 11,
    HF_VEC3_CREATE         = 12,
    HF_VEC3_ADD            = 13,
    HF_VEC3_SUB            = 14,
    HF_VEC3_SCALE          = 15,
    HF_VEC3_LENGTH         = 16,
    HF_VEC3_NORMALIZE      = 17,
    HF_VEC3_UP             = 18,
    HF_VEC3_FORWARD        = 19,
    HF_VEC3_RIGHT          = 20,
    HF_PHYSICS_RAYCAST     = 21,
    HF_PHYSICS_MOVE        = 22,
    HF_PHYSICS_IS_GROUNDED = 23,
    HF_INPUT_GETKEYDOWN    = 24,
    HF_ENTITY_GETID        = 25,
    HF_ENTITY_GETNAME      = 26,
    HF_ANIMATOR_SETFLOAT   = 27,
    HF_ANIMATOR_SETBOOL    = 28,
    HF_ANIMATOR_SETINT     = 29,
    HF_ANIMATOR_SETTRIGGER = 30,
    HF_CAMERA_RIGHTX       = 45,
    HF_CAMERA_RIGHTZ       = 46,
    HF_CAMERA_FORWARDX     = 47,
    HF_CAMERA_FORWARDZ     = 48,
    HF_CAMERA_YAW          = 49,
    HF_MATHF_ATAN2         = 50,
    HF_AUDIO_PLAY          = 51,
    HF_AUDIO_STOP          = 52,
    HF_AUDIO_PAUSE         = 53,
    HF_AUDIO_UNPAUSE       = 54,
    HF_AUDIO_SET_CLIP      = 55,
    HF_AUDIO_SET_VOLUME    = 56,
    HF_AUDIO_SET_LOOP      = 57,
    HF_AUDIO_SET_MUTE      = 58,
    HF_AUDIO_SET_AWAKE     = 59,
    HF_AUDIO_SET_ENABLED   = 60,
    HF_INPUT_GETKEYUP      = 61,
    HF_INPUT_GETAXIS       = 62,
    HF_MATHF_MIN           = 63,
    HF_MATHF_MAX           = 64,
    HF_MATHF_LERP          = 65,
    HF_MATHF_FLOOR         = 66,
    HF_MATHF_CEIL          = 67,
    HF_MATHF_ROUND         = 68,
    HF_MATHF_SIGN          = 69,
};

/* Fast fixed-point atan2 approximation. The result is in radians, matching
 * the desktop Mathf.Atan2 host call closely enough for facing interpolation. */
static fix16_t host_atan2(fix16_t y, fix16_t x) {
    const fix16_t pi_over_4 = (fix16_t)51472;  /* pi/4 in Q16.16 */
    const fix16_t three_pi_over_4 = (fix16_t)154416;
    const fix16_t abs_y = fix16_abs(y) + 1;
    fix16_t ratio;
    fix16_t angle;
    if (x >= 0) {
        ratio = fix16_div(x - abs_y, x + abs_y);
        angle = pi_over_4 - fix16_mul(pi_over_4, ratio);
    } else {
        ratio = fix16_div(x + abs_y, abs_y - x);
        angle = three_pi_over_4 - fix16_mul(pi_over_4, ratio);
    }
    return y < 0 ? fix16_neg(angle) : angle;
}

static void active_camera_planar_axes(fix16_t* right_x, fix16_t* right_z,
                                      fix16_t* forward_x, fix16_t* forward_z,
                                      fix16_t* yaw_out) {
    const int cam_idx = ps1_scene_camera_index();
    const ps1_entity* cam = cam_idx >= 0 ? ps1_scene_entity((unsigned int)cam_idx) : 0;
    fix16_t yaw = 0;
    fix16_t turn;
    fix16_t sy;
    fix16_t cy;
    if (cam)
        yaw = cam->rotation[1];
    turn = fix16_div(yaw, FIX16_FROM_INT(360));
    sy = fix16_sin(turn);
    cy = fix16_cos(turn);
    if (right_x) *right_x = cy;
    if (right_z) *right_z = fix16_neg(sy);
    if (forward_x) *forward_x = fix16_neg(sy);
    if (forward_z) *forward_z = fix16_neg(cy);
    if (yaw_out) *yaw_out = yaw;
}

int host_dispatch(struct vm_state* vm, uint16_t host_id, uint8_t argc) {
    host_value a[8];
    if (argc > 8) {
        host_log("[VM] host call argc>8");
        for (int i = 0; i < argc; ++i) (void)vm_pop(vm);
        push_nil(vm);
        return 0;
    }
    if (!pop_args(vm, a, argc)) {
        host_log("[VM] stack underflow on host call");
        push_nil(vm);
        return 0;
    }

    switch (host_id) {
        case HF_LOG_INFO: {
            char buf[LOG_COLS + 1];
            buf[0] = 0;
            if (argc >= 1) {
                if (a[0].tag == HOST_VAL_NUMBER) {
                    fix16_format((fix16_t)a[0].ival, buf, sizeof(buf));
                } else {
                    host_value_to_string(vm, a[0], buf, sizeof(buf));
                }
            }
            host_log(buf);
            push_nil(vm);
            return 1;
        }
        case HF_TIME_DELTATIME:
            push_number(vm, s_delta_q16_16);
            return 1;
        case HF_MATHF_SIN:
            push_number(vm, fix16_sin(argc >= 1 ? (fix16_t)a[0].ival : 0));
            return 1;
        case HF_MATHF_COS:
            push_number(vm, fix16_cos(argc >= 1 ? (fix16_t)a[0].ival : 0));
            return 1;
        case HF_MATHF_SQRT:
            push_number(vm, fix16_sqrt(argc >= 1 ? (fix16_t)a[0].ival : 0));
            return 1;
        case HF_MATHF_ABS:
            push_number(vm, fix16_abs(argc >= 1 ? (fix16_t)a[0].ival : 0));
            return 1;
        case HF_MATHF_CLAMP:
            if (argc >= 3) {
                push_number(vm,
                    fix16_clamp((fix16_t)a[0].ival, (fix16_t)a[1].ival, (fix16_t)a[2].ival));
            } else {
                push_number(vm, argc >= 1 ? (fix16_t)a[0].ival : 0);
            }
            return 1;
        case HF_MATHF_ATAN2:
            push_number(vm, host_atan2(
                argc >= 1 ? (fix16_t)a[0].ival : 0,
                argc >= 2 ? (fix16_t)a[1].ival : 0));
            return 1;
        case HF_MATHF_MIN:
            push_number(vm, argc >= 2 && a[1].ival < a[0].ival ?
                (fix16_t)a[1].ival : (argc >= 1 ? (fix16_t)a[0].ival : 0));
            return 1;
        case HF_MATHF_MAX:
            push_number(vm, argc >= 2 && a[1].ival > a[0].ival ?
                (fix16_t)a[1].ival : (argc >= 1 ? (fix16_t)a[0].ival : 0));
            return 1;
        case HF_MATHF_LERP: {
            fix16_t av = argc >= 1 ? (fix16_t)a[0].ival : 0;
            fix16_t bv = argc >= 2 ? (fix16_t)a[1].ival : 0;
            fix16_t t = argc >= 3 ? fix16_clamp((fix16_t)a[2].ival, 0, FIX16_ONE) : 0;
            push_number(vm, av + fix16_mul(bv - av, t));
            return 1;
        }
        case HF_MATHF_FLOOR: {
            fix16_t v = argc >= 1 ? (fix16_t)a[0].ival : 0;
            push_number(vm, v & (fix16_t)0xFFFF0000);
            return 1;
        }
        case HF_MATHF_CEIL: {
            fix16_t v = argc >= 1 ? (fix16_t)a[0].ival : 0;
            push_number(vm, (v & 0xFFFF) ? ((v & (fix16_t)0xFFFF0000) + FIX16_ONE) : v);
            return 1;
        }
        case HF_MATHF_ROUND: {
            fix16_t v = argc >= 1 ? (fix16_t)a[0].ival : 0;
            push_number(vm, (v + (v >= 0 ? FIX16_ONE / 2 : -FIX16_ONE / 2)) &
                            (fix16_t)0xFFFF0000);
            return 1;
        }
        case HF_MATHF_SIGN: {
            fix16_t v = argc >= 1 ? (fix16_t)a[0].ival : 0;
            push_number(vm, v < 0 ? -FIX16_ONE : (v > 0 ? FIX16_ONE : 0));
            return 1;
        }
        case HF_INPUT_GETKEY:
            if (argc >= 1 && a[0].tag == HOST_VAL_STRING) {
                const char* key = vm_module_string(vm, a[0].str_idx);
                push_number(vm, ps1_input_key_held(key) ? FIX16_ONE : 0);
            } else {
                push_number(vm, 0);
            }
            return 1;
        case HF_INPUT_GETKEYDOWN:
            if (argc >= 1 && a[0].tag == HOST_VAL_STRING) {
                const char* key = vm_module_string(vm, a[0].str_idx);
                push_number(vm, ps1_input_key_down(key) ? FIX16_ONE : 0);
            } else {
                push_number(vm, 0);
            }
            return 1;
        case HF_INPUT_GETKEYUP:
            if (argc >= 1 && a[0].tag == HOST_VAL_STRING) {
                const char* key = vm_module_string(vm, a[0].str_idx);
                push_number(vm, ps1_input_key_up(key) ? FIX16_ONE : 0);
            } else {
                push_number(vm, 0);
            }
            return 1;
        case HF_INPUT_GETAXIS:
            if (argc >= 1 && a[0].tag == HOST_VAL_STRING) {
                const char* axis = vm_module_string(vm, a[0].str_idx);
                fix16_t value = 0;
                if (axis && strcmp(axis, "Horizontal") == 0) {
                    if (ps1_input_key_held("A")) value -= FIX16_ONE;
                    if (ps1_input_key_held("D")) value += FIX16_ONE;
                } else if (axis && strcmp(axis, "Vertical") == 0) {
                    if (ps1_input_key_held("S")) value -= FIX16_ONE;
                    if (ps1_input_key_held("W")) value += FIX16_ONE;
                }
                push_number(vm, value);
            } else {
                push_number(vm, 0);
            }
            return 1;
        case HF_INPUT_MOUSEDX:
            push_number(vm, ps1_input_look_delta_x_q16());
            return 1;
        case HF_INPUT_MOUSEDY:
            push_number(vm, ps1_input_look_delta_y_q16());
            return 1;
        case HF_INPUT_SETCURSORLOCK:
            push_nil(vm);
            return 1;
        case HF_INPUT_GETCURSORLOCK:
            push_number(vm, FIX16_ONE);
            return 1;
        case HF_VEC3_LENGTH:
        case HF_VEC3_UP:
        case HF_VEC3_FORWARD:
        case HF_VEC3_RIGHT:
            /* Vec3 ops collapse to a scalar on the PS1 in Milestone A; the
             * full struct lives in a follow-up milestone. */
            push_number(vm, 0);
            return 1;
        case HF_VEC3_CREATE:
        case HF_VEC3_ADD:
        case HF_VEC3_SUB:
        case HF_VEC3_SCALE:
        case HF_VEC3_NORMALIZE:
            push_nil(vm);
            return 1;
        case HF_PHYSICS_RAYCAST:
            push_number(vm, 0);
            return 1;
        case HF_PHYSICS_MOVE: {
            fix16_t vx = argc >= 1 ? (fix16_t)a[0].ival : 0;
            fix16_t vy = argc >= 2 ? (fix16_t)a[1].ival : 0;
            fix16_t vz = argc >= 3 ? (fix16_t)a[2].ival : 0;
            ps1_physics_set_active_entity((unsigned int)vm_instance_entity_index(vm));
            ps1_physics_move(vx, vy, vz, s_delta_q16_16);
            push_nil(vm);
            return 1;
        }
        case HF_PHYSICS_IS_GROUNDED:
            ps1_physics_set_active_entity((unsigned int)vm_instance_entity_index(vm));
            push_number(vm, ps1_physics_is_grounded() ? FIX16_ONE : 0);
            return 1;
        case HF_ENTITY_GETID:
            push_number(vm, argc >= 1 && a[0].tag == HOST_VAL_HOST
                ? FIX16_FROM_INT((int32_t)a[0].ref.entity_idx) : 0);
            return 1;
        case HF_ENTITY_GETNAME:
            push_nil(vm);
            return 1;
        case HF_ANIMATOR_SETFLOAT:
        case HF_ANIMATOR_SETBOOL:
        case HF_ANIMATOR_SETINT: {
            const char* name = argc >= 1 && a[0].tag == HOST_VAL_STRING
                ? vm_module_string(vm, a[0].str_idx)
                : 0;
            fix16_t value = argc >= 2
                ? (a[1].tag == HOST_VAL_BOOL
                    ? (a[1].ival ? FIX16_ONE : 0)
                    : (fix16_t)a[1].ival)
                : 0;
            if (name) {
                ps1_scene_set_animator_float(
                    (unsigned int)vm_instance_entity_index(vm),
                    name,
                    value);
            }
            push_nil(vm);
            return 1;
        }
        case HF_ANIMATOR_SETTRIGGER:
            push_nil(vm);
            return 1;
        case HF_AUDIO_PLAY:
        case HF_AUDIO_STOP:
        case HF_AUDIO_PAUSE:
        case HF_AUDIO_UNPAUSE: {
            unsigned int entity_index = (unsigned int)vm_instance_entity_index(vm);
            if (argc >= 1 && a[0].tag == HOST_VAL_HOST &&
                a[0].ref.kind == HOST_REF_AUDIO_SOURCE)
                entity_index = a[0].ref.entity_idx;
            if (host_id == HF_AUDIO_PLAY) push_bool(vm, ps1_audio_play(entity_index));
            else if (host_id == HF_AUDIO_STOP) { ps1_audio_stop(entity_index); push_bool(vm, 1); }
            else if (host_id == HF_AUDIO_PAUSE) { ps1_audio_pause(entity_index); push_bool(vm, 1); }
            else { ps1_audio_unpause(entity_index); push_bool(vm, 1); }
            return 1;
        }
        case HF_AUDIO_SET_CLIP:
        case HF_AUDIO_SET_VOLUME:
        case HF_AUDIO_SET_LOOP:
        case HF_AUDIO_SET_MUTE:
        case HF_AUDIO_SET_AWAKE:
        case HF_AUDIO_SET_ENABLED: {
            if (argc < 2 || a[0].tag != HOST_VAL_HOST ||
                a[0].ref.kind != HOST_REF_AUDIO_SOURCE) {
                push_bool(vm, 0);
                return 1;
            }
            ps1_entity* entity = ps1_scene_mutable_entity(a[0].ref.entity_idx);
            if (!entity) { push_bool(vm, 0); return 1; }
            if (host_id == HF_AUDIO_SET_CLIP) {
                int index = (int)((fix16_t)a[1].ival / FIX16_ONE);
                if (index < 0) index = 0;
                if (index > (int)g_ps1_audio_clip_count) index = (int)g_ps1_audio_clip_count;
                ps1_audio_stop(a[0].ref.entity_idx);
                entity->audio_clip_index = (uint8_t)index;
            } else if (host_id == HF_AUDIO_SET_VOLUME) {
                fix16_t value = (fix16_t)a[1].ival;
                if (value < 0) value = 0;
                if (value > FIX16_ONE) value = FIX16_ONE;
                entity->audio_volume_q8 = (uint8_t)(((int64_t)value * 255) / FIX16_ONE);
                ps1_audio_apply_entity(a[0].ref.entity_idx);
            } else if (host_id == HF_AUDIO_SET_LOOP) {
                entity->audio_loop = a[1].ival != 0;
            } else if (host_id == HF_AUDIO_SET_MUTE) {
                entity->audio_mute = a[1].ival != 0;
                ps1_audio_apply_entity(a[0].ref.entity_idx);
            } else if (host_id == HF_AUDIO_SET_AWAKE) {
                entity->audio_play_on_awake = a[1].ival != 0;
            } else if (host_id == HF_AUDIO_SET_ENABLED) {
                entity->audio_enabled = a[1].ival != 0;
                if (!entity->audio_enabled) ps1_audio_stop(a[0].ref.entity_idx);
            }
            /* Runtime clip-path swapping is intentionally unavailable on PS1;
             * all playable samples are fixed into SPU RAM at build time. */
            push_bool(vm, 1);
            return 1;
        }
        case HF_CAMERA_RIGHTX: {
            fix16_t v = 0;
            active_camera_planar_axes(&v, 0, 0, 0, 0);
            push_number(vm, v);
            return 1;
        }
        case HF_CAMERA_RIGHTZ: {
            fix16_t v = 0;
            active_camera_planar_axes(0, &v, 0, 0, 0);
            push_number(vm, v);
            return 1;
        }
        case HF_CAMERA_FORWARDX: {
            fix16_t v = 0;
            active_camera_planar_axes(0, 0, &v, 0, 0);
            push_number(vm, v);
            return 1;
        }
        case HF_CAMERA_FORWARDZ: {
            fix16_t v = 0;
            active_camera_planar_axes(0, 0, 0, &v, 0);
            push_number(vm, v);
            return 1;
        }
        case HF_CAMERA_YAW: {
            fix16_t v = 0;
            active_camera_planar_axes(0, 0, 0, 0, &v);
            push_number(vm, v);
            return 1;
        }
        default: {
            /* Log unimplemented host calls only the first few times. */
            static uint32_t s_unimpl_count[64];
            uint32_t slot = host_id < 64 ? host_id : 0;
            if (s_unimpl_count[slot] < 3) {
                char tag[24]; int n = 0;
                const char* hd = "[VM] unimpl host #";
                while (hd[n]) { tag[n] = hd[n]; ++n; }
                n += fmt_uint_local(host_id, tag + n, (int)sizeof(tag) - n - 1);
                tag[n] = 0;
                host_log(tag);
                ++s_unimpl_count[slot];
            }
            push_nil(vm);
            return 1;
        }
    }
}
