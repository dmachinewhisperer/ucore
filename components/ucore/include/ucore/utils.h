#ifndef UCORE_UTILS_H
#define UCORE_UTILS_H

/*binrpc implementation */
#include <stdint.h>
#include <stddef.h>

#define BINRPC_HEADER_SIZE 13
#define BINRPC_MAGIC 0x42525043  // 'BRPC' in ASCII (big-endian)

typedef struct {
    uint32_t magic;      // Always 'BRPC'
    uint8_t  version;    // Protocol version
    uint16_t method_id;  // Application-defined
    uint16_t payload_len;// Length of payload in bytes
    uint32_t request_id; // Request identifier
} binrpc_header_t;



// Error codes for binary payloads
typedef enum {
    BINRPC_OK = 0,                 // No error
    BINRPC_ERR_BUFFER_TOO_SMALL,  // Buffer size insufficient to parse
    BINRPC_ERR_INVALID_MAGIC,      // Magic number mismatch
    BINRPC_ERR_INVALID_DATA,
    BINRPC_ERR_INVALID_VERSION,    // Protocol version unsupported or invalid
    BINRPC_ERR_UNEXPECTED_EOF,     // Unexpected end of buffer while parsing
    BINRPC_ERR_INVALID_PARAM_TYPE, // Unknown or unsupported parameter type tag
    BINRPC_ERR_PAYLOAD_LENGTH_MISMATCH, // Declared payload length doesn't match actual
    BINRPC_ERR_OVERFLOW,           // Integer overflow or size overflow detected
    BINRPC_ERR_ALIGNMENT,          // Misaligned data detected
    BINRPC_ERR_CHECKSUM_MISMATCH,  // Checksum or CRC validation failed
    BINRPC_ERR_INVALID_FORMAT,     // Generic format error or unexpected value
    BINRPC_ERR_UNSUPPORTED_FEATURE // Feature not supported in this implementation
} binrpc_error_t;


// Byte swap utilities for host/network conversions (big-endian network order)
static inline uint16_t ntoh16(uint16_t val) {
    return ((val & 0xFF) << 8) | ((val >> 8) & 0xFF);
}

static inline uint32_t ntoh32(uint32_t val) {
    return ((val & 0xFF) << 24) |
           (((val >> 8) & 0xFF) << 16) |
           (((val >> 16) & 0xFF) << 8) |
           ((val >> 24) & 0xFF);
}

static inline uint16_t hton16(uint16_t val) {
    return ntoh16(val);
}

static inline uint32_t hton32(uint32_t val) {
    return ntoh32(val);
}

// Read a 16-bit big-endian integer from a byte buffer
static inline uint16_t read_uint16_be(const uint8_t *data) {
    return ((uint16_t)data[0] << 8) |
           (uint16_t)data[1];
}

// Write a 16-bit big-endian integer to a byte buffer
static inline void write_uint16_be(uint8_t *data, uint16_t value) {
    data[0] = (value >> 8) & 0xFF;
    data[1] = value & 0xFF;
}

// Read a 32-bit big-endian integer from a byte buffer
static inline uint32_t read_uint32_be(const uint8_t *data) {
    return ((uint32_t)data[0] << 24) |
           ((uint32_t)data[1] << 16) |
           ((uint32_t)data[2] << 8)  |
           (uint32_t)data[3];
}

// Write a 32-bit big-endian integer to a byte buffer
static inline void write_uint32_be(uint8_t *data, uint32_t value) {
    data[0] = (value >> 24) & 0xFF;
    data[1] = (value >> 16) & 0xFF;
    data[2] = (value >> 8)  & 0xFF;
    data[3] = value & 0xFF;
}

//binrpc protocol helpers
int binrpc_serialize_header(
    uint8_t *buf,
    size_t buf_len,
    const binrpc_header_t *header,
    size_t *out_len
);

int binrpc_deserialize_header(
    const uint8_t *buf,
    size_t buf_len,
    binrpc_header_t *header,
    size_t *out_len
);



/* cob encoding implementation */
#define COB_MAX_BLOCK_SIZE 254
typedef enum {
    COB_OK = 0,
    COB_ERROR_NULL_POINTER,
    COB_ERROR_INVALID_SIZE,
    COB_ERROR_BUFFER_TOO_SMALL,
    COB_ERROR_INVALID_DATA,
    COB_ERROR_INCOMPLETE_PACKET
} cob_result_t;

// upper bound = input_size + ceil(input_size/COB_MAX_BLOCK_SIZE)
// ceil(a/b) = (a+b-1)/b
// +1 if the library adds 0x00 delimiter
static inline size_t cob_encoded_max_size(size_t input_size) {
    if (input_size == 0) return 1;
    return input_size + (input_size + COB_MAX_BLOCK_SIZE - 1) / COB_MAX_BLOCK_SIZE + 1;
}

static inline size_t cob_decoded_max_size(size_t encoded_size) {
    return encoded_size - (encoded_size + COB_MAX_BLOCK_SIZE) / (COB_MAX_BLOCK_SIZE + 1);
}
cob_result_t cob_encode(const uint8_t* input, 
                       size_t input_size,
                       uint8_t* output, 
                       size_t output_size,
                       size_t* encoded_size);
cob_result_t cob_decode(const uint8_t* input,
                       size_t input_size, 
                       uint8_t* output,
                       size_t output_size,
                       size_t* decoded_size); 
#endif