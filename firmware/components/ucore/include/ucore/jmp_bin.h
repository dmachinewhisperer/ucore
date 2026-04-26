#ifndef JUPYTER_PROTOCOL_H
#define JUPYTER_PROTOCOL_H

#include <stdint.h>
#include <stddef.h>

// Channel IDs
#define JMP_CHANNEL_SHELL   0
#define JMP_CHANNEL_IOPUB   1
#define JMP_CHANNEL_STDIN   2
#define JMP_CHANNEL_CONTROL 3

#define JMP_MSG_PREFIX_LEN (2 * 5)
enum {
    KERNEL_BUSY = 0,
    KERNEL_IDLE,
    KERNEL_STARTING,
    KERNEL_DEAD,
};

enum {
    STATUS_OK = 0,
    STATUS_ERROR,
    STATUS_ABORT,
};

enum{
    STDOUT=0,
    STDERR,
};
typedef enum {
    JMP_KERNEL_INFO_REQUEST    = 0x01,
    JMP_KERNEL_INFO_REPLY      = 0x02,
    JMP_EXECUTE_REQUEST        = 0x03,
    JMP_EXECUTE_REPLY          = 0x04,
    JMP_STREAM                 = 0x05,
    JMP_ERROR                  = 0x06,
    JMP_DISPLAY_DATA           = 0x07,
    JMP_STATUS                 = 0x08,
    JMP_INPUT_REQUEST          = 0x09,
    JMP_INPUT_REPLY            = 0x0A,
    JMP_COMPLETE_REQUEST       = 0x0B,
    JMP_COMPLETE_REPLY         = 0x0C,
    JMP_INSPECT_REQUEST        = 0x0D,
    JMP_INSPECT_REPLY          = 0x0E,
    JMP_IS_COMPLETE_REQUEST    = 0x0F,
    JMP_IS_COMPLETE_REPLY      = 0x10,
    JMP_SHUTDOWN_REQUEST       = 0x11,
    JMP_SHUTDOWN_REPLY         = 0x12,
    JMP_INTERRUPT_REQUEST      = 0x13,
    JMP_INTERRUPT_REPLY        = 0x14,
    JMP_EXECUTE_RESULT         = 0x15,

    //comms messages
    JMP_COMM_OPEN              = 0x16,
    JMP_COMM_MSG               = 0x17,
    JMP_COMM_CLOSE             = 0x18,

    //variants of comm_msg
    //JMP_COMM_NOTIFICATION_MSG  = 0x17,

    //control messages
    JMP_AUTH_REQUEST           = 0x64,
    JMP_AUTH_REPLY             = 0x65,
    TARGET_NOT_FOUND           = 0x66,

} jmp_msg_type_t;

//TODO: reorder structs to be more memory efficient

//TODO: change strings to use a fixed type
// typedef struct {
//     uint16_t len; 
//     uint8_t *string; 
// } u16prefstring

typedef struct {
    uint16_t msg_id_len;
    uint8_t *msg_id;       // UTF-8 string (length-prefixed)

    uint16_t session_id_len;
    uint8_t *session_id;   // UTF-8 string (length-prefixed)

    uint16_t username_len;
    uint8_t *username;     // UTF-8 string (length-prefixed)

    uint8_t msg_type;      // See enum jmp_msg_type
    uint8_t version[3];    // major, minor, patch
} jmp_header_t;

typedef struct {
    uint16_t code_len;
    uint8_t *code;     // UTF-8 string
    uint8_t flags;     // bits: 0=silent, 1=store_history, 2=allow_stdin, 3=stop_on_error
} jmp_execute_request_t;

typedef struct {
    uint8_t status;          // 0=ok, 1=error, 2=abort
    uint16_t execution_count;
} jmp_execute_reply_t;

// according to the the specs, this error message 
// forms the content of _reply messages when the status is error
// also, there is a dedicated message called error (literal) aside from this expected on IOPub 
// when execution fails. error messages are not expected to have exectuion_count and status fields.
typedef struct {
    uint8_t status;          // Must be 1
    uint16_t ename_len;
    uint8_t *ename;
    uint16_t evalue_len;
    uint8_t *evalue;
    uint16_t traceback_len;
    uint8_t *traceback; // string of joined lines, separated by '\n'
    uint16_t execution_count;   //special case, only used for execute_reply failures
} jmp_error_t;

typedef struct {
    uint8_t status;
    uint8_t protocol_version[3];
    uint16_t implementation_len;
    uint8_t *implementation;
    uint8_t implementation_version[3];

    uint16_t lang_name_len;
    uint8_t *lang_name;
    uint8_t lang_version[3];
    uint16_t mimetype_len;
    uint8_t *mimetype;
    uint16_t file_ext_len;
    uint8_t *file_extension;

    uint16_t banner_len;
    uint8_t *banner;
    uint8_t debugger; // boolean
} jmp_kernel_info_reply_t;

