#include <string.h>
#include <stdlib.h>
#include <stdint.h>

#include "ucore/utils.h"
#include "ucore/jmp_bin.h"

// Helper function to check buffer space
static inline int check_buffer_space(size_t current_pos, size_t needed, size_t buf_len) {
    return (current_pos + needed <= buf_len) ? 0 : BINRPC_ERR_BUFFER_TOO_SMALL;
}

// Helper function to write string with length prefix
static int write_string_field(uint8_t *buf, size_t buf_len, size_t *pos, 
                             uint16_t str_len, const uint8_t *str) {
    if (check_buffer_space(*pos, 2 + str_len, buf_len) != 0) {
        return BINRPC_ERR_BUFFER_TOO_SMALL;
    }
    
    write_uint16_be(buf + *pos, str_len);
    *pos += 2;
    
    if (str_len > 0 && str != NULL) {
        memcpy(buf + *pos, str, str_len);
        *pos += str_len;
    }
    
    return BINRPC_OK;
}

// Helper function to read string field
static int read_string_field(const uint8_t *buf, size_t buf_len, size_t *pos,
                            uint16_t *str_len, uint8_t **str) {
    if (check_buffer_space(*pos, 2, buf_len) != 0) {
        return BINRPC_ERR_BUFFER_TOO_SMALL;
    }
    
    *str_len = read_uint16_be(buf + *pos);
    *pos += 2;
    
    if (*str_len > 0) {
        if (check_buffer_space(*pos, *str_len, buf_len) != 0) {
            return BINRPC_ERR_BUFFER_TOO_SMALL;
        }
        *str = (uint8_t *)(buf + *pos);
        *pos += *str_len;
    } else {
        *str = NULL;
    }
    
    return BINRPC_OK;
}

// Serialize functions (kernel -> frontend)

int jmp_serialize_execute_reply(uint8_t *buf, size_t buf_len, const jmp_execute_reply_t *execute_rep, size_t *out_len) {
    if (!buf || !execute_rep || !out_len) {
        return BINRPC_ERR_INVALID_DATA;
    }
    
    size_t pos = 0;
    
    // Write status
    if (check_buffer_space(pos, 1, buf_len) != 0) {
        return BINRPC_ERR_BUFFER_TOO_SMALL;
    }
    buf[pos++] = execute_rep->status;
    
    // If status is OK (0), write execution count
    if (execute_rep->status == 0) {
        if (check_buffer_space(pos, 2, buf_len) != 0) {
            return BINRPC_ERR_BUFFER_TOO_SMALL;
        }
        write_uint16_be(buf + pos, execute_rep->execution_count);
        pos += 2;
    }
    
    *out_len = pos;
    return BINRPC_OK;
}

int jmp_serialize_kernel_info_reply(uint8_t *buf, size_t buf_len, const jmp_kernel_info_reply_t *kernel_info, size_t *out_len) {
    if (!buf || !kernel_info || !out_len) {
        return BINRPC_ERR_INVALID_DATA;
    }
    
    size_t pos = 0;
    int ret;
    
    // Write status
    if (check_buffer_space(pos, 1, buf_len) != 0) {
        return BINRPC_ERR_BUFFER_TOO_SMALL;
    }
    buf[pos++] = kernel_info->status;
    
    // Write protocol version
    if (check_buffer_space(pos, 3, buf_len) != 0) {
        return BINRPC_ERR_BUFFER_TOO_SMALL;
    }
    memcpy(buf + pos, kernel_info->protocol_version, 3);
    pos += 3;
    
    // Write implementation
    ret = write_string_field(buf, buf_len, &pos, kernel_info->implementation_len, kernel_info->implementation);
    if (ret != BINRPC_OK) return ret;
    
    // Write implementation version
    if (check_buffer_space(pos, 3, buf_len) != 0) {
        return BINRPC_ERR_BUFFER_TOO_SMALL;
    }
    memcpy(buf + pos, kernel_info->implementation_version, 3);
    pos += 3;
    
    // Write language info
    ret = write_string_field(buf, buf_len, &pos, kernel_info->lang_name_len, kernel_info->lang_name);
    if (ret != BINRPC_OK) return ret;
    
    if (check_buffer_space(pos, 3, buf_len) != 0) {
        return BINRPC_ERR_BUFFER_TOO_SMALL;
    }
    memcpy(buf + pos, kernel_info->lang_version, 3);
    pos += 3;
    
    ret = write_string_field(buf, buf_len, &pos, kernel_info->mimetype_len, kernel_info->mimetype);
    if (ret != BINRPC_OK) return ret;
    
    ret = write_string_field(buf, buf_len, &pos, kernel_info->file_ext_len, kernel_info->file_extension);
    if (ret != BINRPC_OK) return ret;
    
    // Write banner
    ret = write_string_field(buf, buf_len, &pos, kernel_info->banner_len, kernel_info->banner);
    if (ret != BINRPC_OK) return ret;
    
    // Write debugger flag
    if (check_buffer_space(pos, 1, buf_len) != 0) {
        return BINRPC_ERR_BUFFER_TOO_SMALL;
    }
    buf[pos++] = kernel_info->debugger;
    
    *out_len = pos;
    return BINRPC_OK;
}

