/*
 * Host-compiled test harness for jmp_bin serialize/deserialize functions.
 * Loads golden vectors from jmp_bin.vectors.json and verifies the C
 * implementation produces identical bytes.
 *
 * Build: make -C kernel/components/ucore/test
 * Run:   kernel/components/ucore/test/test_jmp_bin
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdint.h>

#include "ucore/utils.h"
#include "ucore/jmp_bin.h"

static void hex_encode(const uint8_t *data, size_t len, char *out) {
    for (size_t i = 0; i < len; i++) {
        sprintf(out + i * 2, "%02x", data[i]);
    }
    out[len * 2] = '\0';
}

/* ---- Test functions ---- */

static int tests_run = 0;
static int tests_passed = 0;

#define TEST_START(name) do { \
    tests_run++; \
    printf("  %s ... ", name); \
} while(0)

#define TEST_PASS() do { \
    tests_passed++; \
    printf("ok\n"); \
} while(0)

#define TEST_FAIL(msg, ...) do { \
    printf("FAIL\n    "); \
    printf(msg, ##__VA_ARGS__); \
    printf("\n"); \
} while(0)

static void test_stream(const char *vector_hex) {
    TEST_START("stream (serialize)");

    jmp_stream_t stream = {
        .name = STDOUT,
        .text = (uint8_t *)"hello",
        .text_len = 5,
    };

    uint8_t buf[64];
    size_t out_len;
    int rc = jmp_serialize_stream(buf, sizeof(buf), &stream, &out_len);

    if (rc != BINRPC_OK) {
        TEST_FAIL("serialize returned %d", rc);
        return;
    }

    char hex[128];
    hex_encode(buf, out_len, hex);

    if (strcmp(hex, vector_hex) == 0) {
        TEST_PASS();
    } else {
        TEST_FAIL("expected %s, got %s", vector_hex, hex);
    }
}

static void test_stream_roundtrip(void) {
    TEST_START("stream (roundtrip)");

    jmp_stream_t original = {
        .name = STDERR,
        .text = (uint8_t *)"error message",
        .text_len = 13,
    };

    uint8_t buf[128];
    size_t out_len;

    int rc = jmp_serialize_stream(buf, sizeof(buf), &original, &out_len);
    assert(rc == BINRPC_OK);

    /* We don't have a deserialize_stream, so verify manually */
    if (buf[0] == STDERR && read_uint16_be(buf + 1) == 13 &&
        memcmp(buf + 3, "error message", 13) == 0) {
        TEST_PASS();
    } else {
        TEST_FAIL("roundtrip verification failed");
    }
}

static void test_status(void) {
    TEST_START("status (busy/idle/starting)");

    uint8_t buf[4];
    size_t out_len;

    jmp_status_t status = { .execution_state = KERNEL_BUSY };
    jmp_serialize_status(buf, sizeof(buf), &status, &out_len);
    if (out_len != 1 || buf[0] != 0x00) {
        TEST_FAIL("busy: expected 00, got %02x", buf[0]);
        return;
    }

    status.execution_state = KERNEL_IDLE;
    jmp_serialize_status(buf, sizeof(buf), &status, &out_len);
    if (buf[0] != 0x01) {
        TEST_FAIL("idle: expected 01, got %02x", buf[0]);
        return;
    }

    status.execution_state = KERNEL_STARTING;
    jmp_serialize_status(buf, sizeof(buf), &status, &out_len);
    if (buf[0] != 0x02) {
        TEST_FAIL("starting: expected 02, got %02x", buf[0]);
        return;
    }

    TEST_PASS();
}

static void test_execute_reply_ok(const char *vector_hex) {
    TEST_START("execute_reply ok (serialize)");

    jmp_execute_reply_t reply = {
        .status = STATUS_OK,
        .execution_count = 5,
    };

    uint8_t buf[64];
    size_t out_len;
    int rc = jmp_serialize_execute_reply(buf, sizeof(buf), &reply, &out_len);
    if (rc != BINRPC_OK) {
        TEST_FAIL("serialize returned %d", rc);
        return;
    }

    char hex[128];
    hex_encode(buf, out_len, hex);
    if (strcmp(hex, vector_hex) == 0) {
        TEST_PASS();
    } else {
        TEST_FAIL("expected %s, got %s", vector_hex, hex);
    }
}

static void test_execute_request_deserialize(void) {
    TEST_START("execute_request (deserialize)");

    /* Build binary: uint16 code_len + code + uint8 flags */
    const char *code = "print('hi')";
    size_t code_len = strlen(code);
    uint8_t buf[64];
    size_t pos = 0;

    write_uint16_be(buf, (uint16_t)code_len);
    pos += 2;
    memcpy(buf + pos, code, code_len);
    pos += code_len;
    /* flags: store_history(bit1)=1, allow_stdin(bit2)=1, stop_on_error(bit3)=1 = 0x0E */
    buf[pos++] = 0x0E;

    jmp_execute_request_t req;
    size_t out_len;
    int rc = jmp_deserialize_execute_request(buf, pos, &req, &out_len);
    if (rc != BINRPC_OK) {
        TEST_FAIL("deserialize returned %d", rc);
        return;
    }

    if (req.code_len != code_len ||
        memcmp(req.code, code, code_len) != 0 ||
        req.flags != 0x0E) {
        TEST_FAIL("deserialized values don't match");
        return;
    }

    TEST_PASS();
}

