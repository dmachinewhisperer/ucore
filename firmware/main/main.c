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

    // logs flow on uart0 so we need to move the app link to uart1 for qemu emulation
    // for emulation uart0 is attached to mon:stdio while uart1 attaches to a tcp socket
    // during actual running we have to use uart0 as its exposed over the usb
    ucore_start(&(ucore_uart_config_t){
        .uart_port_no = UART_NUM_1,
    });
    

    //start micropython runtime
    mpyruntime_start();    

}