int jmp_serialize_shutdown_reply(uint8_t *buf, size_t buf_len, const jmp_shutdown_reply_t *shutdown_rep, size_t *out_len) {
    if (!buf || !shutdown_rep || !out_len) {
        return BINRPC_ERR_INVALID_DATA;
    }
    
    if (check_buffer_space(0, 2, buf_len) != 0) {
        return BINRPC_ERR_BUFFER_TOO_SMALL;
    }
    
    buf[0] = shutdown_rep->status;
    buf[1] = shutdown_rep->restart;
    
    *out_len = 2;
    return BINRPC_OK;
}

int jmp_serialize_status(uint8_t *buf, size_t buf_len, const jmp_status_t *status, size_t *out_len) {
    if (!buf || !status || !out_len) {
        return BINRPC_ERR_INVALID_DATA;
    }
    
    if (check_buffer_space(0, 1, buf_len) != 0) {
        return BINRPC_ERR_BUFFER_TOO_SMALL;
    }
    
    buf[0] = status->execution_state;
    
    *out_len = 1;
    return BINRPC_OK;
}
int jmp_serialize_error(uint8_t *buf, size_t buf_len, const jmp_error_t *error, size_t *out_len) {
    if (!buf || !error || !out_len) {
        return BINRPC_ERR_INVALID_DATA;
    }
    
    size_t pos = 0;
    int ret;
    
    // Write status (must be 1 for error)
    if (check_buffer_space(pos, 1, buf_len) != 0) {
        return BINRPC_ERR_BUFFER_TOO_SMALL;
    }
    buf[pos++] = error->status;
    
    // Write error name
    ret = write_string_field(buf, buf_len, &pos, error->ename_len, error->ename);
    if (ret != BINRPC_OK) return ret;
    
    // Write error value
    ret = write_string_field(buf, buf_len, &pos, error->evalue_len, error->evalue);
    if (ret != BINRPC_OK) return ret;
    
    // Write traceback
    ret = write_string_field(buf, buf_len, &pos, error->traceback_len, error->traceback);
    if (ret != BINRPC_OK) return ret;

    //write the optional field
    if (check_buffer_space(pos, 2, buf_len) != 0) {
        return BINRPC_ERR_BUFFER_TOO_SMALL;
    }
    write_uint16_be(buf + pos, error->execution_count);
    pos += 2;
    
    *out_len = pos;
    return BINRPC_OK;
}

int jmp_deserialize_error(const uint8_t *buf, size_t buf_len, jmp_error_t *error, size_t *out_len) {
    if (!buf || !error || !out_len) {
        return BINRPC_ERR_INVALID_DATA;
    }

    size_t pos = 0;
    int ret;

    // Read status (must be 1 for error)
    if (check_buffer_space(pos, 1, buf_len) != 0) {
        return BINRPC_ERR_BUFFER_TOO_SMALL;
    }
    error->status = buf[pos++];

    // Read error name
    ret = read_string_field(buf, buf_len, &pos, &error->ename_len, &error->ename);
    if (ret != BINRPC_OK) return ret;

    // Read error value
    ret = read_string_field(buf, buf_len, &pos, &error->evalue_len, &error->evalue);
    if (ret != BINRPC_OK) return ret;

    // Read traceback
    ret = read_string_field(buf, buf_len, &pos, &error->traceback_len, &error->traceback);
    if (ret != BINRPC_OK) return ret;

    // Read execution count (optional field)
    if (check_buffer_space(pos, 2, buf_len) != 0) {
        return BINRPC_ERR_BUFFER_TOO_SMALL;
    }
    error->execution_count = read_uint16_be(buf + pos);
    pos += 2;

    *out_len = pos;
    return BINRPC_OK;
}

