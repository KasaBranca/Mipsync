#include "input.h"
#include "fixedp.h"
#include "pad_mapping.h"

#include <psxapi.h>
#include <psxpad.h>
#include <psxgpu.h>
#include <hwregs_c.h>
#include <string.h>

static volatile unsigned char s_pad_buf[2][34];
static uint16_t      s_curr_btn = 0xFFFFu;
static uint8_t       s_curr_stick_nav = 0;
static volatile uint16_t s_sampled_btn = 0xFFFFu;
static volatile uint16_t s_pending_down, s_pending_up;
static volatile uint8_t s_sampled_stick_nav, s_pending_stick_down;
static uint16_t s_frame_down, s_frame_up;
static uint8_t s_frame_stick_down;
static void (*s_previous_vsync_callback)(void);
static int           s_initialized = 0;

/* Input.mouseDelta is expressed in the same units on desktop and PS1.  A raw
 * DualShock stick only spans roughly [-1, 1] after Q16 conversion, while a
 * desktop mouse commonly reports several pixels per frame.  Scale the stick
 * here so the stock Mips# sensitivity (0.15) produces a useful ~143 deg/s at
 * 30 fps instead of the previous ~4.5 deg/s. */
#define LOOK_SCALE FIX16_FROM_INT(64)
#define LOOK_DEADZONE 16
#define PAD_SPI_TIMEOUT 100000u

static void pad_spi_delay(unsigned int cycles) {
    while (cycles--)
        __asm__ volatile("");
}

static int pad_spi_wait(uint16_t mask, int set) {
    unsigned int timeout = PAD_SPI_TIMEOUT;
    while (timeout--) {
        if (!!(SIO_STAT(0) & mask) == !!set)
            return 1;
    }
    return 0;
}

/* Send one controller command before the BIOS pad driver is started.  The
 * return value is the actual response length: digital mode ends after five
 * bytes, while config/analog mode returns nine or more. */
static int pad_spi_command(const uint8_t* tx, uint8_t* rx, int tx_len) {
    int i;

    SIO_CTRL(0) = 0x0010;
    while (SIO_STAT(0) & 0x0002)
        (void)SIO_DATA(0);
    pad_spi_delay(1000);
    SIO_CTRL(0) = 0x0003;
    pad_spi_delay(2000);

    for (i = 0; i < tx_len; ++i) {
        if (!pad_spi_wait(0x0001, 1))
            break;
        SIO_DATA(0) = tx[i];
        if (!pad_spi_wait(0x0002, 1))
            break;
        rx[i] = SIO_DATA(0);

        if (i + 1 == tx_len) {
            SIO_CTRL(0) = 0x0000;
            return tx_len;
        }

        /* No /ACK after the final response byte is normal, not an error. */
        if (!pad_spi_wait(0x0080, 1)) {
            SIO_CTRL(0) = 0x0000;
            return i + 1;
        }
        if (!pad_spi_wait(0x0080, 0))
            break;
        SIO_CTRL(0) = 0x0013;
        SIO_CTRL(0) = 0x0003;
    }

    SIO_CTRL(0) = 0x0000;
    return i;
}

static void pad_restore_bios_sio(void) {
    /* SIO reset also clears MODE and BAUD.  InitPAD() assumes the standard
     * controller values are already present, so always restore them. */
    SIO_CTRL(0) = 0x0040;
    SIO_MODE(0) = 0x000d;
    SIO_BAUD(0) = 0x0088;
    SIO_CTRL(0) = 0x0000;
}

static void pad_force_analog_mode(void) {
    static const uint8_t enter_config[9] = {
        0x01, PAD_CMD_CONFIG_MODE, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00
    };
    static const uint8_t set_analog[9] = {
        0x01, PAD_CMD_SET_ANALOG, 0x00, 0x01, 0x03, 0x00, 0x00, 0x00, 0x00
    };
    static const uint8_t exit_config[9] = {
        0x01, PAD_CMD_CONFIG_MODE, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
    };
    uint8_t rx[9];
    int i;

    pad_restore_bios_sio();

    /* 43h must be sent twice because the config response is delayed by one
     * transaction.  Some third-party pads need a third attempt. */
    for (i = 0; i < 3; ++i) {
        memset(rx, 0, sizeof(rx));
        (void)pad_spi_command(enter_config, rx, 9);
        if (((rx[1] >> 4) & 0x0f) == PAD_ID_CONFIG_MODE && rx[2] == 0x5a) {
            (void)pad_spi_command(set_analog, rx, 9);
            (void)pad_spi_command(exit_config, rx, 9);
            break;
        }
    }

    pad_restore_bios_sio();
}