static void test_header_roundtrip(void) {
    TEST_START("header (roundtrip)");

    jmp_header_t original = {
        .msg_id = (uint8_t *)"test-msg-001",
        .msg_id_len = 12,
        .session_id = (uint8_t *)"test-session",
        .session_id_len = 12,
        .username = (uint8_t *)"user",
        .username_len = 4,
        .msg_type = JMP_EXECUTE_REQUEST,
        .version = {5, 3, 0},
    };

    uint8_t buf[256];
    size_t ser_len, deser_len;

    int rc = jmp_serialize_header(buf, sizeof(buf), &original, &ser_len);
    if (rc != BINRPC_OK) {
        TEST_FAIL("serialize returned %d", rc);
        return;
    }

    jmp_header_t decoded;
    rc = jmp_deserialize_header(buf, ser_len, &decoded, &deser_len);
    if (rc != BINRPC_OK) {
        TEST_FAIL("deserialize returned %d", rc);
        return;
    }

    if (decoded.msg_id_len != 12 ||
        memcmp(decoded.msg_id, "test-msg-001", 12) != 0 ||
        decoded.session_id_len != 12 ||
        decoded.msg_type != JMP_EXECUTE_REQUEST ||
        decoded.version[0] != 5 || decoded.version[1] != 3) {
        TEST_FAIL("roundtrip values don't match");
        return;
    }

    TEST_PASS();
}

static void test_input_request_serialize(const char *vector_hex) {
    TEST_START("input_request (serialize)");

    jmp_input_request_t req = {
        .prompt = (uint8_t *)"Enter: ",
        .prompt_len = 7,
        .password = 0,
    };

    uint8_t buf[64];
    size_t out_len;
    int rc = jmp_serialize_input_request(buf, sizeof(buf), &req, &out_len);
    if (rc != BINRPC_OK) {
        TEST_FAIL("serialize returned %d", rc);
        return;
    }

    char hex[128];
    hex_encode(buf, out_len, hex);
    if (strcmp(hex, vector_hex) == 0) {
        TEST_PASS();
    } else {
        TEST_FAIL("expected %s, got %s", vector_hex, hex);
    }
}

static void test_input_reply_deserialize(void) {
    TEST_START("input_reply (deserialize)");

    uint8_t buf[] = { 0x00, 0x02, '4', '2' };
    jmp_input_reply_t reply;
    size_t out_len;

    int rc = jmp_deserialize_input_reply(buf, sizeof(buf), &reply, &out_len);
    if (rc != BINRPC_OK) {
        TEST_FAIL("deserialize returned %d", rc);
        return;
    }

    if (reply.value_len != 2 || memcmp(reply.value, "42", 2) != 0) {
        TEST_FAIL("value mismatch");
        return;
    }

    TEST_PASS();
}

static void test_shutdown_roundtrip(void) {
    TEST_START("shutdown (request deserialize + reply serialize)");

    /* shutdown_request: restart=1 */
    uint8_t req_buf[] = { 0x01 };
    jmp_shutdown_request_t req;
    size_t out_len;

    int rc = jmp_deserialize_shutdown_request(req_buf, 1, &req, &out_len);
    if (rc != BINRPC_OK || req.restart != 1) {
        TEST_FAIL("shutdown_request: rc=%d restart=%d", rc, req.restart);
        return;
    }

    /* shutdown_reply: status=ok, restart=1 */
    jmp_shutdown_reply_t reply = { .status = STATUS_OK, .restart = 1 };
    uint8_t rep_buf[4];
    rc = jmp_serialize_shutdown_reply(rep_buf, sizeof(rep_buf), &reply, &out_len);
    if (rc != BINRPC_OK || out_len != 2 || rep_buf[0] != 0x00 || rep_buf[1] != 0x01) {
        TEST_FAIL("shutdown_reply: rc=%d len=%zu bytes=%02x%02x", rc, out_len, rep_buf[0], rep_buf[1]);
        return;
    }

    TEST_PASS();
}

static void test_complete_request_deserialize(void) {
    TEST_START("complete_request (deserialize)");

    /* code="pri", cursor_pos=3 */
    uint8_t buf[] = { 0x00, 0x03, 'p', 'r', 'i', 0x00, 0x03 };
    jmp_complete_request_t req;
    size_t out_len;

    int rc = jmp_deserialize_complete_request(buf, sizeof(buf), &req, &out_len);
    if (rc != BINRPC_OK) {
        TEST_FAIL("deserialize returned %d", rc);
        return;
    }

    if (req.code_len != 3 || memcmp(req.code, "pri", 3) != 0 || req.cursor_pos != 3) {
        TEST_FAIL("code_len=%d cursor_pos=%d", req.code_len, req.cursor_pos);
        return;
    }

    TEST_PASS();
}