int jmp_serialize_stream(uint8_t *buf, size_t buf_len, const jmp_stream_t *stream, size_t *out_len) {
    if (!buf || !stream || !out_len) {
        return BINRPC_ERR_INVALID_DATA;
    }
    
    size_t pos = 0;
    int ret;
    
    // Write stream name
    //ret = write_string_field(buf, buf_len, &pos, stream->name_len, stream->name);
    //if (ret != BINRPC_OK) return ret;

    buf[pos++] = stream->name;
    
    // Write stream text
    ret = write_string_field(buf, buf_len, &pos, stream->text_len, stream->text);
    if (ret != BINRPC_OK) return ret;
    
    *out_len = pos;
    return BINRPC_OK;
}

int jmp_serialize_input_reply(uint8_t *buf, size_t buf_len, const jmp_input_reply_t *input, size_t *out_len) {
    if (!buf || !input || !out_len) {
        return BINRPC_ERR_INVALID_DATA;
    }
    
    size_t pos = 0;
    int ret;
    
    ret = write_string_field(buf, buf_len, &pos, input->value_len, input->value);
    if (ret != BINRPC_OK) return ret;
    
    *out_len = pos;
    return BINRPC_OK;
}

int jmp_serialize_input_request(uint8_t *buf, size_t buf_len, const jmp_input_request_t *input, size_t *out_len) {
    if (!buf || !input || !out_len) {
        return BINRPC_ERR_INVALID_DATA;
    }
    
    size_t pos = 0;
    int ret;
    
    // Write prompt
    ret = write_string_field(buf, buf_len, &pos, input->prompt_len, input->prompt);
    if (ret != BINRPC_OK) return ret;
    
    // Write password flag
    if (check_buffer_space(pos, 1, buf_len) != 0) {
        return BINRPC_ERR_BUFFER_TOO_SMALL;
    }
    buf[pos++] = input->password;
    
    *out_len = pos;
    return BINRPC_OK;
}

int jmp_deserialize_input_reply(const uint8_t *buf, size_t buf_len, jmp_input_reply_t *input, size_t *out_len) {
    if (!buf || !input || !out_len) {
        return BINRPC_ERR_INVALID_DATA;
    }

    size_t pos = 0;
    int ret;

    // Read value field (length-prefixed UTF-8 string)
    ret = read_string_field(buf, buf_len, &pos, &input->value_len, &input->value);
    if (ret != BINRPC_OK) return ret;

    *out_len = pos;
    return BINRPC_OK;
}


// Deserialize functions (frontend -> kernel)

int jmp_deserialize_execute_request(const uint8_t *buf, size_t buf_len, jmp_execute_request_t *execute_req, size_t *out_len) {
    if (!buf || !execute_req || !out_len) {
        return BINRPC_ERR_INVALID_DATA;
    }
    
    size_t pos = 0;
    int ret;
    
    // Read code
    ret = read_string_field(buf, buf_len, &pos, &execute_req->code_len, &execute_req->code);
    if (ret != BINRPC_OK) return ret;
    
    // Read flags
    if (check_buffer_space(pos, 1, buf_len) != 0) {
        return BINRPC_ERR_BUFFER_TOO_SMALL;
    }
    execute_req->flags = buf[pos++];
    
    *out_len = pos;
    return BINRPC_OK;
}

int jmp_deserialize_kernel_info_request(const uint8_t *buf, size_t buf_len, jmp_kernel_info_request_t *kernel_info, size_t *out_len) {
    // kernel_info_request has no content, so this is essentially a no-op
    if (!kernel_info || !out_len) {
        return BINRPC_ERR_INVALID_DATA;
    }
    
    *out_len = 0;
    return BINRPC_OK;
}

