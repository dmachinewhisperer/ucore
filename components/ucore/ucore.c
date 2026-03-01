#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "ucore/ucore.h"
#include "ucore/jmp_bin.h"

QueueHandle_t stdin_queue;
QueueHandle_t ucore_queue;
QueueHandle_t iopub_queue;
QueueHandle_t control_queue;

// global state of execution environment
execution_context_t kcontext = {0};

//helpers
size_t ucore_uuid(char *uuid_str) {
    uint8_t uuid[16];
    for (int i = 0; i < 16; i++) {
        uuid[i] = rand() & 0xFF;
    }

    // version 4 = 0100xxxx
    uuid[6] = (uuid[6] & 0x0F) | 0x40;

    // variant = 10xxxxxx
    uuid[8] = (uuid[8] & 0x3F) | 0x80;

    return snprintf(uuid_str, 37,
        "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
        uuid[0], uuid[1], uuid[2], uuid[3],
        uuid[4], uuid[5],
        uuid[6], uuid[7],
        uuid[8], uuid[9],
        uuid[10], uuid[11], uuid[12], uuid[13], uuid[14], uuid[15]);
}

// all payload sent to the client side kernel carries a prefix 
// this prefix is aside from the jmp protocol. it is used for supporting 
// multi client programming one device. the prefix is a server-level session id
// (uuid4) note that this is different from that which the kernel assignes (header.session)
// server uses this to route messages to the correct device
//server here is the entity orchestrating comms between mulit client and device kernel

//device side kernel refers to the part of kernel running on the device that executes code
//client side kernel refers to the part of the kernel running on the client that interacts with the jupyter frontend
//both are connected by a transport (websocket, serial, etc)
//TODO: fill in usernames fields

//sends async messages that are not tied to any request
//this message is broadcast to all connected clients by the client side kernel
int ucore_send_async(int msg_type, uint8_t* content, int content_len){

    size_t header_len = 0, offset = 0;
    size_t max_len = UCORE_SANS_PREFIX_LEN + JMP_MSG_PREFIX_LEN + 
                        sizeof(jmp_header_t) + 2 * UCORE_MAX_ID_LEN + content_len;
    
    uint8_t *payload = malloc(max_len);
    if (!payload) {
        return -1;
    }
    char uuid[UCORE_MAX_ID_LEN];
    jmp_header_t header = {
        .msg_id_len = (uint16_t)ucore_uuid(uuid),
        .msg_id = (uint8_t*)uuid,

        .session_id_len = 0,
        .session_id = NULL,

        .username_len = 0,
        .username = NULL,
        .msg_type = msg_type,
        .version = UCORE_KERNEL_JMP_VERSION,
    };
    offset += UCORE_SANS_PREFIX_LEN; 
    offset += JMP_MSG_PREFIX_LEN; 

    jmp_serialize_header(payload + offset, max_len - offset, &header, &header_len);
    offset += header_len; 
  
    memcpy(payload + offset, content, content_len);
    offset += content_len;

    jmp_add_msg_prefix(payload + UCORE_SANS_PREFIX_LEN, JMP_MSG_PREFIX_LEN, header_len, 0, 0, content_len, 0);

    //TODO: for multi programming environemnts, sans prefix must be set correctly
    //for now we just set it to zero
    //alternatively, we can modify the client side kernel to broadcast to all connected sessions
    memset(payload, 0, UCORE_SANS_PREFIX_LEN);
    //memcpy(payload, kcontext.current_req->payload, UCORE_SANS_PREFIX_LEN);

    int len = kcontext.transport.bin_tx(kcontext.transport_ctx, payload, offset);
    free(payload);
    return len;
}

