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
#include "protocol_examples_common.h"

// #include "esp_vfs.h"
// #include "esp_vfs_fat.h"

// #include "lib/oofatfs/ff.h"

#include "ucore/ucore.h"
#include "ucore/mpy_bindings.h"

#include "hal_esp32.h"


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

    /* This helper function configures Wi-Fi or Ethernet, as selected in menuconfig.
     * Read "Establishing Wi-Fi or Ethernet Connection" section in
     * examples/protocols/README.md for more information about this function.
     */
    //ESP_ERROR_CHECK(example_connect());

    //register port dependent handlers
    platform_init();

    //start the ucore service
    ucore_start(&(transport_args_t){
        // logs flow on uart0 so we need to move the app link to uart1 for qemu emulation
        // for emuation uart0 is attached to  mon:stdio while uart1 attaches to a tcp socket
        // during actual running we have to use uart0 as its exposed over the usb 
        .uart_port_no = UART_NUM_1,
    });
    

    //start micropython runtime
    mpyruntime_start();    

}
