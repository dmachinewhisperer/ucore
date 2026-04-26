#include <stdio.h>
#include <stdbool.h>
#include <inttypes.h>

// #include "esp_wifi.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "esp_event.h"
#include "freertos/FreeRTOS.h"
#include <freertos/task.h>
#include "esp_netif.h"

#include "ucore/ucore.h"
#include "ucore/mpy_bindings.h"
#include "ucore/transport_uart.h"


static const char *TAG = "main";


void app_main(void)
{ 
    ESP_LOGI(TAG, "[APP] Startup..");
    ESP_LOGI(TAG, "[APP] Free memory: %" PRIu32 " bytes", esp_get_free_heap_size());
    ESP_LOGI(TAG, "[APP] IDF version: %s", esp_get_idf_version());

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }
    
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // Pick a transport backend and start ucore.
    ucore_register_transport(&ucore_transport_uart);

    // UART0 is the line wired to the dev kit's USB-to-UART bridge — the
    // actual port we talk to from the host. It's also the line ESP-IDF's
    // own logs print on, so log output and JMP frames will share the wire
    // when this firmware runs on real hardware.
    //
    // QEMU note: for emulation, UART0 is attached to mon:stdio while
    // UART1 attaches to a TCP socket. The emulation path will need to
    // override this back to UART_NUM_1 (or pick a transport at runtime).
    ucore_start(&(ucore_uart_config_t){
        .uart_port_no = UART_NUM_0,
    });
    

    //start micropython runtime
    mpyruntime_start();    

}