// sends status but does not depend on iopub task
void ucore_update_status(uint8_t execution_state, request_context_t *pctx){
    // note that max_len is not necessarily the size of jmp_status_t that we know of
    // due to possible padding by the compiler, always use len output of *_serialze_ functions
    if(!pctx){
        return;
    }

    size_t header_len = 0, content_len, offset = 0; 
    
    // account for prefixes + header + its dynamic fields, content and parent header (if present)
    size_t max_len = UCORE_SANS_PREFIX_LEN + JMP_MSG_PREFIX_LEN + sizeof(jmp_header_t) + 2 * UCORE_MAX_ID_LEN +
                        sizeof(jmp_status_t) + pctx->msg->header_len;
    
    uint8_t *payload = malloc(max_len);
    if (!payload) {
        return;
    }
    char uuid[UCORE_MAX_ID_LEN];
    jmp_header_t header = {
        .msg_id_len = (uint16_t)ucore_uuid(uuid),
        .msg_id = (uint8_t*)uuid,

        .session_id_len = pctx->header->session_id_len,
        .session_id = pctx->header->session_id,

        .username_len = 0,
        .username = NULL,
        .msg_type = JMP_STATUS,
        .version = UCORE_KERNEL_JMP_VERSION,
    };
    //skip over space for prefixes
    offset += UCORE_SANS_PREFIX_LEN; 
    offset += JMP_MSG_PREFIX_LEN; 

    //add headers
    jmp_serialize_header(payload + offset, max_len - offset, &header, &header_len);
    offset += header_len; 
    memcpy(payload + offset, pctx->msg->header, pctx->msg->header_len);
    offset += pctx->msg->header_len;

    jmp_status_t status = {
        .execution_state = execution_state,
    }; 
    jmp_serialize_status(payload + offset, max_len - offset, &status, &content_len);
    offset += content_len; 
    
    //add message and sans prefixes
    jmp_add_msg_prefix(payload + UCORE_SANS_PREFIX_LEN, JMP_MSG_PREFIX_LEN, header_len, pctx->msg->header_len, 0, content_len, 0);
    memcpy(payload, pctx->payload, UCORE_SANS_PREFIX_LEN);

    
    kcontext.transport.bin_tx(kcontext.transport_ctx, payload, offset);
    free(payload);
}

//comm handlers are intended to be long-running tasks; comm_open launches them (if not already running)
//comm_data notifies them of new data using freertos notifications while comm_close signals them to exit

//comms facility is meant to be a way the kernel updates the frontend of events that are not direct responses to requests
//such events are mostly architecture specific, so the tasks that handle them must be implemented by specific ports
//tasks that handle comms must be implemented by specific ports and the tasks registered with the function below
//comms supported by the frontend are defined in the jmp specs and ports may choose which ones to support

//all comms task needs to handle the 0x0000 notification. this is a task shutdown notification. 
//data from comm_data is passed to the task via notifications and it must send back request to comms facility
void ucore_register_comm_task(int request_type, TaskFunction_t task_function, const char *task_name){
    static int map_index = 0;
    if(map_index < UCORE_MAX_COMM_TASKS){
        kcontext.comms_tasks[map_index].request_type = request_type;
        kcontext.comms_tasks[map_index].task_function = task_function;
        kcontext.comms_tasks[map_index].task_name = task_name;
        map_index++;
    }
}


