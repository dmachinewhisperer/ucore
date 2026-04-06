#ifndef MICROPY_HW_BOARD_NAME
#define MICROPY_HW_BOARD_NAME               "ucore ESP32"
#endif

#ifndef MICROPY_HW_MCU_NAME
#define MICROPY_HW_MCU_NAME                 "ESP32"
#endif

// Disable hardware features not used by ucore
#define MICROPY_HW_USB_CDC                  0
#define MICROPY_HW_ESP_USB_SERIAL_JTAG      0
#define MICROPY_HW_ENABLE_USBDEV            0
#define MICROPY_HW_ENABLE_UART_REPL         0

// Disable unused Python modules
#define MICROPY_PY_MACHINE_I2S              0
#define MICROPY_PY_SYS_STDFILES             0

// Required for JMP interrupt_request to work
#define MICROPY_KBD_EXCEPTION               1

// Redirect stdout to JMP iopub channel
void mpyruntime_log(const char *str, unsigned int len);
#define MP_PLAT_PRINT_STRN(str, len)        mpyruntime_log(str, len)

// Redirect input() to JMP stdin channel
// The actual declaration of readline_over_jmp lives in mpy_bindings.h
// which is included by the files that use mp_hal_readline.
#define mp_hal_readline                     readline_over_jmp
