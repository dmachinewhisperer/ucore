#ifndef UCORE_TRANSPORT_UART_H
#define UCORE_TRANSPORT_UART_H

#include "driver/uart.h"
#include "ucore/ucore.h"

typedef struct {
    int uart_port_no;
} ucore_uart_config_t;

// UART transport vtable. Pass to ucore_register_transport(); the matching
// ucore_uart_config_t goes to ucore_start() as args.
extern const transport_t ucore_transport_uart;

#endif // UCORE_TRANSPORT_UART_H