int jmp_deserialize_shutdown_request(const uint8_t *buf, size_t buf_len, jmp_shutdown_request_t *shutdown_req, size_t *out_len) {
    if (!buf || !shutdown_req || !out_len) {
        return BINRPC_ERR_INVALID_DATA;
    }
    
    if (check_buffer_space(0, 1, buf_len) != 0) {
        return BINRPC_ERR_BUFFER_TOO_SMALL;
    }
    
    shutdown_req->restart = buf[0];
    
    *out_len = 1;
    return BINRPC_OK;
}

// Header serialization/deserialization

int jmp_serialize_header(uint8_t *buf, size_t buf_len, const jmp_header_t *header, size_t *out_len) {
    if (!buf || !header || !out_len) {
        return BINRPC_ERR_INVALID_DATA;
    }

    size_t pos = 0;
    int ret;

    // Write msg_id
    ret = write_string_field(buf, buf_len, &pos, header->msg_id_len, header->msg_id);
    if (ret != BINRPC_OK) return ret;

    // Write session_id
    ret = write_string_field(buf, buf_len, &pos, header->session_id_len, header->session_id);
    if (ret != BINRPC_OK) return ret;

    // Write username
    ret = write_string_field(buf, buf_len, &pos, header->username_len, header->username);
    if (ret != BINRPC_OK) return ret;

    // Write msg_type
    if (check_buffer_space(pos, 1, buf_len) != 0) {
        return BINRPC_ERR_BUFFER_TOO_SMALL;
    }
    buf[pos++] = header->msg_type;

    // Write version (3 bytes)
    if (check_buffer_space(pos, 3, buf_len) != 0) {
        return BINRPC_ERR_BUFFER_TOO_SMALL;
    }
    memcpy(buf + pos, header->version, 3);
    pos += 3;

    *out_len = pos;
    return BINRPC_OK;
}

int jmp_deserialize_header(const uint8_t *buf, size_t buf_len, jmp_header_t *header, size_t *out_len) {
    if (!buf || !header || !out_len) {
        return BINRPC_ERR_INVALID_DATA;
    }

    size_t pos = 0;
    int ret;

    // Read msg_id
    ret = read_string_field(buf, buf_len, &pos, &header->msg_id_len, &header->msg_id);
    if (ret != BINRPC_OK) return ret;

    // Read session_id
    ret = read_string_field(buf, buf_len, &pos, &header->session_id_len, &header->session_id);
    if (ret != BINRPC_OK) return ret;

    // Read username
    ret = read_string_field(buf, buf_len, &pos, &header->username_len, &header->username);
    if (ret != BINRPC_OK) return ret;

    // Read msg_type
    if (check_buffer_space(pos, 1, buf_len) != 0) {
        return BINRPC_ERR_BUFFER_TOO_SMALL;
    }
    header->msg_type = buf[pos++];

    // Read version (3 bytes)
    if (check_buffer_space(pos, 3, buf_len) != 0) {
        return BINRPC_ERR_BUFFER_TOO_SMALL;
    }
    memcpy(header->version, buf + pos, 3);
    pos += 3;

    *out_len = pos;
    return BINRPC_OK;
}

int jmp_serialize_auth_request(uint8_t *buf, size_t buf_len, const jmp_auth_request_t *auth_req, size_t *out_len) {
    if (!buf || !auth_req || !out_len) {
        return BINRPC_ERR_INVALID_DATA;
    }

    size_t pos = 0;
    int ret;

    // Write device_id
    ret = write_string_field(buf, buf_len, &pos, auth_req->device_id_len, auth_req->device_id);
    if (ret != BINRPC_OK) return ret;

    // Write timestamp
    if (check_buffer_space(pos, 4, buf_len) != 0) {
        return BINRPC_ERR_BUFFER_TOO_SMALL;
    }
    write_uint32_be(buf + pos, auth_req->timestamp);
    pos += 4;

    // Write hmac_sha256 (32 bytes)
    if (check_buffer_space(pos, 32, buf_len) != 0) {
        return BINRPC_ERR_BUFFER_TOO_SMALL;
    }
    memcpy(buf + pos, auth_req->hmac_sha256, 32);
    pos += 32;

    *out_len = pos;
    return BINRPC_OK;
}

int jmp_deserialize_auth_reply(const uint8_t *buf, size_t buf_len, jmp_auth_reply_t *auth_rep, size_t *out_len) {
    if (!buf || !auth_rep || !out_len) {
        return BINRPC_ERR_INVALID_DATA;
    }

    if (check_buffer_space(0, 1, buf_len) != 0) {
        return BINRPC_ERR_BUFFER_TOO_SMALL;
    }

    auth_rep->status = buf[0];
    *out_len = 1;
    return BINRPC_OK;
}

