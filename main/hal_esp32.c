#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "esp_system.h"
#include "driver/uart.h"

#include "ucore/ucore.h"
#include "ucore/utils.h"

#include "hal_esp32.h"


//#define LOG_HEX(tag, msg, buf, len) ((void)0)

//this lock is used for guarding against context switch when we are 
// logging a buffer from another task. interleaving jumbles up the logs without it


static const char *TAG = "hal_esp32";

// Serial transport implementation
#define INITIAL_ACCUMULATOR_LEN 256
#define UART_BUF_SIZE 1024
#define UART_BAUD_RATE 115200

//binary buffer logging facility 
static SemaphoreHandle_t serial_write_mutex;
#define LOG_HEX(tag, msg, buf, len) do {                         \
    xSemaphoreTake(serial_write_mutex, portMAX_DELAY);           \
    char _hex[(len) * 3 + 1];                                    \
    for (int _i = 0; _i < (len); _i++) {                         \
        sprintf(&_hex[_i * 3], "%02x ", (buf)[_i]);              \
    }                                                            \
    ESP_LOGI((tag), msg " len=%d: %s", (len), _hex);             \
    xSemaphoreGive(serial_write_mutex);                          \
} while (0)

static const char* jmp_msg_type_to_str(uint8_t type) {
    switch (type) {
        case JMP_KERNEL_INFO_REQUEST: return "KERNEL_INFO_REQUEST";
        case JMP_KERNEL_INFO_REPLY:   return "KERNEL_INFO_REPLY";
        case JMP_EXECUTE_REQUEST:     return "EXECUTE_REQUEST";
        case JMP_EXECUTE_REPLY:       return "EXECUTE_REPLY";
        case JMP_STREAM:              return "STREAM";
        case JMP_ERROR:               return "ERROR";
        case JMP_DISPLAY_DATA:        return "DISPLAY_DATA";
        case JMP_STATUS:              return "STATUS";
        case JMP_INPUT_REQUEST:       return "INPUT_REQUEST";
        case JMP_INPUT_REPLY:         return "INPUT_REPLY";
        case JMP_COMPLETE_REQUEST:    return "COMPLETE_REQUEST";
        case JMP_COMPLETE_REPLY:      return "COMPLETE_REPLY";
        case JMP_INSPECT_REQUEST:     return "INSPECT_REQUEST";
        case JMP_INSPECT_REPLY:       return "INSPECT_REPLY";
        case JMP_IS_COMPLETE_REQUEST: return "IS_COMPLETE_REQUEST";
        case JMP_IS_COMPLETE_REPLY:   return "IS_COMPLETE_REPLY";
        case JMP_SHUTDOWN_REQUEST:    return "SHUTDOWN_REQUEST";
        case JMP_SHUTDOWN_REPLY:      return "SHUTDOWN_REPLY";
        case JMP_INTERRUPT_REQUEST:   return "INTERRUPT_REQUEST";
        case JMP_EXECUTE_RESULT:      return "EXECUTE_RESULT";
        case JMP_COMM_OPEN:           return "COMM_OPEN";
        case JMP_COMM_MSG:            return "COMM_MSG";
        case JMP_COMM_CLOSE:          return "COMM_CLOSE";
        case JMP_AUTH_REQUEST:        return "AUTH_REQUEST";
        case JMP_AUTH_REPLY:          return "AUTH_REPLY";
        case TARGET_NOT_FOUND:        return "TARGET_NOT_FOUND";
        default:                      return "UNKNOWN";
    }
}

static uint8_t get_jmp_msg_type(const uint8_t *payload, int len) {
    if (len < 46) return 0xFF;
    const uint8_t *h_buf = payload + 46; // Skip SANS (36) and MSG_PREFIX (10)
    int h_len = len - 46;
    
    size_t pos = 0;
    // Skip msg_id, session_id, and username length-prefixed strings
    for (int i = 0; i < 3; i++) {
        if (pos + 2 > h_len) return 0xFF;
        uint16_t field_len = (h_buf[pos] << 8) | h_buf[pos + 1];
        pos += 2 + field_len;
    }
    
    if (pos >= h_len) return 0xFF;
    return h_buf[pos];
}


// Context structure to maintain serial connection state
typedef struct {
    int uart_port;
    TaskHandle_t serial_task_handle;
} serial_ctx_t;

static serial_ctx_t *g_serial_ctx = NULL;

bool esp32_serial_status(void *ctx) {
    if (ctx == NULL) return false;
    serial_ctx_t *sctx = (serial_ctx_t *)ctx;
    return uart_is_driver_installed(sctx->uart_port);
}