// TODO: fix potential race in accessing the global execution context
// TODO: jupyter notebook sends retry messages at a rate the chip cannot handle if requests
// are not serviced between status busy and idle. need to stop this spamming at the relay server
// note: this task needs to run with a very high relative priority so that it can preempt
// main execution threads as it publishes side effects these threads emit
void iopub_task(){
    queue_pkt_t pkt = {0}; 

    // we expect from the queue pkt.payload to be a heap allocated content of the 
    // iopub msg, with lenght stored in pkt.payload_len and msg_type in pkt.payload_tag
    while(xQueueReceive(iopub_queue, &pkt, portMAX_DELAY) == pdTRUE){
        if(!kcontext.current_req){
            // if there is not outstanding request, side effects and statuses should not fire
            //ESP_LOGE(TAG, "kcontext.current_req is null");
            goto loop_cleanup;
        }

        size_t header_len = 0, offset = 0, max_len = 0; 
        max_len = UCORE_SANS_PREFIX_LEN + JMP_MSG_PREFIX_LEN + sizeof(jmp_header_t) + 2 * UCORE_MAX_ID_LEN +
                    kcontext.current_req->msg->header_len + pkt.payload_len; 

        uint8_t *payload = malloc(max_len);
        if (!payload) {
            goto loop_cleanup;
        }
        
        char uuid[UCORE_MAX_ID_LEN];
        jmp_header_t header = {
            .msg_id_len = (uint16_t)ucore_uuid(uuid),
            .msg_id = (uint8_t*)uuid,
            .session_id_len = kcontext.current_req->header->session_id_len,
            .session_id = kcontext.current_req->header->session_id, 
            .username_len = 0,
            .username = NULL,
            .msg_type = pkt.payload_tag,
            .version = UCORE_KERNEL_JMP_VERSION,
        };

        offset += UCORE_SANS_PREFIX_LEN; 
        offset +=JMP_MSG_PREFIX_LEN;
        jmp_serialize_header(payload + offset, max_len - offset, &header, &header_len);
        offset += header_len; 

        memcpy(payload + offset, kcontext.current_req->msg->header, kcontext.current_req->msg->header_len);
        offset += kcontext.current_req->msg->header_len;

        memcpy(payload + offset, pkt.payload, pkt.payload_len);
        offset += pkt.payload_len;

        jmp_add_msg_prefix(payload + UCORE_SANS_PREFIX_LEN, JMP_MSG_PREFIX_LEN, header_len, kcontext.current_req->msg->header_len, 0, pkt.payload_len, 0);
        memcpy(payload, kcontext.current_req->payload, UCORE_SANS_PREFIX_LEN);

        
        kcontext.transport.bin_tx(kcontext.transport_ctx, payload, offset);
        free(payload);

loop_cleanup:        
        if(pkt.payload) free(pkt.payload);
        pkt.payload = NULL;
    }
}

void iopub_status(uint8_t execution_state){    

    size_t content_len = 0, max_len = sizeof(jmp_status_t);

    uint8_t *content = malloc(max_len);
    if(!content){
        return; 
    }
    jmp_status_t status = {
        .execution_state = execution_state,
    }; 
    jmp_serialize_status(content, max_len, &status, &content_len);

    queue_pkt_t spkt = {
        .payload = content,
        .payload_tag = JMP_STATUS,
        .payload_len = content_len,
    };

    // ownership of content is transferred to the queue consumer
    // status is now required starting jmp spec 5+ so we must wait until we can send it
    if (xQueueSend(iopub_queue, &spkt, portMAX_DELAY) != pdPASS) {
        free(content);
    }

}
int iopub_print(const char *text, int text_len){    
    size_t content_len = 0, max_len = sizeof(jmp_stream_t) + text_len;
    uint8_t *content = malloc(max_len);
    if(!content){
        return 0; 
    }
    jmp_stream_t stream = {
        .name = STDOUT,
        .text_len = text_len,
        .text = text,
    };
    int ret_val = jmp_serialize_stream(content, max_len, &stream, &content_len);

    printf("content_len=%d, text_len=%d, ret_val=%d, text=", content_len, text_len, ret_val);
    for(int i = 0; i<text_len; i++){
        printf("%c", text[i]);
    }

    queue_pkt_t spkt = {
        .payload = (void*)content,
        .payload_tag = JMP_STREAM,
        .payload_len = content_len,
    };

    // streams are expected to be high volume it is okay if we drop a few (for now)
    if (xQueueSend(iopub_queue, &spkt, pdMS_TO_TICKS(100)) != pdPASS) {
        free(content);
        return 0;
    }
    return text_len;

}

