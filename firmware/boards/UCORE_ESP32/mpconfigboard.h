#include "ucore/mpconfig_ucore.h"

#ifndef MICROPY_HW_BOARD_NAME
#define MICROPY_HW_BOARD_NAME               "ucore ESP32"
#endif

#ifndef MICROPY_HW_MCU_NAME
#define MICROPY_HW_MCU_NAME                 "ESP32"
#endif

// Trim binary size — board doesn't expose I2S.
#define MICROPY_PY_MACHINE_I2S              0