static volatile PADTYPE* pad0(void) {
    return (volatile PADTYPE*)&s_pad_buf[0][0];
}

/* BIOS normally refreshes the pad buffer from its VBlank callback.  The game
 * can spend several fields inside a software render, however, and on some
 * BIOS/emulator combinations our callback runs before that refresh.  Sample
 * the buffer once more at the game tick so a short L1/R1 tap cannot be lost
 * between the edge latch and the next script update. */
static int pad_buttons_now(uint16_t* out) {
    volatile PADTYPE* pad = pad0();
    if (!out || pad->stat != 0 ||
        (pad->type != PAD_ID_DIGITAL && pad->type != PAD_ID_ANALOG &&
         pad->type != PAD_ID_ANALOG_STICK))
        return 0;
    *out = pad->btn;
    return 1;
}

static uint8_t raw_left_stick_nav_bits(void) {
    volatile PADTYPE* pad = pad0();
    int dx, dy;
    uint8_t bits = 0;
    if (pad->stat != 0 || !(pad->type == PAD_ID_ANALOG || pad->type == PAD_ID_ANALOG_STICK))
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

/* Capture BIOS pad updates at VBlank, independently of the render rate. A
 * press and release between two slow frames must still produce a key edge. */
static void input_vblank(void) {
    volatile PADTYPE* pad = pad0();
    uint16_t buttons = 0xFFFFu;
    uint8_t nav = raw_left_stick_nav_bits();
    if (pad->stat == 0 && (pad->type == PAD_ID_DIGITAL ||
        pad->type == PAD_ID_ANALOG || pad->type == PAD_ID_ANALOG_STICK))
        buttons = pad->btn;
    s_pending_down |= s_sampled_btn & (uint16_t)~buttons;
    s_pending_up |= (uint16_t)~s_sampled_btn & buttons;
    s_pending_stick_down |= nav & (uint8_t)~s_sampled_stick_nav;
    s_sampled_btn = buttons;
    s_sampled_stick_nav = nav;
    if (s_previous_vsync_callback) s_previous_vsync_callback();
}

void ps1_input_init(void) {
    int interrupts_enabled;
    if (s_initialized) return;
    interrupts_enabled = EnterCriticalSection();
    /* CD boot may leave the BIOS shell's pad driver running. It must not
     * access SIO while the controller configuration transaction is in flight. */
    StopPAD();
    pad_force_analog_mode();
    memset((void*)s_pad_buf, 0xff, sizeof(s_pad_buf));
    InitPAD((unsigned char*)&s_pad_buf[0][0], 34, (unsigned char*)&s_pad_buf[1][0], 34);
    StartPAD();
    ChangeClearPAD(0);
    s_previous_vsync_callback = (void (*)(void))VSyncCallback(input_vblank);
    s_initialized = 1;
    if (interrupts_enabled) ExitCriticalSection();
}

void ps1_input_poll(void) {
    int interrupts_enabled;
    uint16_t buttons;
    if (!s_initialized) ps1_input_init();
    interrupts_enabled = EnterCriticalSection();
    if (pad_buttons_now(&buttons)) {
        /* This is intentionally ORed with the VBlank latch.  It only fills
         * the gap when the BIOS callback observed the previous packet; it
         * never removes an already buffered press/release edge. */
        s_pending_down |= s_sampled_btn & (uint16_t)~buttons;
        s_pending_up |= (uint16_t)~s_sampled_btn & buttons;
        s_sampled_btn = buttons;
    }
    s_curr_btn = s_sampled_btn;
    s_curr_stick_nav = s_sampled_stick_nav;
    s_frame_down = s_pending_down;
    s_frame_up = s_pending_up;
    s_frame_stick_down = s_pending_stick_down;
    s_pending_down = s_pending_up = 0;
    s_pending_stick_down = 0;
    if (interrupts_enabled) ExitCriticalSection();
}

static int has_analog_sticks(void) {
    volatile PADTYPE* pad = pad0();
    return pad->type == PAD_ID_ANALOG || pad->type == PAD_ID_ANALOG_STICK;
}

static int valid_right_stick(void) {
    volatile PADTYPE* pad = pad0();
    if (pad->stat != 0 || !has_analog_sticks()) return 0;
    if (pad->rs_x == 0 && pad->rs_y == 0) return 0;
    if (pad->rs_x == 255 && pad->rs_y == 255) return 0;
    return 1;
}

static int button_held(PadButton btn) {
    return !(s_curr_btn & btn);
}

static int button_down(PadButton btn) {
    return (s_frame_down & btn) != 0;
}

static int button_up(PadButton btn) {
    return (s_frame_up & btn) != 0;
}

static int stick_held(int axis, int negative) {
    uint8_t bit = axis == 0 ? (negative ? 4u : 8u) : (negative ? 1u : 2u);
    return (s_curr_stick_nav & bit) != 0;
}

int ps1_input_key_held(const char* name) {
    if (!name) return 0;
    { const uint16_t mask=ps1_named_button_mask(name); if (mask) return !(s_curr_btn & mask); }
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
    if (strcmp(name, "Circle") == 0) return button_held(PAD_CIRCLE);
    if (strcmp(name, "Square") == 0) return button_held(PAD_SQUARE);
    if (strcmp(name, "Triangle") == 0) return button_held(PAD_TRIANGLE);
    if (strcmp(name, "Start") == 0) return button_held(PAD_START);
    if (strcmp(name, "Run") == 0) return button_held(PAD_SQUARE);
    if (strcmp(name, "StrafeLeft") == 0) return button_held(PAD_L1);
    if (strcmp(name, "StrafeRight") == 0) return button_held(PAD_R1);
    if (strcmp(name, "QuickTurn") == 0)
        return button_held(PAD_L1) && button_held(PAD_R1);
    if (strcmp(name, "Aim") == 0) return button_held(PAD_R2);
    if (strcmp(name, "LeftShift") == 0 ||
        strcmp(name, "Aim") == 0 ||
        strcmp(name, "L1") == 0)
        return button_held(PAD_L1);
    return 0;
}

int ps1_input_key_down(const char* name) {
    if (!name) return 0;
    { const uint16_t mask=ps1_named_button_mask(name); if (mask) return (s_frame_down & mask)!=0; }
    if (strcmp(name, "W") == 0 || strcmp(name, "Up") == 0)
        return button_down(PAD_UP) || (s_frame_stick_down & 1u);
    if (strcmp(name, "S") == 0 || strcmp(name, "Down") == 0)
        return button_down(PAD_DOWN) || (s_frame_stick_down & 2u);
    if (strcmp(name, "A") == 0 || strcmp(name, "Left") == 0)
        return button_down(PAD_LEFT) || (s_frame_stick_down & 4u);
    if (strcmp(name, "D") == 0 || strcmp(name, "Right") == 0)
        return button_down(PAD_RIGHT) || (s_frame_stick_down & 8u);
    if (strcmp(name, "Space") == 0 || strcmp(name, "Jump") == 0) return button_down(PAD_CROSS);
    if (strcmp(name, "Circle") == 0) return button_down(PAD_CIRCLE);
    if (strcmp(name, "Square") == 0) return button_down(PAD_SQUARE);
    if (strcmp(name, "Triangle") == 0) return button_down(PAD_TRIANGLE);
    if (strcmp(name, "Start") == 0) return button_down(PAD_START);
    if (strcmp(name, "Run") == 0) return button_down(PAD_SQUARE);
    if (strcmp(name, "StrafeLeft") == 0) return button_down(PAD_L1);
    if (strcmp(name, "StrafeRight") == 0) return button_down(PAD_R1);
    if (strcmp(name, "QuickTurn") == 0)
        return (button_down(PAD_L1) && button_held(PAD_R1)) ||
               (button_down(PAD_R1) && button_held(PAD_L1));
    if (strcmp(name, "Aim") == 0) return button_down(PAD_R2);
    if (strcmp(name, "LeftShift") == 0 ||
        strcmp(name, "Aim") == 0 ||
        strcmp(name, "L1") == 0) return button_down(PAD_L1);
    return 0;
}

int ps1_input_key_up(const char* name) {
    if (!name) return 0;
    { const uint16_t mask=ps1_named_button_mask(name); if (mask) return (s_frame_up & mask)!=0; }
    if (strcmp(name, "W") == 0 || strcmp(name, "Up") == 0) return button_up(PAD_UP);
    if (strcmp(name, "S") == 0 || strcmp(name, "Down") == 0) return button_up(PAD_DOWN);
    if (strcmp(name, "A") == 0 || strcmp(name, "Left") == 0) return button_up(PAD_LEFT);
    if (strcmp(name, "D") == 0 || strcmp(name, "Right") == 0) return button_up(PAD_RIGHT);
    if (strcmp(name, "Space") == 0 || strcmp(name, "Jump") == 0) return button_up(PAD_CROSS);
    if (strcmp(name, "Circle") == 0) return button_up(PAD_CIRCLE);
    if (strcmp(name, "Square") == 0) return button_up(PAD_SQUARE);
    if (strcmp(name, "Triangle") == 0) return button_up(PAD_TRIANGLE);
    if (strcmp(name, "Start") == 0) return button_up(PAD_START);
    if (strcmp(name, "Run") == 0) return button_up(PAD_SQUARE);
    if (strcmp(name, "StrafeLeft") == 0) return button_up(PAD_L1);
    if (strcmp(name, "StrafeRight") == 0) return button_up(PAD_R1);
    if (strcmp(name, "QuickTurn") == 0)
        return button_up(PAD_L1) || button_up(PAD_R1);
    if (strcmp(name, "Aim") == 0) return button_up(PAD_R2);
    if (strcmp(name, "LeftShift") == 0 || strcmp(name, "Aim") == 0 ||
        strcmp(name, "L1") == 0) return button_up(PAD_L1);
    return 0;
}

int32_t ps1_input_axis_q16(const char* name) {
    volatile PADTYPE* pad=pad0();
    int32_t x=0,y=0;
    int right;
    if (!name) return 0;
    right=strcmp(name,"RightStickX")==0 || strcmp(name,"RightStickY")==0;
    if (pad->stat==0 && has_analog_sticks()) {
        const int rawX=right ? pad->rs_x : pad->ls_x;
        const int rawY=right ? pad->rs_y : pad->ls_y;
        if (!((rawX==0 && rawY==0) || (rawX==255 && rawY==255))) {
            x=ps1_stick_axis_q16(rawX-128);
            y=-ps1_stick_axis_q16(rawY-128);
        }
    }
    if (right) return strcmp(name,"RightStickX")==0 ? x : y;
    if (strcmp(name,"LeftStickX")==0 || strcmp(name,"Horizontal")==0)
        return ps1_combine_axis_q16(x,button_held(PAD_RIGHT),button_held(PAD_LEFT));
    if (strcmp(name,"LeftStickY")==0 || strcmp(name,"Vertical")==0)
        return ps1_combine_axis_q16(y,button_held(PAD_UP),button_held(PAD_DOWN));
    return 0;
}

static int remap_look_axis(int raw) {
    int magnitude = raw < 0 ? -raw : raw;
    int sign = raw < 0 ? -1 : 1;
    if (magnitude <= LOOK_DEADZONE)
        return 0;
    if (magnitude > 127)
        magnitude = 127;
    /* Remove the deadzone without leaving a large first non-zero step. */
    magnitude = ((magnitude - LOOK_DEADZONE) * 127) /
                (127 - LOOK_DEADZONE);
    return sign * magnitude;
}

int32_t ps1_input_look_delta_x_q16(void) {
    if (!valid_right_stick()) return 0;
    const int dx = remap_look_axis((int)pad0()->rs_x - 128);
    /* Match desktop mouseDeltaX: right is positive.  Mips# decides whether
     * yaw should add or subtract it for the engine's handedness. */
    return fix16_mul((fix16_t)(dx * 256), LOOK_SCALE);
}

int32_t ps1_input_look_delta_y_q16(void) {
    if (!valid_right_stick()) return 0;
    const int dy = remap_look_axis((int)pad0()->rs_y - 128);
    /* Match desktop Input.mouseDeltaY: stick up is positive. Multiplication
     * avoids left-shifting a negative signed value. */
    return fix16_mul((fix16_t)((-dy) * 256), LOOK_SCALE);
}
