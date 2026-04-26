#ifndef UCORE_MPCONFIG_UCORE_H
#define UCORE_MPCONFIG_UCORE_H

// Global ucore policy applied to MicroPython, regardless of board.
// Each ucore board's mpconfigboard.h includes this header and then adds
// its own board-specific defines (HW_BOARD_NAME, HW_MCU_NAME, pins, etc.).

// ucore owns the byte stream — disable MicroPython's REPL transports so
// they don't fight us for UART/USB peripherals.
#define MICROPY_HW_USB_CDC                  0
#define MICROPY_HW_ESP_USB_SERIAL_JTAG      0
#define MICROPY_HW_ENABLE_USBDEV            0
#define MICROPY_HW_ENABLE_UART_REPL         0

// ucore owns stdin/stdout — sys.stdin/stdout/stderr go through JMP, not files.
#define MICROPY_PY_SYS_STDFILES             0

// Required for JMP interrupt_request to raise KeyboardInterrupt in the runtime.
#define MICROPY_KBD_EXCEPTION               1

// Redirect MicroPython stdout to the JMP iopub channel.
void mpyruntime_log(const char *str, unsigned int len);
#define MP_PLAT_PRINT_STRN(str, len)        mpyruntime_log(str, len)

// Redirect input() to the JMP stdin channel. The declaration of
// readline_over_jmp lives in ucore/mpy_bindings.h and is force-included
// into MicroPython sources by the firmware CMakeLists.
#define mp_hal_readline                     readline_over_jmp

#endif // UCORE_MPCONFIG_UCORE_H