int esp32_serial_bin_tx(void *ctx, uint8_t *payload, int len) {
    if (ctx == NULL || len <= 0 || payload == NULL) return -1;
    
    serial_ctx_t *sctx = (serial_ctx_t *)ctx;
    
    //LOG_HEX(TAG, "TX Decoded:", payload, len);
    uint8_t mtype = get_jmp_msg_type(payload, len);
    ESP_LOGI(TAG, "TX Message Type: %s", jmp_msg_type_to_str(mtype));

    // COB encode payload and send
    uint8_t encoded_data[cob_encoded_max_size(len)];
    size_t encoded_size = 0;
    cob_result_t result = cob_encode(payload, len, encoded_data, sizeof(encoded_data), &encoded_size);
    if (result != COB_OK) {
        ESP_LOGE(TAG, "COB encode failed");
        return -1;
    }

    //LOG_HEX(TAG, "TX Encoded:", encoded_data, encoded_size);

    int bytes_written = uart_write_bytes(sctx->uart_port, (const char *)encoded_data, encoded_size);
    //ESP_LOGI(TAG, "\nuart written bytes: %d\n", bytes_written);
    if (bytes_written < 0) {
        ESP_LOGE(TAG, "UART write failed");
        return -1;
    }
    

    return bytes_written;
}

void serial_task(void *pvParameters) {
    serial_ctx_t *sctx = (serial_ctx_t *)pvParameters;
    int acc_idx = 0;
    size_t buf_len = 0;
    int acc_len = INITIAL_ACCUMULATOR_LEN;
    uint8_t *acc = malloc(INITIAL_ACCUMULATOR_LEN);
    
    if (acc == NULL) {
        ESP_LOGE(TAG, "Failed to allocate initial accumulator buffer");
        vTaskDelete(NULL);
        return;
    }

    while (1) {
        // Data coming from serial is COB encoded
        if (uart_get_buffered_data_len(sctx->uart_port, &buf_len) == ESP_OK && buf_len > 0) {
            uint8_t encoded_data[buf_len];
            int len = uart_read_bytes(sctx->uart_port, encoded_data, sizeof(encoded_data), pdMS_TO_TICKS(100));
            
            
            for (int i = 0; i < len; i++) {
                // Expand accumulator as needed
                if (acc_idx >= acc_len) {
                    int new_len = acc_len * 2;
                    uint8_t *new_acc = realloc(acc, new_len);
                    if (new_acc) {
                        acc = new_acc;
                        acc_len = new_len;
                    } else {
                        ESP_LOGE(TAG, "Failed to expand accumulator");
                        acc_idx = 0;
                        break;
                    }
                }
                
                acc[acc_idx++] = encoded_data[i];
                
                // Frame delimiter found
                if (encoded_data[i] == 0x00) {

                    //LOG_HEX(TAG, "RX Encoded:", acc, acc_idx);

                    uint8_t decoded_data[cob_decoded_max_size(acc_idx)];
                    size_t decoded_size = 0;
                    cob_result_t result = cob_decode(acc, acc_idx, decoded_data, sizeof(decoded_data), &decoded_size);
                    
                    if (result == COB_OK && decoded_size > 0) {
                        //LOG_HEX(TAG, "RX Decoded:", decoded_data, decoded_size);
                        uint8_t rmtype = get_jmp_msg_type(decoded_data, decoded_size);
                        ESP_LOGI(TAG, "RX Message Type: %s", jmp_msg_type_to_str(rmtype));


                        queue_pkt_t pkt = {
                            .payload = malloc(decoded_size),
                            .payload_tag = 0,
                            .payload_len = decoded_size
                        };
                        
                        if (pkt.payload) {
                            memcpy(pkt.payload, decoded_data, decoded_size);
                            if (xQueueSend(ucore_queue, &pkt, pdMS_TO_TICKS(1000)) != pdTRUE) {
                                ESP_LOGW(TAG, "Failed to queue packet");
                                free(pkt.payload);
                            }
                        } else {
                            ESP_LOGE(TAG, "Failed to allocate packet payload");
                        }
                    }
                
                    
                    acc_idx = 0;
                }
            }           
        }

        // Shrink accumulator as needed
        if (acc_len > INITIAL_ACCUMULATOR_LEN && acc_idx < acc_len / 2) {
            int target_size = acc_len / 2;
            if (target_size < INITIAL_ACCUMULATOR_LEN)
                target_size = INITIAL_ACCUMULATOR_LEN;

            uint8_t *shrunk_acc = realloc(acc, target_size);
            if (shrunk_acc) {
                acc = shrunk_acc;
                acc_len = target_size;
            }
        } 

        vTaskDelay(pdMS_TO_TICKS(10));
    }

    free(acc);
    vTaskDelete(NULL);
}