//TODO: add checks to ensure kcontext.current_req is not NULL before dereferencing
char *ucore_raw_input(const char *prompt){
    //send an input request and block on stdin until frontend returns something. 

    size_t header_len = 0,content_len=0, offset = 0, max_len = 0; 
    // max_len = UCORE_SANS_PREFIX_LEN + JMP_MSG_PREFIX_LEN + sizeof(jmp_header_t) + 2 * UCORE_MAX_ID_LEN +
    //                     sizeof(jmp_input_request_t) + strlen(prompt);
    max_len = UCORE_SANS_PREFIX_LEN + JMP_MSG_PREFIX_LEN + 
                     sizeof(jmp_header_t) + 2 * UCORE_MAX_ID_LEN + strlen(prompt) +
                     kcontext.current_req->msg->header_len + sizeof(jmp_input_request_t);                    
    uint8_t *payload = malloc(max_len);
    if (!payload) {
        return NULL;
    }

    char uuid[UCORE_MAX_ID_LEN];
    jmp_header_t header = {
        .msg_id_len = (uint16_t)ucore_uuid(uuid),
        .msg_id = (uint8_t*)uuid,
        .session_id_len = kcontext.current_req->header->session_id_len,
        .session_id = kcontext.current_req->header->session_id, 
        .username_len = 0,
        .username = NULL,
        .msg_type = JMP_INPUT_REQUEST,
        .version = UCORE_KERNEL_JMP_VERSION,
    };

    offset += UCORE_SANS_PREFIX_LEN; 
    offset +=JMP_MSG_PREFIX_LEN;
    jmp_serialize_header(payload + offset, max_len - offset, &header, &header_len);
    offset += header_len; 
    memcpy(payload +offset, kcontext.current_req->msg->header, kcontext.current_req->msg->header_len);
    offset += kcontext.current_req->msg->header_len;

    jmp_input_request_t input_req = {
        .prompt_len = strlen(prompt),
        .prompt = (uint8_t*)prompt,
        .password = 0,
    }; 
    jmp_serialize_input_request(payload + offset, max_len - offset, &input_req, &content_len);
    offset += content_len; 
    jmp_add_msg_prefix(payload + UCORE_SANS_PREFIX_LEN, JMP_MSG_PREFIX_LEN, header_len, kcontext.current_req->msg->header_len, 0, content_len, 0);
    memcpy(payload, kcontext.current_req->payload, UCORE_SANS_PREFIX_LEN);

    
    kcontext.transport.bin_tx(kcontext.transport_ctx, payload, offset);
    free(payload);
    
    queue_pkt_t pkt;
    xQueueReceive(stdin_queue, &pkt, portMAX_DELAY);  //we expect the content of a input_reply ppacket
    
    jmp_input_reply_t input_rep; 
    jmp_deserialize_input_reply(pkt.payload, pkt.payload_len, &input_rep, &content_len);

    char *str = malloc(input_rep.value_len + 1);
    memcpy(str, input_rep.value, input_rep.value_len);
    str[input_rep.value_len] = '\0';
    free(pkt.payload);
    return str;

}

// ---- Reply helper ----
int ucore_send_reply(request_context_t *req_ctx, uint8_t msg_type,
                     const uint8_t *content, size_t content_len) {

    size_t header_len = 0, offset = 0;
    size_t max_len = UCORE_SANS_PREFIX_LEN + JMP_MSG_PREFIX_LEN +
                     sizeof(jmp_header_t) + 2 * UCORE_MAX_ID_LEN +
                     req_ctx->msg->header_len + content_len;

    uint8_t *payload = malloc(max_len);
    if (!payload) {
        return -1;
    }

    char uuid[UCORE_MAX_ID_LEN];
    jmp_header_t header = {
        .msg_id_len = (uint16_t)ucore_uuid(uuid),
        .msg_id = (uint8_t*)uuid,
        .session_id_len = req_ctx->header->session_id_len,
        .session_id = req_ctx->header->session_id,
        .username_len = 0,
        .username = NULL,
        .msg_type = msg_type,
        .version = UCORE_KERNEL_JMP_VERSION,
    };

    offset += UCORE_SANS_PREFIX_LEN;
    offset += JMP_MSG_PREFIX_LEN;
    jmp_serialize_header(payload + offset, max_len - offset, &header, &header_len);
    offset += header_len;

    memcpy(payload + offset, req_ctx->msg->header, req_ctx->msg->header_len);
    offset += req_ctx->msg->header_len;

    if (content && content_len > 0) {
        memcpy(payload + offset, content, content_len);
        offset += content_len;
    }

    jmp_add_msg_prefix(payload + UCORE_SANS_PREFIX_LEN, JMP_MSG_PREFIX_LEN,
                       header_len, req_ctx->msg->header_len, 0, content_len, 0);
    memcpy(payload, req_ctx->payload, UCORE_SANS_PREFIX_LEN);

    int ret = kcontext.transport.bin_tx(kcontext.transport_ctx, payload, offset);
    free(payload);
    return ret;
}

