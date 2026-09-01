#include "input.h"
#include "fixedp.h"

#include <psxapi.h>
#include <psxpad.h>
#include <hwregs_c.h>
#include <string.h>

static unsigned char s_pad_buf[2][34];
static uint16_t      s_prev_btn = 0xFFFFu;
static uint16_t      s_curr_btn = 0xFFFFu;
static uint8_t       s_prev_stick_nav = 0;
static uint8_t       s_curr_stick_nav = 0;
static int           s_initialized = 0;

#define LOOK_SCALE FIX16_FROM_INT(2)
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
    pad_force_analog_mode();
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
    if (strcmp(name, "W") == 0 || strcmp(name, "Up") == 0)
        return button_down(PAD_UP) || ((s_curr_stick_nav & 1u) && !(s_prev_stick_nav & 1u));
    if (strcmp(name, "S") == 0 || strcmp(name, "Down") == 0)
        return button_down(PAD_DOWN) || ((s_curr_stick_nav & 2u) && !(s_prev_stick_nav & 2u));
    if (strcmp(name, "A") == 0 || strcmp(name, "Left") == 0)
        return button_down(PAD_LEFT) || ((s_curr_stick_nav & 4u) && !(s_prev_stick_nav & 4u));
    if (strcmp(name, "D") == 0 || strcmp(name, "Right") == 0)
        return button_down(PAD_RIGHT) || ((s_curr_stick_nav & 8u) && !(s_prev_stick_nav & 8u));
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

int32_t ps1_input_look_delta_x_q16(void) {
    if (!valid_right_stick()) return 0;
    int dx = (int)pad0()->rs_x - 128;
    if (dx > -8 && dx < 8) dx = 0;
    /* Camera yaw uses the opposite handedness from the pad's raw X axis. */
    return fix16_mul((fix16_t)((-dx) * 256), LOOK_SCALE);
}

int32_t ps1_input_look_delta_y_q16(void) {
    if (!valid_right_stick()) return 0;
    int dy = (int)pad0()->rs_y - 128;
    if (dy > -8 && dy < 8) dy = 0;
    /* Match desktop Input.mouseDeltaY: stick up is positive. Multiplication
     * avoids left-shifting a negative signed value. */
    return fix16_mul((fix16_t)((-dy) * 256), LOOK_SCALE);
}
