#ifndef MICROPYTHON_RUNTIME_OVERRIDES
#define MICROPYTHON_RUNTIME_OVERRIDES

#include "ucore.h"


#define MICROPY_HW_USB_CDC                  0
#define  MICROPY_HW_ESP_USB_SERIAL_JTAG     0
#define MICROPY_HW_ENABLE_USBDEV            0
#define MICROPY_HW_ENABLE_UART_REPL         0

#define MICROPY_PY_MACHINE_I2S              0
//#define MICROPY_PY_IO 0
#define MICROPY_PY_SYS_STDFILES 0

#define MICROPY_KBD_EXCEPTION 1 //must be set for the jmp interrupt_request to wrok

//for some reason, not including espnow throws the following build error:
/*
/home/ubuntu/.espressif/tools/xtensa-esp-elf/esp-14.2.0_20241119/xtensa-esp-elf/bin/../lib/gcc/xtensa-esp-elf/14.2.0/../../../../xtensa-esp-elf/bin/ld: esp-idf/main_esp32/libmain_esp32.a(objmodule.c.obj):(.rodata.mp_builtin_module_table+0x84): undefined reference to `mp_module_espnow'
collect2: error: ld returned 1 exit status
make[2]: *** [CMakeFiles/ucore.elf.dir/build.make:604: ucore.elf] Error 1
make[1]: *** [CMakeFiles/Makefile2:2643: CMakeFiles/ucore.elf.dir/all] Error 2
make: *** [Makefile:136: all] Error 2
*/
//#define MICROPY_PY_ESPNOW                   0

/*
overrrides the logging facility used in the mpy runtime so that we can output 
"print" outputs of mpy runtime during code execution over custom channels like websocket
*/
//#define mp_hal_stdout_tx_strn_cooked mpyruntime_log
#define MP_PLAT_PRINT_STRN(str, len)        mpyruntime_log(str, len)
#define mp_hal_readline readline_over_jmp
#endif

