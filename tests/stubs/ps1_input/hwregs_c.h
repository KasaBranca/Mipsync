#pragma once
#include <stdint.h>
static volatile uint16_t test_sio[5];
#define SIO_CTRL(n) test_sio[0]
#define SIO_STAT(n) test_sio[1]
#define SIO_DATA(n) test_sio[2]
#define SIO_MODE(n) test_sio[3]
#define SIO_BAUD(n) test_sio[4]