// Added missing typedef
typedef struct {
    // Empty - kernel_info_request has no content
} jmp_kernel_info_request_t;


typedef struct {
    // Empty - interrupt_request_t has no content
} jmp_interrupt_request_t;

typedef struct {
    uint8_t status; // ok or error (format as in others)
} jmp_interrupt_reply_t;

typedef struct {
    uint8_t restart; // boolean
} jmp_shutdown_request_t;

typedef struct {
    uint8_t status;  // boolean: 0 = ok
    uint8_t restart; // boolean
} jmp_shutdown_reply_t;

typedef struct {
    uint8_t name; // 0 = "stdout", 1= "stderr"
    uint16_t text_len;
    uint8_t *text;
} jmp_stream_t;

typedef struct {
    uint8_t execution_state; // 0=busy, 1=idle, 2=starting
} jmp_status_t;

typedef struct {
    uint16_t prompt_len;
    uint8_t *prompt;
    uint8_t password; // boolean
} jmp_input_request_t;

typedef struct {
    uint16_t value_len;
    uint8_t *value;
} jmp_input_reply_t;

typedef struct {
    uint16_t header_len;
    uint16_t parent_header_len;
    uint16_t metadata_len;
    uint16_t content_len;
    uint16_t buffers_len;

    uint8_t *header;
    uint8_t *parent_header;
    uint8_t *metadata;
    uint8_t *content;
    uint8_t *buffers;
} jmp_message_t;

typedef struct {
    uint16_t device_id_len;
    uint8_t *device_id;
    uint32_t timestamp;
    uint8_t hmac_sha256 [32];
} jmp_auth_request_t;

typedef struct {
    uint8_t status; //if ok true device is recognized and have been authorized and kernel can now reach it. 

} jmp_auth_reply_t;

typedef struct {
    uint8_t *comm_id;
    uint16_t comm_id_len;

    uint16_t target_id;
} jmp_comm_open_t;

typedef struct {
    uint16_t comm_id_len;
    uint8_t *comm_id;
    uint32_t data;
} jmp_comm_msg_t;

typedef struct {
    uint16_t comm_id_len;
    uint8_t *comm_id;

    uint8_t to_restart;  // boolean
    uint8_t restarted;   // boolean
    uint32_t free_heap;
    uint32_t uptime;
} jmp_comm_notification_msg_t;

typedef struct {
    uint16_t comm_id_len;
    uint8_t *comm_id;
    // data: none
} jmp_comm_close_t;

// inspect
typedef struct {
    uint16_t code_len;
    uint8_t *code;
    uint16_t cursor_pos;
    uint8_t detail_level;
} jmp_inspect_request_t;

typedef struct {
    uint8_t status;        // 0 = ok
    uint8_t found;         // boolean
    uint16_t data_count;
    uint16_t *data_keys_len;
    uint8_t **data_keys;
    uint16_t *data_values_len;
    uint8_t **data_values;
    uint16_t metadata_count; // always 0
} jmp_inspect_reply_t;

// is_complete
typedef struct {
    uint16_t code_len;
    uint8_t *code;
} jmp_is_complete_request_t;

typedef struct {
    uint8_t status;        // 0=complete, 1=incomplete, 2=invalid, 3=unknown
    uint16_t indent_len;
    uint8_t *indent;
} jmp_is_complete_reply_t;

//completions
typedef struct {
    uint16_t code_len;
    uint8_t *code;
    uint16_t cursor_pos;
} jmp_complete_request_t;

typedef struct {
    uint16_t matches_count;
    uint16_t *matches_len;
    uint8_t **matches;

    uint16_t cursor_start;
    uint16_t cursor_end;

    uint16_t metadata_len;  // always 0, but included for completeness
    uint8_t status;        
} jmp_complete_reply_t;

typedef struct {
    uint16_t execution_count;
    uint16_t data_count;
    uint16_t *data_keys_len;
    uint8_t **data_keys;
    uint16_t *data_values_len;
    uint8_t **data_values;

    uint16_t cursor_start;

    uint16_t metadata_count;  // always 0, but included for completeness  
} jmp_execute_result_t;

//parts of the message payload that orignate from the kernel only need a serialize function. 
int jmp_serialize_execute_reply(uint8_t *buf, size_t buf_len, const jmp_execute_reply_t *execute_rep, size_t *out_len);
int jmp_serialize_kernel_info_reply(uint8_t *buf, size_t buf_len, const jmp_kernel_info_reply_t * kernel_info, size_t *out_len);
int jmp_serialize_shutdown_reply(uint8_t *buf, size_t buf_len, const jmp_shutdown_reply_t *shutdown_rep, size_t *out_len);
int jmp_serialize_status(uint8_t *buf, size_t buf_len, const jmp_status_t *status, size_t *out_len);
int jmp_serialize_error(uint8_t *buf, size_t buf_len, const jmp_error_t *error, size_t *out_len);
int jmp_serialize_stream(uint8_t *buf, size_t buf_len, const jmp_stream_t *stream, size_t *out_len);
int jmp_serialize_input_reply(uint8_t *buf, size_t buf_len, const jmp_input_reply_t *input, size_t *out_len);
int jmp_serialize_input_request(uint8_t *buf, size_t buf_len, const jmp_input_request_t *input, size_t *out_len);