// ---- JMP Message Handlers (handled directly by ucore_task) ----

static void handle_kernel_info(request_context_t *req_ctx) {
    iopub_status(KERNEL_BUSY);

    jmp_kernel_info_reply_t info_rep = {
        .status = STATUS_OK,
        .protocol_version = UCORE_KERNEL_JMP_VERSION,

        .implementation_len = UCORE_KERNEL_IMPLEMENTATION_LEN,
        .implementation = (uint8_t *)UCORE_KERNEL_IMPLEMENTATION,
        .implementation_version = UCORE_KERNEL_IMPLEMENATION_VERSION,

        .lang_name_len = UCORE_KERNEL_LANGUAGE_NAME_LEN,
        .lang_name = (uint8_t *)UCORE_KERNEL_LANGUAGE_NAME,
        .lang_version = UCORE_MICROPYTHON_VERSION,

        .mimetype_len = UCORE_KERNEL_MIMETYPE_LEN,
        .mimetype = (uint8_t *)UCORE_KERNEL_MIMETYPE,

        .file_ext_len = UCORE_KERNEL_FILE_EXTENSION_LEN,
        .file_extension = (uint8_t *)UCORE_KERNEL_FILE_EXTENSION,

        .banner_len = UCORE_KERNEL_BANNER_LEN,
        .banner = (uint8_t *)UCORE_KERNEL_BANNER,

        .debugger = 0,
    };

    size_t content_len = 0;
    size_t max_content = sizeof(jmp_kernel_info_reply_t) +
                         info_rep.implementation_len + info_rep.lang_name_len +
                         info_rep.mimetype_len + info_rep.file_ext_len + info_rep.banner_len;
    uint8_t *content = malloc(max_content);
    if (!content) {
        return;
    }

    jmp_serialize_kernel_info_reply(content, max_content, &info_rep, &content_len);
    ucore_send_reply(req_ctx, JMP_KERNEL_INFO_REPLY, content, content_len);
    free(content);

    iopub_status(KERNEL_IDLE);
}

static void handle_input_reply(jmp_message_t *msg) {
    uint8_t *input_rep = malloc(msg->content_len);
    if (!input_rep) {
        return;
    }
    memcpy(input_rep, msg->content, msg->content_len);
    queue_pkt_t ipkt = {
        .payload = (void*)input_rep,
        .payload_len = msg->content_len,
        .payload_tag = JMP_INPUT_REPLY,
    };
    if (xQueueSend(stdin_queue, &ipkt, pdMS_TO_TICKS(1000)) != pdPASS) {
        free(input_rep);
    }
}

static void handle_interrupt(request_context_t *req_ctx) {
    iopub_status(KERNEL_BUSY);
    //vTaskDelay(pdMS_TO_TICKS(1000)); //delay a bit to make sure busy status goes first

    // use registered callback to interrupt the runtime
    // this cannot be forwarded via queue because the runtime task may be busy executing code
    if (kcontext.keyboard_interrupt_fn) {
        kcontext.keyboard_interrupt_fn();
    }

    jmp_interrupt_reply_t interrupt_rep = {
        .status = STATUS_OK,
    };
    size_t content_len = 0;
    uint8_t content[sizeof(jmp_status_t)];
    //jmp_status_t has same pack format as jmp_interrupt_reply_t
    jmp_serialize_status(content, sizeof(content), (jmp_status_t*)&interrupt_rep, &content_len);
    ucore_send_reply(req_ctx, JMP_KERNEL_INFO_REPLY, content, content_len);

    iopub_status(KERNEL_IDLE);
}