static void test_execute_result_serialize(void) {
    TEST_START("execute_result (serialize)");

    uint8_t *key = (uint8_t *)"text/plain";
    uint16_t key_len = 10;
    uint8_t *val = (uint8_t *)"4";
    uint16_t val_len = 1;

    jmp_execute_result_t result = {
        .execution_count = 1,
        .data_count = 1,
        .data_keys_len = &key_len,
        .data_keys = &key,
        .data_values_len = &val_len,
        .data_values = &val,
        .metadata_count = 0,
    };

    uint8_t buf[128];
    size_t out_len;
    int rc = jmp_serialize_execute_result(buf, sizeof(buf), &result, &out_len);
    if (rc != BINRPC_OK) {
        TEST_FAIL("serialize returned %d", rc);
        return;
    }

    char hex[256];
    hex_encode(buf, out_len, hex);

    const char *expected = "00010001000a746578742f706c61696e0001340000";
    if (strcmp(hex, expected) == 0) {
        TEST_PASS();
    } else {
        TEST_FAIL("expected %s, got %s", expected, hex);
    }
}

static void test_error_serialize(void) {
    TEST_START("error (serialize)");

    jmp_error_t err = {
        .status = STATUS_ERROR,
        .ename = (uint8_t *)"ZeroDivisionError",
        .ename_len = 17,
        .evalue = (uint8_t *)"divide by zero",
        .evalue_len = 14,
        .traceback = (uint8_t *)"line 1",
        .traceback_len = 6,
        .execution_count = 1,
    };

    uint8_t buf[256];
    size_t out_len;
    int rc = jmp_serialize_error(buf, sizeof(buf), &err, &out_len);
    if (rc != BINRPC_OK) {
        TEST_FAIL("serialize returned %d", rc);
        return;
    }

    /* Verify structure: status(1) + ename_len(2) + ename + evalue_len(2) + evalue +
       traceback_len(2) + traceback + execution_count(2) */
    if (buf[0] != STATUS_ERROR) {
        TEST_FAIL("status byte: expected 1, got %d", buf[0]);
        return;
    }

    size_t expected_len = 1 + 2 + 17 + 2 + 14 + 2 + 6 + 2;
    if (out_len != expected_len) {
        TEST_FAIL("length: expected %zu, got %zu", expected_len, out_len);
        return;
    }

    /* Verify it can be deserialized */
    jmp_error_t decoded;
    size_t parsed;
    rc = jmp_deserialize_error(buf, out_len, &decoded, &parsed);
    if (rc != BINRPC_OK) {
        TEST_FAIL("deserialize returned %d", rc);
        return;
    }

    if (decoded.ename_len != 17 || memcmp(decoded.ename, "ZeroDivisionError", 17) != 0) {
        TEST_FAIL("ename roundtrip failed");
        return;
    }

    TEST_PASS();
}

static void test_kernel_info_reply_vector(const char *vector_hex) {
    TEST_START("kernel_info_reply (serialize against vector)");

    jmp_kernel_info_reply_t info = {
        .status = STATUS_OK,
        .protocol_version = {5, 3, 0},
        .implementation = (uint8_t *)"micropython",
        .implementation_len = 11,
        .implementation_version = {1, 0, 0},
        .lang_name = (uint8_t *)"MicroPython",
        .lang_name_len = 11,
        .lang_version = {0, 1, 0},
        .mimetype = (uint8_t *)"text/x-python",
        .mimetype_len = 13,
        .file_extension = (uint8_t *)".py",
        .file_ext_len = 3,
        .banner = (uint8_t *)"MicroPython Kernel",
        .banner_len = 18,
        .debugger = 0,
    };

    uint8_t buf[256];
    size_t out_len;
    int rc = jmp_serialize_kernel_info_reply(buf, sizeof(buf), &info, &out_len);
    if (rc != BINRPC_OK) {
        TEST_FAIL("serialize returned %d", rc);
        return;
    }

    char hex[512];
    hex_encode(buf, out_len, hex);
    if (strcmp(hex, vector_hex) == 0) {
        TEST_PASS();
    } else {
        TEST_FAIL("expected %s\n     got %s", vector_hex, hex);
    }
}

/* ---- Main ---- */

int main(void) {
    printf("jmp_bin C test harness\n");
    printf("======================\n\n");

    /* Run tests with hardcoded vectors matching jmp_bin.vectors.json */
    test_stream("00000568656c6c6f");
    test_stream_roundtrip();
    test_status();
    test_execute_reply_ok("000005");
    test_execute_request_deserialize();
    test_header_roundtrip();
    test_input_request_serialize("0007456e7465723a2000");
    test_input_reply_deserialize();
    test_shutdown_roundtrip();
    test_complete_request_deserialize();
    test_execute_result_serialize();
    test_error_serialize();
    test_kernel_info_reply_vector(
        "00050300000b6d6963726f707974686f6e010000000b4d6963726f507974686f6e000100000d746578742f782d707974686f6e00032e707900124d6963726f507974686f6e204b65726e656c00"
    );

    printf("\n======================\n");
    printf("%d/%d tests passed\n", tests_passed, tests_run);

    return (tests_passed == tests_run) ? 0 : 1;
}