void *esp32_serial_connect(void *args) {
    // Allocate context
    serial_ctx_t *sctx = malloc(sizeof(serial_ctx_t));
    if (sctx == NULL) {
        ESP_LOGE(TAG, "Failed to allocate serial context");
        return NULL;
    }
    
    sctx->uart_port = ((transport_args_t *)args)->uart_port_no;

    sctx->serial_task_handle = NULL;

    // Install UART driver
    esp_err_t err = uart_driver_install(
        sctx->uart_port, UART_BUF_SIZE, UART_BUF_SIZE, 10, NULL, 0
    );
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "UART driver install failed: %d", err);
        free(sctx);
        return NULL;
    }
    
    // Configure UART parameters
    uart_config_t uart_config = {
        .baud_rate = UART_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_CTS_RTS,
        .source_clk = UART_SCLK_DEFAULT,
    };
    err = uart_param_config(sctx->uart_port, &uart_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "UART param config failed: %d", err);
        uart_driver_delete(sctx->uart_port);
        free(sctx);
        return NULL;
    }

    // Start serial task
    BaseType_t task_created = xTaskCreate(
        serial_task, 
        "serial_task", 
        4096, 
        sctx, 
        5, 
        &sctx->serial_task_handle
    );
    
    if (task_created != pdPASS) {
        ESP_LOGE(TAG, "Failed to create serial task");
        uart_driver_delete(sctx->uart_port);
        free(sctx);
        return NULL;
    }

    g_serial_ctx = sctx;
    ESP_LOGI(TAG, "Serial transport initialized on UART%d", sctx->uart_port);
    return sctx;
}

void esp32_serial_disconnect(void *ctx) {
    if (ctx == NULL) return;
    
    serial_ctx_t *sctx = (serial_ctx_t *)ctx;
    
    // Stop serial task
    if (sctx->serial_task_handle != NULL) {
        vTaskDelete(sctx->serial_task_handle);
        sctx->serial_task_handle = NULL;
    }
    
    // Uninstall UART driver
    uart_driver_delete(sctx->uart_port);
    
    if (g_serial_ctx == sctx) {
        g_serial_ctx = NULL;
    }
    
    free(sctx);
    ESP_LOGI(TAG, "Serial transport disconnected");
}

void esp32_serial_stop(void *ctx) {
    esp32_serial_disconnect(ctx);
}

void esp32_comm_notification_task(void *pvParameters) {
    comms_task_map_t *comm_map = NULL;
    for (int i = 0; i < UCORE_MAX_COMM_TASKS; i++) {
        if (kcontext.comms_tasks[i].request_type == UCORE_COMMS_NOTIFICATION) {
            comm_map = &kcontext.comms_tasks[i];
            break;
        }
    }

    if (comm_map == NULL) {
        ESP_LOGE(TAG, "No notification comm task found");
        vTaskDelete(NULL);
        return;
    }

    uint32_t notify_value = 0;
    while (1) {
        BaseType_t notified = xTaskNotifyWait(0, 0, &notify_value, pdMS_TO_TICKS(60000));

        if (notified == pdTRUE && notify_value == 0) {
            break; // Exit signal
        }

        jmp_comm_notification_msg_t notif = {
            .comm_id_len = comm_map->comm_open.comm_id_len,
            .comm_id = comm_map->comm_open.comm_id,
            .to_restart = 0,
            .restarted = 0,
            .free_heap = esp_get_free_heap_size(),
            .uptime = esp_timer_get_time() / 1000000,
        };

        size_t content_max_len = notif.comm_id_len + sizeof(jmp_comm_notification_msg_t);
        size_t out_len;
        uint8_t content[content_max_len];
        jmp_serialize_comm_notification_msg(content, content_max_len, &notif, &out_len);

        ucore_send_async(JMP_COMM_MSG, content, out_len);
    }
    
    vTaskDelete(NULL);
}

static transport_t serial_transport = {
    .connect    = esp32_serial_connect,
    .status     = esp32_serial_status,
    .bin_tx     = esp32_serial_bin_tx,
    .disconnect = esp32_serial_disconnect,
    .stop       = esp32_serial_stop
};

void platform_init(void) {    
    serial_write_mutex = xSemaphoreCreateMutex();
    kcontext.transport = serial_transport;
    kcontext.transport_ctx = NULL;
}