//server control messages
static void handle_auth_reply(jmp_message_t *msg) {
    uint8_t *auth_rep = malloc(msg->content_len);
    if (!auth_rep) {
        return;
    }

    memcpy(auth_rep, msg->content, msg->content_len);
    queue_pkt_t apkt = {
        .payload = (void*)auth_rep,
        .payload_len = msg->content_len,
        .payload_tag = JMP_AUTH_REPLY,
    };
    if (xQueueSend(control_queue, &apkt, pdMS_TO_TICKS(1000)) != pdPASS) {
        free(auth_rep);
    }
}

static void handle_comm_open(request_context_t *req_ctx) {
    size_t out_len = 0;
    jmp_comm_open_t comm_open; 
    jmp_deserialize_comm_open(req_ctx->msg->content, req_ctx->msg->content_len, &comm_open, &out_len);
    
    for(int i = 0; i < UCORE_MAX_COMM_TASKS; i++){
        if( kcontext.comms_tasks[i].request_type == comm_open.target_id && kcontext.comms_tasks[i].instance_active == false){
            kcontext.comms_tasks[i].comm_open = comm_open;
            if(comm_open.comm_id_len > UCORE_MAX_COMM_ID_NAME_LEN){
                return;
            }
            kcontext.comms_tasks[i].comm_open.comm_id = malloc(comm_open.comm_id_len);
            if (kcontext.comms_tasks[i].comm_open.comm_id) {
                memcpy(kcontext.comms_tasks[i].comm_open.comm_id, comm_open.comm_id, comm_open.comm_id_len);

                // there is opportunity to pass comm_open data field to the 
                // task but for now jmp_bin does not support data fields for comm_open
                TaskHandle_t task_handle = NULL;
                BaseType_t result = xTaskCreate(
                    kcontext.comms_tasks[i].task_function,
                    kcontext.comms_tasks[i].task_name,
                    configMINIMAL_STACK_SIZE*4,
                    NULL,
                    tskIDLE_PRIORITY + 1,
                    &task_handle
                );
                if(result == pdPASS){
                    kcontext.comms_tasks[i].instance_active = true;
                    kcontext.comms_tasks[i].instance_handle = task_handle;
                    kcontext.comms_tasks[i].comm_open = comm_open;
                }
            }
            //TODO: jmp specifies that backend needs to send a comm_close if a comm cannot be opened
        }
            break;
    
    }
}

static void handle_comm_msg(request_context_t *req_ctx) {
    size_t out_len = 0;
    jmp_comm_msg_t comm_msg; 
    jmp_deserialize_comm_msg(req_ctx->msg->content, req_ctx->msg->content_len, &comm_msg, &out_len);

    for(int i = 0; i < UCORE_MAX_COMM_TASKS; i++){
        if(kcontext.comms_tasks[i].comm_open.comm_id_len == comm_msg.comm_id_len &&
           memcmp(kcontext.comms_tasks[i].comm_open.comm_id, comm_msg.comm_id, comm_msg.comm_id_len) == 0){
            if(comm_msg.data > 0){
                //filter out comm_msg.data == 0 which is reserved for exit
                xTaskNotify(kcontext.comms_tasks[i].instance_handle, comm_msg.data, eSetValueWithOverwrite);
            }
            break;
        }
    }
}

static void handle_comm_close(request_context_t *req_ctx) {
    size_t out_len = 0;
    jmp_comm_close_t comm_close; 
    jmp_deserialize_comm_close(req_ctx->msg->content, req_ctx->msg->content_len, &comm_close, &out_len);
    for(int i = 0; i < UCORE_MAX_COMM_TASKS; i++){
        if(kcontext.comms_tasks[i].comm_open.comm_id_len == comm_close.comm_id_len &&
           memcmp(kcontext.comms_tasks[i].comm_open.comm_id, comm_close.comm_id, comm_close.comm_id_len) == 0){
            kcontext.comms_tasks[i].instance_active = false;
            free(kcontext.comms_tasks[i].comm_open.comm_id);
            kcontext.comms_tasks[i].comm_open.comm_id = NULL;

            //notification with value 0 means exit
            xTaskNotify(kcontext.comms_tasks[i].instance_handle, 0, eSetValueWithOverwrite);
            break;
        }
    }
}