// Message disassembly function
int jmp_dissassemble_message(const uint8_t *buf, size_t buf_len, jmp_message_t *msg) {
    if (!buf || !msg) {
        return BINRPC_ERR_INVALID_DATA;
    }

    size_t pos = 0;

    if (check_buffer_space(pos, 10, buf_len) != 0) {
        return BINRPC_ERR_BUFFER_TOO_SMALL;
    }

    uint16_t *length_fields[] = {
        &msg->header_len,
        &msg->parent_header_len,
        &msg->metadata_len,
        &msg->content_len,
        &msg->buffers_len
    };

    for (int i = 0; i < 5; i++) {
        *length_fields[i] = read_uint16_be(buf + pos);
        pos += 2;
    }

    uint8_t **data_ptrs[] = {
        &msg->header,
        &msg->parent_header,
        &msg->metadata,
        &msg->content,
        &msg->buffers
    };

    // Assign pointers using a loop with safety checks
    for (int i = 0; i < 5; i++) {
        uint16_t len = *length_fields[i];
        if (check_buffer_space(pos, len, buf_len) != 0) {
            return BINRPC_ERR_BUFFER_TOO_SMALL;
        }
        *data_ptrs[i] = (len > 0) ? buf + pos : NULL;
        pos += len;
    }

    return BINRPC_OK;
}

int jmp_add_msg_prefix(uint8_t *buf, size_t buf_len, uint16_t header_len,
    uint16_t parent_header_len, uint16_t metadata_len, uint16_t content_len, uint16_t buffers_len) {
    if (!buf) {
    return BINRPC_ERR_INVALID_DATA;
    }

    // Need space for 5 uint16_t values
    if (buf_len < 10) {
    return BINRPC_ERR_BUFFER_TOO_SMALL;
    }

    write_uint16_be(buf, header_len);
    write_uint16_be(buf + 2, parent_header_len);
    write_uint16_be(buf + 4, metadata_len);
    write_uint16_be(buf + 6, content_len);
    write_uint16_be(buf + 8, buffers_len);

    return BINRPC_OK;
}

// Comm message serialization/deserialization functions

// Serialize comm_open (both directions)
int jmp_serialize_comm_open(uint8_t *buf, size_t buf_len, const jmp_comm_open_t *open_msg, size_t *out_len) {
    if (!buf || !open_msg || !out_len) {
        return BINRPC_ERR_INVALID_DATA;
    }
    
    size_t pos = 0;
    int ret;
    
    // Write comm_id with length prefix
    ret = write_string_field(buf, buf_len, &pos, open_msg->comm_id_len, open_msg->comm_id);
    if (ret != BINRPC_OK) {
        return ret;
    }
    
    // Write target_id
    if (check_buffer_space(pos, 2, buf_len) != 0) {
        return BINRPC_ERR_BUFFER_TOO_SMALL;
    }
    write_uint16_be(buf + pos, open_msg->target_id);
    pos += 2;
    
    *out_len = pos;
    return BINRPC_OK;
}

// Deserialize comm_open (both directions)
int jmp_deserialize_comm_open(const uint8_t *buf, size_t buf_len, jmp_comm_open_t *open_msg, size_t *out_len) {
    if (!buf || !open_msg || !out_len) {
        return BINRPC_ERR_INVALID_DATA;
    }
    
    size_t pos = 0;
    int ret;
    
    // Read comm_id with length prefix
    ret = read_string_field(buf, buf_len, &pos, &open_msg->comm_id_len, &open_msg->comm_id);
    if (ret != BINRPC_OK) {
        return ret;
    }
    
    // Read target_id
    if (check_buffer_space(pos, 2, buf_len) != 0) {
        return BINRPC_ERR_BUFFER_TOO_SMALL;
    }
    open_msg->target_id = read_uint16_be(buf + pos);
    pos += 2;
    
    *out_len = pos;
    return BINRPC_OK;
}
int jmp_serialize_comm_msg(uint8_t *buf, size_t buf_len, const jmp_comm_msg_t *msg, size_t *out_len) {
    if (!buf || !msg || !out_len) {
        return BINRPC_ERR_INVALID_DATA;
    }

    size_t pos = 0;
    int ret;

    ret = write_string_field(buf, buf_len, &pos, msg->comm_id_len, msg->comm_id);
    if (ret != BINRPC_OK) {
        return ret;
    }

    if (check_buffer_space(pos, 4, buf_len) != 0) {
        return BINRPC_ERR_BUFFER_TOO_SMALL;
    }

    write_uint32_be(buf + pos, msg->data);
    pos += 4;

    *out_len = pos;
    return BINRPC_OK;
}

