#ifndef UCORE_H_
#define UCORE_H_

#include <stdbool.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#include "jmp_bin.h"

// Macro definitions
#define MAX_NETWORK_LOG_LEN               128
#define MAX_UCORE_TASK_QUEUE_LEN          5
#define MAX_MPYRUNTIME_TASK_QUEUE_LEN     5
#define MAX_WEBSOCKET_REQUEST_LEN         1024
#define MAX_MICROPY_RUNTIME_CODE_LEN      1024

#define QUEUE_GENERIC_LEN              10

// Feature toggles
#define UCORE_BUNDLE_UPIP                 0
#define UCORE_BUNDLE_USHELL               0
#define UCORE_BUNDLE_BUSYBOX              0

#define UCORE_MAX_ID_LEN  37 //no id generator returns id more than this lenght (including \0)

#if ((UCORE_BUNDLE_BUSYBOX == 1) || (UCORE_BUNDLE_UPIP == 1)) && (UCORE_BUNDLE_USHELL == 0)
    #error "UCORE_BUNDLE_USHELL must be set if UCORE_BUNDLE_BUSYBOX or UCORE_BUNDLE_UPIP is set"
#endif

//kernel info
#define UCORE_KERNEL_IMPLEMENTATION     "micropython"
#define UCORE_KERNEL_IMPLEMENTATION_LEN 11
#define UCORE_KERNEL_LANGUAGE_NAME      "MicroPython"
#define UCORE_KERNEL_LANGUAGE_NAME_LEN  11
#define UCORE_KERNEL_MIMETYPE           "text/x-python"
#define UCORE_KERNEL_MIMETYPE_LEN       13
#define UCORE_KERNEL_FILE_EXTENSION     ".py"
#define UCORE_KERNEL_FILE_EXTENSION_LEN 3
#define UCORE_KERNEL_BANNER             "MicroPython Kernel for Jupyter"
#define UCORE_KERNEL_BANNER_LEN         30
#define UCORE_KERNEL_IMPLEMENATION_VERSION {1, 0, 0}
#define UCORE_KERNEL_JMP_VERSION {5, 3, 0}
#define UCORE_MICROPYTHON_VERSION {0, 1, 0}
//#define RELAY_SERVER_ENDPOINT "ws://12.23.12.34/device"

//SANS = server assigned notebook prefix len
#define UCORE_SANS_PREFIX_LEN (36)

//generic type for sending messages via queues
typedef struct {
    void *payload; 
    size_t payload_tag; 
    size_t payload_len;
} queue_pkt_t;


// Queue handles
extern QueueHandle_t stdin_queue;
extern QueueHandle_t ucore_queue;
extern QueueHandle_t iopub_queue;
extern QueueHandle_t control_queue;
extern QueueHandle_t mpyruntime_queue;


#if UCORE_BUNDLE_USHELL == 1
extern QueueHandle_t ushell_queue;
#endif

//sideeffects use the parent request context to append parent headers, ids, etc
#define UCORE_MAX_COMM_TASKS 10
#define UCORE_MAX_COMM_ID_NAME_LEN 50

typedef struct {
    jmp_message_t *msg;
    jmp_header_t *header; 
    uint8_t *payload; 
    uint8_t payload_len; 

} request_context_t; 

//defines pluggable transport functions 
//so we can support various media and architectures
//it is expected that transport.connect() returns a ctx 
typedef struct {
    void *(*connect)(void *args); 
    bool (*status)(void *ctx);
    int (*bin_tx)(void *ctx, uint8_t *payload, int len);
    void (*disconnect)(void *ctx);
    void (*stop)(void *ctx);
} transport_t;

typedef struct {
    void *(*time)(void *args);
} utils_t; 

typedef struct { 
    int request_type;
    TaskFunction_t task_function;
    const char *task_name;
    jmp_comm_open_t comm_open;
    bool instance_active;
    TaskHandle_t instance_handle;
} comms_task_map_t;

typedef struct{
    uint16_t execution_count;
    uint16_t soft_reset_count;
    request_context_t *current_req;
    comms_task_map_t comms_tasks[UCORE_MAX_COMM_TASKS];
    
    utils_t utils; 
    transport_t transport; 
    void *transport_ctx;
    void (*keyboard_interrupt_fn)(void);   // callback for runtime-level keyboard interrupt
} execution_context_t;

typedef enum{
    UCORE_COMMS_NOTIFICATION,
} ucore_comms_request_type_t;


void ucore_register_comm_task(int request_type, TaskFunction_t task_function, const char *task_name);

extern execution_context_t kcontext;
/**
 * Starts the ucore task.
 */

// uint32_t ucore_make_id();
size_t ucore_uuid(char *uuid_str);

int iopub_print(const char *str, int len);
void iopub_status(uint8_t execution_state);

// void ucore_hmac_sha256(const uint8_t *key, size_t key_len,
//     const uint8_t *data, size_t data_len,
//     uint8_t *output);

// int ucore_time_sync(void);    

char *ucore_raw_input(const char *prompt);
void ucore_update_status(uint8_t execution_state, request_context_t *pctx);
int ucore_start(void *args);


int ucore_send_async(int msg_type, uint8_t* content, int content_len);

int ucore_send_reply(request_context_t *req_ctx, uint8_t msg_type,
                     const uint8_t *content, size_t content_len);

// Install a transport backend. Must be called before ucore_start().
// The transport_t is copied into kcontext, so the caller can pass a
// pointer to a const global vtable exported by the backend.
void ucore_register_transport(const transport_t *t);

#endif // UCORE_H_