// ---- Main ucore task ----

void ucore_task(void *pvParameters){
    queue_pkt_t pkt = {0};
    while(xQueueReceive(ucore_queue, &pkt, portMAX_DELAY) == pdTRUE){
        // we expect a stream of heap allocated bytes from the transport media and  pkt.payload can be null. 
        if(pkt.payload == NULL){
            continue;
        }

        jmp_message_t msg; 
        jmp_dissassemble_message((uint8_t*)pkt.payload + UCORE_SANS_PREFIX_LEN, pkt.payload_len - UCORE_SANS_PREFIX_LEN, &msg);
        jmp_header_t req_header; 
        size_t out_len = 0;
        jmp_deserialize_header(msg.header, msg.header_len, &req_header, &out_len);

        // forward messages that need micropython runtime context to the runtime task
        if(req_header.msg_type == JMP_EXECUTE_REQUEST || 
           req_header.msg_type == JMP_SHUTDOWN_REQUEST ||
           req_header.msg_type == JMP_COMPLETE_REQUEST){
            if (xQueueSend(mpyruntime_queue, &pkt, pdMS_TO_TICKS(1000)) != pdPASS) {
                goto loop_cleanup;
            }
            continue;
        }

        // pass-through messages: forward to internal queues without touching kcontext.current_req
        // these may arrive while mp_task is actively using kcontext.current_req (e.g., during input())
        // overwriting or NULLing it would break iopub delivery for the in-progress execution
        if(req_header.msg_type == JMP_INPUT_REPLY){
            handle_input_reply(&msg);
            free(pkt.payload);
            pkt.payload = NULL;
            continue;
        }

        if(req_header.msg_type == JMP_AUTH_REPLY){
            handle_auth_reply(&msg);
            free(pkt.payload);
            pkt.payload = NULL;
            continue;
        }

        // setting the global kcontext state must be done on the task level
        // some requests are outsourced to other tasks that service them
        // they must also set the kcontext before starting the service the requests
        // it looks like there is going to be nasty race condition here but you have to 
        // know that jmps specifies that all request-reply and sideeffects must arrive between 
        // status busy and idle. 
        request_context_t req_ctx = {
            .msg = &msg,
            .header = &req_header,
            .payload = (uint8_t*)pkt.payload,
            .payload_len = pkt.payload_len,
        };
        
        kcontext.current_req = &req_ctx;
        
        switch(req_header.msg_type){
            case JMP_KERNEL_INFO_REQUEST: handle_kernel_info(&req_ctx); break;
            case JMP_INTERRUPT_REQUEST:   handle_interrupt(&req_ctx); break;
            case JMP_COMM_OPEN:           handle_comm_open(&req_ctx); break;
            case JMP_COMM_MSG:            handle_comm_msg(&req_ctx); break;
            case JMP_COMM_CLOSE:          handle_comm_close(&req_ctx); break;
            case TARGET_NOT_FOUND:        break;
            default:                      break;
        }

loop_cleanup:
        kcontext.current_req = NULL;
        free(pkt.payload);
        pkt.payload = NULL;


    }
}

