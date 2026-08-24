#include "input.h"
#include "fixedp.h"

#include <psxapi.h>
#include <psxpad.h>
#include <string.h>

static unsigned char s_pad_buf[2][34];
static uint16_t      s_prev_btn = 0xFFFFu;
static uint16_t      s_curr_btn = 0xFFFFu;
static uint8_t       s_prev_stick_nav = 0;
static uint8_t       s_curr_stick_nav = 0;
static int           s_initialized = 0;

#define LOOK_SCALE FIX16_FROM_INT(2)

static PADTYPE* pad0(void) {
    return (PADTYPE*)&s_pad_buf[0][0];
}

static uint8_t raw_left_stick_nav_bits(void) {
    PADTYPE* pad = pad0();
    int dx, dy;
    uint8_t bits = 0;
    if (!(pad->type == PAD_ID_ANALOG || pad->type == PAD_ID_ANALOG_STICK))
        return 0;
    if ((pad->ls_x == 0 && pad->ls_y == 0) || (pad->ls_x == 255 && pad->ls_y == 255))
        return 0;
    dx = (int)pad->ls_x - 128;
    dy = (int)pad->ls_y - 128;
    if (dy < -32) bits |= 1u;
    if (dy > 32) bits |= 2u;
    if (dx < -32) bits |= 4u;
    if (dx > 32) bits |= 8u;
    return bits;
}

void ps1_input_init(void) {
    InitPAD(&s_pad_buf[0][0], 34, &s_pad_buf[1][0], 34);
    StartPAD();
    ChangeClearPAD(0);
    s_initialized = 1;
}

void ps1_input_poll(void) {
    if (!s_initialized) ps1_input_init();
    s_prev_btn = s_curr_btn;
    s_curr_btn = 0xFFFFu;
    PADTYPE* pad = pad0();
    if (pad->type == PAD_ID_DIGITAL ||
        pad->type == PAD_ID_ANALOG ||
        pad->type == PAD_ID_ANALOG_STICK) {
        s_curr_btn = pad->btn;
    }
    s_prev_stick_nav = s_curr_stick_nav;
    s_curr_stick_nav = raw_left_stick_nav_bits();
}

static int has_analog_sticks(void) {
    PADTYPE* pad = pad0();
    return pad->type == PAD_ID_ANALOG || pad->type == PAD_ID_ANALOG_STICK;
}

static int valid_left_stick(void) {
    PADTYPE* pad = pad0();
    if (!has_analog_sticks()) return 0;
    if (pad->ls_x == 0 && pad->ls_y == 0) return 0;
    if (pad->ls_x == 255 && pad->ls_y == 255) return 0;
    return 1;
}

static int valid_right_stick(void) {
    PADTYPE* pad = pad0();
    if (!has_analog_sticks()) return 0;
    if (pad->rs_x == 0 && pad->rs_y == 0) return 0;
    if (pad->rs_x == 255 && pad->rs_y == 255) return 0;
    return 1;
}

static int button_held(PadButton btn) {
    return !(s_curr_btn & btn);
}

static int button_down(PadButton btn) {
    return button_held(btn) && (s_prev_btn & btn);
}

static int button_up(PadButton btn) {
    return !button_held(btn) && !(s_prev_btn & btn);
}

static int stick_held(int axis, int negative) {
    if (!valid_left_stick()) return 0;
    PADTYPE* pad = pad0();
    int v = (axis == 0) ? ((int)pad->ls_x - 128) : ((int)pad->ls_y - 128);
    if (negative) return v < -32;
    return v > 32;
}

int ps1_input_key_held(const char* name) {
    if (!name) return 0;
    if (strcmp(name, "W") == 0 || strcmp(name, "Up") == 0)
        return button_held(PAD_UP) || stick_held(1, 1);
    if (strcmp(name, "S") == 0 || strcmp(name, "Down") == 0)
        return button_held(PAD_DOWN) || stick_held(1, 0);
    if (strcmp(name, "A") == 0 || strcmp(name, "Left") == 0)
        return button_held(PAD_LEFT) || stick_held(0, 1);
    if (strcmp(name, "D") == 0 || strcmp(name, "Right") == 0)
        return button_held(PAD_RIGHT) || stick_held(0, 0);
    if (strcmp(name, "Space") == 0 || strcmp(name, "Jump") == 0)
        return button_held(PAD_CROSS);
    if (strcmp(name, "LeftShift") == 0 ||
        strcmp(name, "Aim") == 0 ||
        strcmp(name, "L1") == 0)
        return button_held(PAD_L1);
    return 0;
}

int ps1_input_key_down(const char* name) {
    if (!name) return 0;
    if (strcmp(name, "W") == 0 || strcmp(name, "Up") == 0)
        return button_down(PAD_UP) || ((s_curr_stick_nav & 1u) && !(s_prev_stick_nav & 1u));
    if (strcmp(name, "S") == 0 || strcmp(name, "Down") == 0)
        return button_down(PAD_DOWN) || ((s_curr_stick_nav & 2u) && !(s_prev_stick_nav & 2u));
    if (strcmp(name, "A") == 0 || strcmp(name, "Left") == 0)
        return button_down(PAD_LEFT) || ((s_curr_stick_nav & 4u) && !(s_prev_stick_nav & 4u));
    if (strcmp(name, "D") == 0 || strcmp(name, "Right") == 0)
        return button_down(PAD_RIGHT) || ((s_curr_stick_nav & 8u) && !(s_prev_stick_nav & 8u));
    if (strcmp(name, "Space") == 0 || strcmp(name, "Jump") == 0) return button_down(PAD_CROSS);
    if (strcmp(name, "LeftShift") == 0 ||
        strcmp(name, "Aim") == 0 ||
        strcmp(name, "L1") == 0) return button_down(PAD_L1);
    return 0;
}

int ps1_input_key_up(const char* name) {
    if (!name) return 0;
    if (strcmp(name, "W") == 0 || strcmp(name, "Up") == 0) return button_up(PAD_UP);
    if (strcmp(name, "S") == 0 || strcmp(name, "Down") == 0) return button_up(PAD_DOWN);
    if (strcmp(name, "A") == 0 || strcmp(name, "Left") == 0) return button_up(PAD_LEFT);
    if (strcmp(name, "D") == 0 || strcmp(name, "Right") == 0) return button_up(PAD_RIGHT);
    if (strcmp(name, "Space") == 0 || strcmp(name, "Jump") == 0) return button_up(PAD_CROSS);
    if (strcmp(name, "LeftShift") == 0 || strcmp(name, "Aim") == 0 ||
        strcmp(name, "L1") == 0) return button_up(PAD_L1);
    return 0;
}

int32_t ps1_input_look_delta_x_q16(void) {
    if (!valid_right_stick()) return 0;
    int dx = (int)pad0()->rs_x - 128;
    if (dx > -8 && dx < 8) dx = 0;
    return fix16_mul((fix16_t)(dx << 8), LOOK_SCALE);
}

int32_t ps1_input_look_delta_y_q16(void) {
    if (!valid_right_stick()) return 0;
    int dy = (int)pad0()->rs_y - 128;
    if (dy > -8 && dy < 8) dy = 0;
    return fix16_mul((fix16_t)(dy << 8), LOOK_SCALE);
}