int jmp_deserialize_comm_msg(const uint8_t *buf, size_t buf_len, jmp_comm_msg_t *msg, size_t *out_len) {
    if (!buf || !msg || !out_len) {
        return BINRPC_ERR_INVALID_DATA;
    }

    size_t pos = 0;
    int ret;

    ret = read_string_field(buf, buf_len, &pos, &msg->comm_id_len, &msg->comm_id);
    if (ret != BINRPC_OK) {
        return ret;
    }

    if (check_buffer_space(pos, 4, buf_len) != 0) {
        return BINRPC_ERR_BUFFER_TOO_SMALL;
    }

    msg->data = read_uint32_be(buf + pos);
    pos += 4;

    *out_len = pos;
    return BINRPC_OK;
}

int jmp_serialize_comm_notification_msg(uint8_t *buf, size_t buf_len, const jmp_comm_notification_msg_t *msg, size_t *out_len) {
    if (!buf || !msg || !out_len) {
        return BINRPC_ERR_INVALID_DATA;
    }
    
    size_t pos = 0;
    int ret;
    
    ret = write_string_field(buf, buf_len, &pos, msg->comm_id_len, msg->comm_id);
    if (ret != BINRPC_OK) {
        return ret;
    }
    
    if (check_buffer_space(pos, 10, buf_len) != 0) { // 1 + 1 + 4 + 4 = 10 bytes
        return BINRPC_ERR_BUFFER_TOO_SMALL;
    }
    
    buf[pos++] = msg->to_restart;
    buf[pos++] = msg->restarted;
    
    write_uint32_be(buf + pos, msg->free_heap);
    pos += 4;
    
    write_uint32_be(buf + pos, msg->uptime);
    pos += 4;
    
    *out_len = pos;
    return BINRPC_OK;
}

int jmp_deserialize_comm_notification_msg(const uint8_t *buf, size_t buf_len, jmp_comm_notification_msg_t *msg, size_t *out_len) {
    if (!buf || !msg || !out_len) {
        return BINRPC_ERR_INVALID_DATA;
    }
    
    size_t pos = 0;
    int ret;
    
    ret = read_string_field(buf, buf_len, &pos, &msg->comm_id_len, &msg->comm_id);
    if (ret != BINRPC_OK) {
        return ret;
    }
    
    if (check_buffer_space(pos, 10, buf_len) != 0) { // 1 + 1 + 4 + 4 = 10 bytes
        return BINRPC_ERR_BUFFER_TOO_SMALL;
    }
    
    msg->to_restart = buf[pos++];
    msg->restarted = buf[pos++];
    
    msg->free_heap = read_uint32_be(buf + pos);
    pos += 4;
    
    msg->uptime = read_uint32_be(buf + pos);
    pos += 4;
    
    *out_len = pos;
    return BINRPC_OK;
}

int jmp_serialize_comm_close(uint8_t *buf, size_t buf_len, const jmp_comm_close_t *close_msg, size_t *out_len) {
    if (!buf || !close_msg || !out_len) {
        return BINRPC_ERR_INVALID_DATA;
    }
    
    size_t pos = 0;
    int ret;
    
    ret = write_string_field(buf, buf_len, &pos, close_msg->comm_id_len, close_msg->comm_id);
    if (ret != BINRPC_OK) {
        return ret;
    }
    
    *out_len = pos;
    return BINRPC_OK;
}

int jmp_deserialize_comm_close(const uint8_t *buf, size_t buf_len, jmp_comm_close_t *close_msg, size_t *out_len) {
    if (!buf || !close_msg || !out_len) {
        return BINRPC_ERR_INVALID_DATA;
    }
    
    size_t pos = 0;
    int ret;
    
    ret = read_string_field(buf, buf_len, &pos, &close_msg->comm_id_len, &close_msg->comm_id);
    if (ret != BINRPC_OK) {
        return ret;
    }
    
    *out_len = pos;
    return BINRPC_OK;
}