// starts the queues, tasks and authenticates with the jupyter server-side kernel. 
// need to pass the args to pass to the transport connect function 
int ucore_start(void *args){
    ucore_queue = xQueueCreate(QUEUE_GENERIC_LEN, sizeof(queue_pkt_t));
    stdin_queue = xQueueCreate(QUEUE_GENERIC_LEN, sizeof(queue_pkt_t));
    iopub_queue = xQueueCreate(QUEUE_GENERIC_LEN, sizeof(queue_pkt_t));
    control_queue = xQueueCreate(QUEUE_GENERIC_LEN, sizeof(queue_pkt_t));

    xTaskCreate(ucore_task, "ucore_task", configMINIMAL_STACK_SIZE*2, NULL, 2, NULL);
    xTaskCreate(iopub_task, "iopub_task", configMINIMAL_STACK_SIZE*2, NULL, 3, NULL);


    if (!kcontext.transport.connect ||
        !kcontext.transport.status ||
        !kcontext.transport.bin_tx ||
        !kcontext.transport.disconnect ||
        !kcontext.transport.stop) {
        
        return -1;
    }    

    kcontext.transport_ctx = kcontext.transport.connect(args);
    if(!kcontext.transport_ctx){
        return -1;
    }
    while(kcontext.transport.status(kcontext.transport_ctx) == false){
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
    
    return 0;
    //auth code    
    //uint8_t *auth_data[128];
    //int len = snprintf(auth_data, sizeof(message), "%s,%s", DEVICE_ID, timestamp);
    //uint8_t hmac_output[32];
    //ucore_hmac_sha256(SECRET_KEY, strlen(SECRET_KEY), auth_data, strlen(auth_data), hmac_output);

    /// authentication logic, for now we dont auth as frontend client now runs locally
    // size_t header_len = 0,content_len = 0, max_len=0, offset = 0; 
    // max_len = UCORE_SANS_PREFIX_LEN + JMP_MSG_PREFIX_LEN +sizeof(jmp_header_t) + 2*UCORE_MAX_ID_LEN + 
    //                     sizeof(jmp_auth_request_t) + strlen(DEVICE_ID);
    // uint8_t *payload = malloc(max_len);
    // if(!payload){
    //     return -1;
    // }

    // //server control messages dont really use msg_id and session fields
    // jmp_header_t header = {
    //     .msg_id_len = 0,
    //     .msg_id = NULL,
    //     .session_id_len = 0,
    //     .session_id = NULL, 
    //     .username_len = 0,
    //     .username = NULL,
    //     .msg_type = JMP_AUTH_REQUEST,
    //     .version = UCORE_KERNEL_JMP_VERSION,
    // };
    // jmp_auth_request_t auth_req = {
    //     .device_id_len = strlen(DEVICE_ID),
    //     .device_id = (uint8_t*)DEVICE_ID,
    //     .timestamp = 0,
    //     .hmac_sha256 = {0},
    // };

    // offset += UCORE_SANS_PREFIX_LEN; 
    // offset +=JMP_MSG_PREFIX_LEN;
    // jmp_serialize_header(payload + offset, max_len - offset, &header, &header_len);
    // offset += header_len; 
    // jmp_serialize_auth_request(payload + offset, max_len - offset, &auth_req, &content_len);
    // offset += content_len; 

    // jmp_add_msg_prefix(payload + UCORE_SANS_PREFIX_LEN, JMP_MSG_PREFIX_LEN, header_len, 0, 0, content_len, 0);

    // //the sans prefix does not mean anything to the server when device is sending 
    // //server control messages so may leave it a gibberish. 

    // kcontext.transport.bin_tx(kcontext.transport_ctx, payload, offset);
    // free(payload);

    // //we expect the content field of an auth_reponse from the queue
    // queue_pkt_t pkt = {0};
    // xQueueReceive(control_queue, &pkt, portMAX_DELAY);

    // if(pkt.payload == NULL){
    //     return -1;
    // }
   
    // jmp_auth_reply_t auth_rep;
    // jmp_deserialize_auth_reply((uint8_t*)pkt.payload, pkt.payload_len, &auth_rep, &content_len);

    // if(auth_rep.status != STATUS_OK){

    //     jmp_error_t error;
    //     jmp_deserialize_error((uint8_t*)pkt.payload, pkt.payload_len, &error, &content_len);
    //     for(int i = 0; i< error.ename_len; i++){
    //         printf("ename: %c", (char)error.ename[i]);
    //     }
    //     printf("\n");
    //     for(int i = 0; i< error.evalue_len; i++){
    //         printf("evalue: %c", (char)error.evalue[i]);
    //     }
    //     printf("\n");
    //     for(int i = 0; i< error.traceback_len; i++){
    //         printf("traceback: %c", (char)error.traceback[i]);
    //     }
    // }


    // free(pkt.payload);
    // return auth_rep.status;
}