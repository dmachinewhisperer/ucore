#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "ucore/utils.h"

/* binrpc implementation */

// binrpc defines a simple JSON-RPC-like binary payload format for
// moving data over a wire. The application defines what each `method_id` corresponds to
// and how the payload is interpreted on both ends.

// Header Format (big-endian):
// ┌────────┬────────┬────────────┬─────────────┬───────────────┐
// │ Magic  │ Version│ Method ID  │ Payload Len │ Request ID    │
// │ 4 bytes│ 1 byte │ 2 bytes    │ 2 bytes     │ 4 bytes       │
// └────────┴────────┴────────────┴─────────────┴───────────────┘
// Total size: 13 bytes

int binrpc_serialize_header(
    uint8_t *buf,
    size_t buf_len,
    const binrpc_header_t *header,
    size_t *out_len
) {
    if (!buf || !header || !out_len)
        return BINRPC_ERR_BUFFER_TOO_SMALL;

    if (buf_len < BINRPC_HEADER_SIZE)
        return BINRPC_ERR_BUFFER_TOO_SMALL;

    write_uint32_be(buf,     header->magic);        // Magic (e.g. 'BINRPC')
    buf[4] = header->version;                       // Protocol version
    write_uint16_be(buf + 5, header->method_id);    // Method ID
    write_uint16_be(buf + 7, header->payload_len);  // Payload length in bytes
    write_uint32_be(buf + 9, header->request_id);   // Request ID

    *out_len = BINRPC_HEADER_SIZE;
    return BINRPC_OK;
}

int binrpc_deserialize_header(
    const uint8_t *buf,
    size_t buf_len,
    binrpc_header_t *header,
    size_t *out_len
) {
    if (!buf || !header || !out_len)
        return BINRPC_ERR_BUFFER_TOO_SMALL;

    if (buf_len < BINRPC_HEADER_SIZE)
        return BINRPC_ERR_BUFFER_TOO_SMALL;

    uint32_t magic = read_uint32_be(buf);
    if (magic != BINRPC_MAGIC)
        return BINRPC_ERR_INVALID_MAGIC;

    header->magic       = magic;
    header->version     = buf[4];
    header->method_id   = read_uint16_be(buf + 5);
    header->payload_len = read_uint16_be(buf + 7);
    header->request_id  = read_uint32_be(buf + 9);

    *out_len = BINRPC_HEADER_SIZE;
    return BINRPC_OK;
}


/* cob implementation */


cob_result_t cob_encode(const uint8_t *input,
                        size_t input_size,
                        uint8_t *output,
                        size_t output_size,
                        size_t *encoded_size)
{
    if (!output || !encoded_size) {
        return COB_ERROR_NULL_POINTER;
    }

    if (!input && input_size > 0) {
        return COB_ERROR_NULL_POINTER;
    }

    /* Worst case: every 254 bytes adds a code byte + final zero */
    size_t max_needed = input_size + (input_size + COB_MAX_BLOCK_SIZE - 1) / COB_MAX_BLOCK_SIZE + 1;
    if (output_size < max_needed) {
        return COB_ERROR_BUFFER_TOO_SMALL;
    }

    size_t in = 0;
    size_t out = 1;      /* reserve space for first code */
    size_t code_index = 0;
    uint8_t code = 1;

    while (in < input_size) {
        if (input[in] == 0) {
            output[code_index] = code;
            code_index = out++;
            code = 1;
            in++;
        } else {
            output[out++] = input[in++];
            code++;
            if (code == 0xFF) {
                output[code_index] = code;
                code_index = out++;
                code = 1;
            }
        }
    }

    output[code_index] = code;
    output[out++] = 0x00;   /* packet delimiter */

    *encoded_size = out;
    return COB_OK;
}


cob_result_t cob_decode(const uint8_t *input,
                        size_t input_size,
                        uint8_t *output,
                        size_t output_size,
                        size_t *decoded_size)
{
    if (!input || !output || !decoded_size) {
        return COB_ERROR_NULL_POINTER;
    }

    if (input_size < 2 || input[input_size - 1] != 0x00) {
        return COB_ERROR_INCOMPLETE_PACKET;
    }

    size_t in = 0;
    size_t out = 0;

    while (in < input_size - 1) {
        uint8_t code = input[in++];
        if (code == 0) {
            return COB_ERROR_INVALID_DATA;
        }

        for (uint8_t i = 1; i < code; i++) {
            if (in >= input_size - 1) {
                return COB_ERROR_INVALID_DATA;
            }
            if (out >= output_size) {
                return COB_ERROR_BUFFER_TOO_SMALL;
            }
            output[out++] = input[in++];
        }

        if (code < 0xFF && in < input_size - 1) {
            if (out >= output_size) {
                return COB_ERROR_BUFFER_TOO_SMALL;
            }
            output[out++] = 0x00;
        }
    }

    *decoded_size = out;
    return COB_OK;
}