int jmp_deserialize_complete_request(const uint8_t *buf, size_t buf_len, jmp_complete_request_t *req, size_t *out_len) {
    if (!buf || !req || !out_len) {
        return BINRPC_ERR_INVALID_DATA;
    }

    size_t pos = 0;
    int ret;

    // Read code (UTF-8 string with uint16 length prefix)
    ret = read_string_field(buf, buf_len, &pos, &req->code_len, &req->code);
    if (ret != BINRPC_OK) return ret;

    // Read cursor_pos (uint16)
    if (check_buffer_space(pos, 2, buf_len) != 0) {
        return BINRPC_ERR_BUFFER_TOO_SMALL;
    }
    req->cursor_pos = read_uint16_be(buf + pos);
    pos += 2;

    *out_len = pos;
    return BINRPC_OK;
}

int jmp_serialize_complete_reply(uint8_t *buf, size_t buf_len, const jmp_complete_reply_t *rep, size_t *out_len) {
    if (!buf || !rep || !out_len) {
        return BINRPC_ERR_INVALID_DATA;
    }

    size_t pos = 0;
    int ret;

    // Write number of matches
    if (check_buffer_space(pos, 2, buf_len) != 0) {
        return BINRPC_ERR_BUFFER_TOO_SMALL;
    }
    write_uint16_be(buf + pos, rep->matches_count);
    pos += 2;

    // Write each match
    for (uint16_t i = 0; i < rep->matches_count; ++i) {
        ret = write_string_field(buf, buf_len, &pos, rep->matches_len[i], rep->matches[i]);
        if (ret != BINRPC_OK) return ret;
    }

    // Write cursor_start
    if (check_buffer_space(pos, 2, buf_len) != 0) {
        return BINRPC_ERR_BUFFER_TOO_SMALL;
    }
    write_uint16_be(buf + pos, rep->cursor_start);
    pos += 2;

    // Write cursor_end
    if (check_buffer_space(pos, 2, buf_len) != 0) {
        return BINRPC_ERR_BUFFER_TOO_SMALL;
    }
    write_uint16_be(buf + pos, rep->cursor_end);
    pos += 2;

    // Write metadata_len (always 0 for now)
    if (check_buffer_space(pos, 2, buf_len) != 0) {
        return BINRPC_ERR_BUFFER_TOO_SMALL;
    }
    write_uint16_be(buf + pos, 0);
    pos += 2;

    // Write status (uint8)
    if (check_buffer_space(pos, 1, buf_len) != 0) {
        return BINRPC_ERR_BUFFER_TOO_SMALL;
    }
    buf[pos++] = rep->status;

    *out_len = pos;
    return BINRPC_OK;
}

int jmp_serialize_execute_result(uint8_t *buf, size_t buf_len, const jmp_execute_result_t *result, size_t *out_len) {
    if (!buf || !result || !out_len) {
        return BINRPC_ERR_INVALID_DATA;
    }

    size_t pos = 0;
    int ret;

    if (check_buffer_space(pos, 2, buf_len) != 0) return BINRPC_ERR_BUFFER_TOO_SMALL;
    write_uint16_be(buf + pos, result->execution_count);
    pos += 2;

    if (check_buffer_space(pos, 2, buf_len) != 0) return BINRPC_ERR_BUFFER_TOO_SMALL;
    write_uint16_be(buf + pos, result->data_count);
    pos += 2;

    for (uint16_t i = 0; i < result->data_count; i++) {
        ret = write_string_field(buf, buf_len, &pos, result->data_keys_len[i], result->data_keys[i]);
        if (ret != BINRPC_OK) return ret;

        ret = write_string_field(buf, buf_len, &pos, result->data_values_len[i], result->data_values[i]);
        if (ret != BINRPC_OK) return ret;
    }

    if (check_buffer_space(pos, 2, buf_len) != 0) return BINRPC_ERR_BUFFER_TOO_SMALL;
    write_uint16_be(buf + pos, result->metadata_count);
    pos += 2;

    *out_len = pos;
    return BINRPC_OK;
}

