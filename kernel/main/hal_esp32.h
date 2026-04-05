#ifndef UCORE_HAL_ESP32
#define UCORE_HAL_ESP32

#include "driver/uart.h"

typedef struct {
    int uart_port_no;
} transport_args_t;

void platform_init(void); 

#endif