//parts of the message payload that orignate from the frontend only need a deserialize function
int jmp_deserialize_execute_request(const uint8_t *buf, size_t buf_len, jmp_execute_request_t *execute_req, size_t *out_len);
int jmp_deserialize_kernel_info_request(const uint8_t *buf, size_t buf_len, jmp_kernel_info_request_t *kernel_info, size_t *out_len);
int jmp_deserialize_shutdown_request(const uint8_t *buf, size_t buf_len, jmp_shutdown_request_t *shutdown_req, size_t *out_len);
int jmp_deserialize_input_request(const uint8_t *buf, size_t buf_len, jmp_input_request_t *input, size_t *out_len);
int jmp_deserialize_input_reply(const uint8_t *buf, size_t buf_len, jmp_input_reply_t *input, size_t *out_len);
int jmp_deserialize_error(const uint8_t *buf, size_t buf_len, jmp_error_t *error, size_t *out_len);
//parts of the message payload that may originate or terminate at the kernel need both
int jmp_serialize_header(uint8_t *buf, size_t buf_len, const jmp_header_t *header, size_t *out_len);
int jmp_deserialize_header(const uint8_t *buf, size_t buf_len, jmp_header_t *header, size_t *out_len);

//this is a convinience special function to dissassemble the payload parts.
//just walk through payload and assign msg memers according, no dynamic allocation happens
int jmp_dissassemble_message(const uint8_t *buf, size_t buf_len, jmp_message_t *msg);

//additional functions not in the jmp spec used intenally for control
int jmp_serialize_auth_request(uint8_t *buf, size_t buf_len, const jmp_auth_request_t *auth_req, size_t *out_len);
int jmp_deserialize_auth_reply(const uint8_t *buf, size_t buf_len, jmp_auth_reply_t *auth_rep, size_t *out_len);

int jmp_add_msg_prefix(uint8_t *buf, size_t buf_len, uint16_t header_len, uint16_t parent_header_len,
    uint16_t metadata_len, uint16_t content_len, uint16_t buffers_len);

// Comm messages originate from both ends
int jmp_serialize_comm_open(uint8_t *buf, size_t buf_len, const jmp_comm_open_t *open_msg, size_t *out_len);
int jmp_deserialize_comm_open(const uint8_t *buf, size_t buf_len, jmp_comm_open_t *open_msg, size_t *out_len);

int jmp_serialize_comm_msg(uint8_t *buf, size_t buf_len, const jmp_comm_msg_t *msg, size_t *out_len);
int jmp_deserialize_comm_msg(const uint8_t *buf, size_t buf_len, jmp_comm_msg_t *msg, size_t *out_len);

int jmp_serialize_comm_notification_msg(uint8_t *buf, size_t buf_len, const jmp_comm_notification_msg_t *msg, size_t *out_len);
int jmp_deserialize_comm_notification_msg(const uint8_t *buf, size_t buf_len, jmp_comm_notification_msg_t *msg, size_t *out_len);

int jmp_serialize_comm_close(uint8_t *buf, size_t buf_len, const jmp_comm_close_t *close_msg, size_t *out_len);
int jmp_deserialize_comm_close(const uint8_t *buf, size_t buf_len, jmp_comm_close_t *close_msg, size_t *out_len);


//completions
// Complete request and reply
int jmp_deserialize_complete_request(const uint8_t *buf, size_t buf_len, jmp_complete_request_t *req, size_t *out_len);
int jmp_serialize_complete_reply(uint8_t *buf, size_t buf_len, const jmp_complete_reply_t *reply, size_t *out_len);

//jupter messaging protocol specifies that the result of constant expressions 
// (at the end of code to execute) should be produced 
// as an execute_result 
int jmp_serialize_execute_result(uint8_t *buf, size_t buf_len, const jmp_execute_result_t *result, size_t *out_len);

// inspect
int jmp_deserialize_inspect_request(const uint8_t *buf, size_t buf_len, jmp_inspect_request_t *req, size_t *out_len);
int jmp_serialize_inspect_reply(uint8_t *buf, size_t buf_len, const jmp_inspect_reply_t *reply, size_t *out_len);

// is_complete
int jmp_deserialize_is_complete_request(const uint8_t *buf, size_t buf_len, jmp_is_complete_request_t *req, size_t *out_len);
int jmp_serialize_is_complete_reply(uint8_t *buf, size_t buf_len, const jmp_is_complete_reply_t *reply, size_t *out_len);

// interrupt
int jmp_serialize_interrupt_reply(uint8_t *buf, size_t buf_len, const jmp_interrupt_reply_t *reply, size_t *out_len);
#endif