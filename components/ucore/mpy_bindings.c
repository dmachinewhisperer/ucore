#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <sys/time.h>
#include <time.h>
#include <stddef.h>

#include "ucore/mpy_overrides.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_system.h"

//#include "nvs_flash.h"

#include "esp_task.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_memory_utils.h"
#include "esp_psram.h"

#include "py/cstack.h"
//#include "py/nlr.h"
//#include "py/compile.h"
//#include "py/runtime.h"
#include "py/persistentcode.h"
#include "py/repl.h"
#include "py/gc.h"
#include "py/mphal.h"

#include "py/obj.h"
#include "py/objmodule.h"
#include "py/runtime.h"
#include "py/builtin.h"

//#include "shared/readline/readline.h"
#include "shared/runtime/pyexec.h"
#include "shared/timeutils/timeutils.h"
#include "shared/tinyusb/mp_usbd.h"
#include "mbedtls/platform_time.h"

//#include "uart.h"
//#include "usb.h"
#include "usb_serial_jtag.h"
#include "modmachine.h"
#include "modnetwork.h"

#include "py/runtime.h"    // Core runtime functions (mp_obj_t, etc.)
#include "py/compile.h"    // For mp_compile
#include "py/parse.h"      // For AST surgical splitting
#include "py/lexer.h"      // For mp_lexer_new_from_str_len
#include "py/nlr.h"        // For nlr_push, nlr_pop, nlr_buf_t
#include "py/objexcept.h"  // For exception handling
#include "py/objtype.h"    // For mp_obj_is_subclass_fast

#include "py/misc.h"
#include "py/runtime.h"
#include "py/obj.h"
#include "py/mpprint.h"
#include "extmod/vfs.h"
#include "esp_log.h"

//custom
#include "ucore/ucore.h"
#include "ucore/jmp_bin.h"
#include "ucore/utils.h"

#if MICROPY_BLUETOOTH_NIMBLE
#include "extmod/modbluetooth.h"
#endif

#if MICROPY_PY_ESPNOW
#include "modespnow.h"
#endif

#define __PYEXEC_FAIL             (-1)
#define __PYEXEC_SUCCESS          (0 )
#define __PYEXEC_NLR_FAIL         (-2)
#define __PYEXEC_LEXER_FAIL       (-3)


// MicroPython runs as a task under FreeRTOS
#define MP_TASK_PRIORITY        (ESP_TASK_PRIO_MIN + 1)

// static const char *TAG = "mpyruntime";

extern const mp_obj_type_t esp32_partition_type;

QueueHandle_t mpyruntime_queue;

typedef struct _native_code_node_t {
    struct _native_code_node_t *next;
    uint32_t data[];
} native_code_node_t;

static native_code_node_t *native_code_head = NULL;

static void esp_native_code_free_all(void);


time_t platform_mbedtls_time(time_t *timer) {
    // mbedtls_time requires time in seconds from EPOCH 1970

    struct timeval tv;
    gettimeofday(&tv, NULL);

    return tv.tv_sec + TIMEUTILS_SECONDS_1970_TO_2000;
}

// Simple embedded completion API - add to repl.h

typedef struct {
    const char *str;    // Points to qstr data (no copy needed)
    uint16_t len;
} mp_completion_t;

//to support code completions the completions api of micropython is reimplemeted 
//here to be jmp friendly
int mp_repl_get_completions_direct(
    const uint8_t *code, 
    uint16_t code_len,
    uint16_t cursor_pos,
    mp_completion_t *completions,    // Caller-provided buffer for results
    uint16_t max_completions,        // Size of completions buffer
    uint16_t *cursor_start,          // Where token starts
    uint16_t *cursor_end,            // Where token ends
    const char **common_prefix,      // Common prefix if multiple matches
    uint16_t *common_prefix_len      // Length of common prefix
);

//test_qstr is directly pulled from repl.c
static bool test_qstr(mp_obj_t obj, qstr name) {
    if (obj) {
        // try object member
        mp_obj_t dest[2];
        mp_load_method_protected(obj, name, dest, true);
        return dest[0] != MP_OBJ_NULL;
    } else {
        // try builtin module
        return mp_map_lookup((mp_map_t *)&mp_builtin_module_map, MP_OBJ_NEW_QSTR(name), MP_MAP_LOOKUP) ||
               mp_map_lookup((mp_map_t *)&mp_builtin_extensible_module_map, MP_OBJ_NEW_QSTR(name), MP_MAP_LOOKUP);
    }
}

int mp_repl_get_completions_direct(
    const uint8_t *code, 
    uint16_t code_len,
    uint16_t cursor_pos,
    mp_completion_t *completions,
    uint16_t max_completions,
    uint16_t *cursor_start,
    uint16_t *cursor_end,
    const char **common_prefix,
    uint16_t *common_prefix_len
) {
    if (!code || !completions || !cursor_start || !cursor_end) {
        return 0;
    }
    
    // Clamp cursor position
    if (cursor_pos > code_len) cursor_pos = code_len;
    
    const char *str = (const char*)code;
    const char *org_str = str;
    const char *top = str + cursor_pos;
    
    // Find start of completion chain "a.b.c"
    for (const char *s = top; --s >= str;) {
        if (!(unichar_isalpha(*s) || unichar_isdigit(*s) || *s == '_' || *s == '.')) {
            ++s;
            str = s;
            break;
        }
    }
    
    *cursor_start = str - org_str;
    *cursor_end = cursor_pos;
    
    // Navigate object chain
    mp_obj_t obj = MP_OBJ_FROM_PTR(&mp_module___main__);
    mp_obj_t dest[2];
    
    const char *s_start;
    size_t s_len;
    
    for (;;) {
        s_start = str;
        while (str < top && *str != '.') {
            ++str;
        }
        s_len = str - s_start;
        
        if (str == top) {
            break; // Found the partial token to complete
        }
        
        // Complete word - look it up
        qstr q = qstr_find_strn(s_start, s_len);
        if (q == MP_QSTRnull) {
            return 0; // Not found
        }
        
        mp_load_method_protected(obj, q, dest, true);
        obj = dest[0];
        
        if (obj == MP_OBJ_NULL) {
            return 0; // Not found
        }
        
        ++str; // Skip '.'
    }
    
    // Handle "import" special case
    static const char import_str[] = "import ";
    if (cursor_pos >= 7 && !memcmp(org_str, import_str, 7)) {
        obj = MP_OBJ_NULL;
    }
    
    // Collect matches
    uint16_t match_count = 0;
    size_t nqstr = QSTR_TOTAL();
    
    for (qstr q = MP_QSTR_ + 1; q < nqstr && match_count < max_completions; ++q) {
        size_t d_len;
        const char *d_str = (const char *)qstr_data(q, &d_len);
        
        // Filter underscores unless partial match
        if (s_len == 0 && d_str[0] == '_') {
            continue;
        }
        
        if (s_len <= d_len && strncmp(s_start, d_str, s_len) == 0) {
            if (test_qstr(obj, q)) {
                completions[match_count].str = d_str;
                completions[match_count].len = d_len;
                match_count++;
            }
        }
    }
    
    // Handle "import" completion if no matches
    if (match_count == 0 && s_start == org_str && s_len > 0 && s_len < sizeof(import_str) - 1) {
        if (memcmp(s_start, import_str, s_len) == 0) {
            completions[0].str = import_str;
            completions[0].len = sizeof(import_str) - 1;
            *cursor_start = 0;
            *cursor_end = s_len;
            return 1;
        }
    }
    
    // Find common prefix if multiple matches
    if (match_count > 1 && common_prefix && common_prefix_len) {
        const char *first = completions[0].str;
        uint16_t min_len = completions[0].len;
        
        // Find minimum length
        for (uint16_t i = 1; i < match_count; i++) {
            if (completions[i].len < min_len) {
                min_len = completions[i].len;
            }
        }
        
        // Find common prefix length
        uint16_t prefix_len = s_len;
        for (uint16_t pos = s_len; pos < min_len; pos++) {
            char c = first[pos];
            bool all_match = true;
            
            for (uint16_t i = 1; i < match_count; i++) {
                if (completions[i].str[pos] != c) {
                    all_match = false;
                    break;
                }
            }
            
            if (!all_match) break;
            prefix_len = pos + 1;
        }
        
        *common_prefix = first;
        *common_prefix_len = prefix_len;
    }
    
    return match_count;
}
// Simple handler for Jupyter completions - embedded friendly

int handle_complete_request(const jmp_complete_request_t *req, jmp_complete_reply_t *reply) {
    #define MAX_COMPLETIONS 32
    mp_completion_t completions[MAX_COMPLETIONS];
    
    uint16_t cursor_start, cursor_end;
    const char *common_prefix = NULL;
    uint16_t common_prefix_len = 0;
    int match_count = mp_repl_get_completions_direct(
        req->code,
        req->code_len,
        req->cursor_pos,
        completions,
        MAX_COMPLETIONS,
        &cursor_start,
        &cursor_end,
        &common_prefix,
        &common_prefix_len
    );
    
    reply->cursor_start = cursor_start;
    reply->cursor_end = cursor_end;
    reply->metadata_len = 0;
    reply->status = STATUS_OK;
    
    if (match_count == 0) {
        reply->matches_count = 0;
        reply->matches_len = NULL;
        reply->matches = NULL;
        return BINRPC_OK;
    }
    
    if (match_count == 1 || (common_prefix_len > (cursor_end - cursor_start))) {
        reply->matches_count = 1;
        reply->matches_len = malloc(sizeof(uint16_t));
        reply->matches = malloc(sizeof(uint8_t*));
        
        if (!reply->matches_len || !reply->matches) {
            return BINRPC_ERR_BUFFER_TOO_SMALL;
        }
        
        const char *completion_str;
        uint16_t completion_len;
        
        if (common_prefix && common_prefix_len > (cursor_end - cursor_start)) {
            completion_str = common_prefix;
            completion_len = common_prefix_len;
        } else {
            completion_str = completions[0].str;
            completion_len = completions[0].len;
        }
        
        reply->matches_len[0] = completion_len;
        reply->matches[0] = malloc(completion_len);
        if (!reply->matches[0]) {
            free(reply->matches_len);
            free(reply->matches);
            return BINRPC_ERR_BUFFER_TOO_SMALL;
        }
        
        memcpy(reply->matches[0], completion_str, completion_len);
    } else {
        reply->matches_count = match_count;
        reply->matches_len = malloc(match_count * sizeof(uint16_t));
        reply->matches = malloc(match_count * sizeof(uint8_t*));
        
        if (!reply->matches_len || !reply->matches) {
            return BINRPC_ERR_BUFFER_TOO_SMALL;
        }
        
        for (int i = 0; i < match_count; i++) {
            reply->matches_len[i] = completions[i].len;
            reply->matches[i] = malloc(completions[i].len);
            
            if (!reply->matches[i]) {
                // Cleanup on failure
                for (int j = 0; j < i; j++) {
                    free(reply->matches[j]);
                }
                free(reply->matches);
                free(reply->matches_len);
                return BINRPC_ERR_BUFFER_TOO_SMALL;
            }
            
            memcpy(reply->matches[i], completions[i].str, completions[i].len);
        }
    }
    
    return BINRPC_OK;
}

//in the micropython codebase, the prompt is transmitted
//before mp_hal_readline is called and empty prompt is passed to it instead
//this means that there is really no need of setting the prompt field of the jmp input request as 
//this must have been sent out as a stream message before input() runs
//TODO: fix and submit a pr
int readline_over_jmp(vstr_t *line, const char *prompt) {
    char *str = ucore_raw_input(prompt);
    int len = strlen(str);
    vstr_add_strn(line, str, len);
    return len;
}

//core mpy vm bindings
static mp_obj_t ucore_last_result = MP_OBJ_NULL;

static mp_obj_t ucore_repl_print(mp_obj_t obj) {
    // Suppress None result (standard Jupyter/REPL behavior)
    if (obj == mp_const_none) {
        ucore_last_result = MP_OBJ_NULL;
        return mp_const_none;
    }

    // Unconditionally log for now to debug why it's not firing
    vstr_t vstr;
    mp_print_t print;
    vstr_init_print(&vstr, 32, &print);
    mp_obj_print_helper(&print, obj, PRINT_REPR);
    printf("DEBUG: ucore_repl_print object: %s\n", vstr_str(&vstr));
    vstr_clear(&vstr);

    ucore_last_result = obj;
    return mp_const_none;
}
// Node kinds for AST surgery
typedef enum {
    #define DEF_RULE(rule, comp, kind, ...) PN_##rule,
    #define DEF_RULE_NC(rule, kind, ...)
    #include "py/grammar.h"
    #undef DEF_RULE
    #undef DEF_RULE_NC
    PN_const_object,
    #define DEF_RULE(rule, comp, kind, ...)
    #define DEF_RULE_NC(rule, kind, ...) PN_##rule,
    #include "py/grammar.h"
    #undef DEF_RULE
    #undef DEF_RULE_NC
} pn_kind_t;

static MP_DEFINE_CONST_FUN_OBJ_1(ucore_repl_print_obj, ucore_repl_print);

int execute_str(const char *source, int len, jmp_error_t *err) {
    printf("DEBUG: execute_str source='%.*s' (len=%d)\n", len, source, len);
    memset(err, 0, sizeof(jmp_error_t));
    
    mp_lexer_t *lex = mp_lexer_new_from_str_len(MP_QSTR__lt_stdin_gt_, source, len, 0);
    if (lex == NULL) {
        err->status = STATUS_ERROR;
        const char *ename_str = "LexerError";
        const char *evalue_str = "Failed to tokenize input";
        
        err->ename_len = strlen(ename_str);
        err->ename = (uint8_t *)malloc(err->ename_len + 1);
        memcpy(err->ename, ename_str, err->ename_len + 1);
        
        err->evalue_len = strlen(evalue_str);
        err->evalue = (uint8_t *)malloc(err->evalue_len + 1);
        memcpy(err->evalue, evalue_str, err->evalue_len + 1);
        
        err->traceback_len = 0;
        err->traceback = NULL;
        return __PYEXEC_LEXER_FAIL;
    }
    
    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
        // Success path - execute the code
        // ucore_last_result is NULL after execution
        ucore_last_result = MP_OBJ_NULL;
        mp_store_global(MP_QSTR___repl_print__, MP_OBJ_FROM_PTR(&ucore_repl_print_obj));
        
        // === Strategy: AST-guided source splitting ===
        // 1. Parse as FILE_INPUT to validate and find the last statement's source line
        // 2. Free the parse tree
        // 3. Re-parse preamble as FILE_INPUT (no REPL), execute
        // 4. Re-parse last statement as SINGLE_INPUT (with REPL), execute
        //
        // We MUST re-parse the last statement as SINGLE_INPUT because FILE_INPUT
        // mode folds away expr_stmt nodes, preventing __repl_print__ from firing.
        
        mp_parse_tree_t parse_tree = mp_parse(lex, MP_PARSE_FILE_INPUT);
        mp_parse_node_t root = parse_tree.root;

        // Determine if there are multiple statements and find the last one's line
        bool has_preamble = false;
        size_t last_stmt_line = 0;

        if (MP_PARSE_NODE_IS_STRUCT_KIND(root, PN_file_input_2)) {
            mp_parse_node_t *nodes = NULL;
            size_t n_nodes = mp_parse_node_extract_list(&root, PN_file_input_2, &nodes);
            
            // Count non-NEWLINE nodes and track the last one's source line
            int non_nl_count = 0;
            for (size_t j = 0; j < n_nodes; j++) {
                if (!MP_PARSE_NODE_IS_TOKEN_KIND(nodes[j], MP_TOKEN_NEWLINE)) {
                    non_nl_count++;
                    if (MP_PARSE_NODE_IS_STRUCT(nodes[j])) {
                        last_stmt_line = ((mp_parse_node_struct_t*)nodes[j])->source_line;
                    }
                }
            }
            has_preamble = (non_nl_count >= 2 && last_stmt_line > 1);
            printf("DEBUG: file_input_2: %d non-NL nodes, last_stmt_line=%d, has_preamble=%d\n",
                   non_nl_count, (int)last_stmt_line, (int)has_preamble);
        } else {
            printf("DEBUG: single statement (no file_input_2)\n");
        }

        // Free the FILE_INPUT parse tree - we'll re-parse from source
        mp_parse_tree_clear(&parse_tree);

        if (!has_preamble) {
            // Single statement (or couldn't split): parse entire source as SINGLE_INPUT
            printf("DEBUG: Parsing entire source as SINGLE_INPUT\n");
            mp_lexer_t *lex2 = mp_lexer_new_from_str_len(MP_QSTR__lt_stdin_gt_, source, len, 0);
            mp_parse_tree_t pt = mp_parse(lex2, MP_PARSE_SINGLE_INPUT);
            mp_obj_t fun = mp_compile(&pt, MP_QSTR__lt_stdin_gt_, true);
            mp_call_function_0(fun);
            mp_handle_pending(true);
        } else {
            // Find byte offset where the last statement begins
            const char *p = source;
            size_t cur_line = 1;
            while (cur_line < last_stmt_line && p < source + len) {
                if (*p == '\n') cur_line++;
                p++;
            }
            size_t preamble_len = p - source;
            const char *last_src = p;
            size_t last_len = len - preamble_len;

            printf("DEBUG: Splitting at line %d (preamble=%d bytes, last=%d bytes)\n",
                   (int)last_stmt_line, (int)preamble_len, (int)last_len);

            // Execute preamble as FILE_INPUT (no REPL behavior)
            if (preamble_len > 0) {
                mp_lexer_t *lex_pre = mp_lexer_new_from_str_len(MP_QSTR__lt_stdin_gt_, source, preamble_len, 0);
                mp_parse_tree_t pt_pre = mp_parse(lex_pre, MP_PARSE_FILE_INPUT);
                mp_obj_t fun_pre = mp_compile(&pt_pre, MP_QSTR__lt_stdin_gt_, false);
                mp_call_function_0(fun_pre);
                mp_handle_pending(true);
            }

            // Execute last statement as SINGLE_INPUT (triggers __repl_print__ for expressions)
            mp_lexer_t *lex_last = mp_lexer_new_from_str_len(MP_QSTR__lt_stdin_gt_, last_src, last_len, 0);
            mp_parse_tree_t pt_last = mp_parse(lex_last, MP_PARSE_SINGLE_INPUT);
            mp_obj_t fun_last = mp_compile(&pt_last, MP_QSTR__lt_stdin_gt_, true);
            mp_call_function_0(fun_last);
            mp_handle_pending(true);
        }

        nlr_pop();
        gc_collect();
        
        err->status = STATUS_OK;
        return __PYEXEC_SUCCESS;
    } else {
        // Exception caught - now we need to safely extract info
        // Wrap this in ANOTHER nlr_push to catch any exceptions during extraction
        nlr_buf_t nlr2;
        if (nlr_push(&nlr2) == 0) {
            mp_obj_t exc = MP_OBJ_FROM_PTR(nlr.ret_val);
            err->status = STATUS_ERROR;
            
            const char *ename_str = qstr_str(((mp_obj_type_t *)mp_obj_get_type(exc))->name);
            err->ename_len = strlen(ename_str);
            err->ename = (uint8_t *)malloc(err->ename_len + 1);
            memcpy(err->ename, ename_str, err->ename_len + 1);
            
            vstr_t eval_vstr;
            mp_print_t eval_print;
            vstr_init_print(&eval_vstr, 64, &eval_print);
            mp_obj_print_helper(&eval_print, exc, PRINT_STR);
            
            err->evalue_len = eval_vstr.len;
            err->evalue = (uint8_t *)malloc(err->evalue_len + 1);
            memcpy(err->evalue, eval_vstr.buf, eval_vstr.len);
            err->evalue[eval_vstr.len] = '\0';
            vstr_clear(&eval_vstr);
            
            vstr_t vstr;
            mp_print_t print;
            vstr_init_print(&vstr, 64, &print);
            
            mp_obj_print_exception(&print, exc);
            
            err->traceback_len = vstr.len;
            err->traceback = (uint8_t *)malloc(vstr.len + 1);
            memcpy(err->traceback, vstr.buf, vstr.len);
            err->traceback[vstr.len] = '\0';
            
            //printf("Extracted traceback (len=%d):\n%s", err->traceback_len, err->traceback);
            
            vstr_clear(&vstr);
            
            nlr_pop();  // Pop the nlr2 handler
            gc_collect();
            
            if (mp_obj_is_subclass_fast(MP_OBJ_FROM_PTR(((mp_obj_base_t *)nlr.ret_val)->type), 
                                         MP_OBJ_FROM_PTR(&mp_type_SystemExit))) {
                return PYEXEC_FORCED_EXIT;
            } else {
                return __PYEXEC_NLR_FAIL;
            }
        } else {
            // Failed while trying to extract exception info
            err->status = STATUS_ERROR;
            const char *ename_str = "InternalError";
            const char *evalue_str = "Exception handling failed";
            
            err->ename_len = strlen(ename_str);
            err->ename = (uint8_t *)malloc(err->ename_len + 1);
            memcpy(err->ename, ename_str, err->ename_len + 1);
            
            err->evalue_len = strlen(evalue_str);
            err->evalue = (uint8_t *)malloc(err->evalue_len + 1);
            memcpy(err->evalue, evalue_str, err->evalue_len + 1);
            
            err->traceback_len = 0;
            err->traceback = NULL;
            
            return __PYEXEC_NLR_FAIL;
        }
    }
}

/*
both mp_hal_stdout_net_tx_strn and mp_hal_stdout_net_tx_strn_cooked(orignal names without _pusher)
 are taken from 
shared\runtime\stdout_helpers.c and reimplemented to enable logging over jmp

*/
static mp_uint_t _mp_hal_stdout_tx_strn(const char *str, size_t len) {
    // Only release the GIL if many characters are being sent
    mp_uint_t ret = len;
    bool did_write = false;

    bool release_gil = len > MICROPY_PY_STRING_TX_GIL_THRESHOLD;
    
    #if MICROPY_DEBUG_PRINTERS && MICROPY_DEBUG_VERBOSE && MICROPY_PY_THREAD_GIL
    // Ensure the GIL is properly initialized before releasing it
    release_gil = release_gil && (MP_STATE_VM(gil_mutex).handle != NULL);
    #endif

    if (release_gil) {
        MP_THREAD_GIL_EXIT();
    }

    int res = iopub_print(str, len);
    if (res > 0) {
        did_write = true;
        ret = MIN((mp_uint_t)res, ret);
    }

    if (release_gil) {
        MP_THREAD_GIL_ENTER();
    }

    return did_write ? ret : 0;
}

static void _mp_hal_stdout_tx_strn_cooked(const char *str, size_t len) {
    const char *last = str;
    while (len--) {
        if (*str == '\n') {
            if (str > last) {
                _mp_hal_stdout_tx_strn(last, str - last);
            }
            _mp_hal_stdout_tx_strn("\r\n", 2);
            ++str;
            last = str;
        } else {
            ++str;
        }
    }
    if (str > last) {
        _mp_hal_stdout_tx_strn(last, str - last);
    }
}

//TODO: make the core log infra redirectable to other outputs like uart for debugging
void mpyruntime_log(const char*log, size_t len){
    _mp_hal_stdout_tx_strn_cooked(log, len);
}
void mpyruntime_keyboard_interrupt(){
    mp_sched_keyboard_interrupt();
}

// ---- JMP Message Handlers (run on mp_task, need micropython runtime context) ----

// Returns true if a soft reset is needed
static bool handle_execute_request(request_context_t *req_ctx) {

    //uint8_t *payload = NULL; 
    int exit_code = 0;
    size_t out_len;
    jmp_execute_request_t execute_req;
    jmp_deserialize_execute_request(req_ctx->msg->content, req_ctx->msg->content_len, &execute_req, &out_len);

    //if code lenght is zero, frontend expects kernel to not execute 
    //anything but instead send the curernt execution count. 
    //note that error init needs to be before the goto so that error is defined
    jmp_error_t error = {0};

    if(execute_req.code_len == 0){
        exit_code = 0;
        goto response;
    }

    exit_code = execute_str((const char*)execute_req.code, execute_req.code_len, &error); 
    kcontext.execution_count++;

    // Dispatch execute_result if we have a captured object
    printf("DEBUG: Finishing execution, exit_code=%d, ucore_last_result=%p\n", exit_code, (void*)ucore_last_result);
    if (exit_code == __PYEXEC_SUCCESS && ucore_last_result != MP_OBJ_NULL) {
        printf("DEBUG: Attempting to send execute_result message\n");
        vstr_t vstr;
        mp_print_t print;
        vstr_init_print(&vstr, 64, &print);
        mp_obj_print_helper(&print, ucore_last_result, PRINT_REPR);

        jmp_execute_result_t result = {
            .execution_count = kcontext.execution_count,
            .data_count = 1,
            .metadata_count = 0
        };
        
        uint16_t key_len = 10;
        uint8_t *key = (uint8_t*)"text/plain";
        uint16_t val_len = vstr.len;
        uint8_t *val = (uint8_t*)vstr.buf;
        
        result.data_keys_len = &key_len;
        result.data_keys = &key;
        result.data_values_len = &val_len;
        result.data_values = &val;

        size_t res_content_len = 0;
        size_t res_max_len = sizeof(jmp_execute_result_t) + key_len + val_len + 32;

        uint8_t *res_content = malloc(res_max_len);
        if (res_content) {
            jmp_serialize_execute_result(res_content, res_max_len, &result, &res_content_len);
            ucore_send_reply(req_ctx, JMP_EXECUTE_RESULT, res_content, res_content_len);
            free(res_content);
        }
        vstr_clear(&vstr);
    }
    
response:;
    size_t content_len = 0;

    if(exit_code == 0){
        jmp_execute_reply_t execute_rep = {
            .status = STATUS_OK,
            .execution_count = kcontext.execution_count,
        };
        uint8_t content[sizeof(jmp_execute_reply_t)];
        jmp_serialize_execute_reply(content, sizeof(content), &execute_rep, &content_len);
        ucore_send_reply(req_ctx, JMP_EXECUTE_REPLY, content, content_len);
    } 
    else{
        error.execution_count = kcontext.execution_count;
        size_t err_max = sizeof(jmp_error_t) + error.ename_len + error.evalue_len + error.traceback_len;
        uint8_t *err_content = malloc(err_max);
        if(!err_content){
            // malloc failure is fatal, it means we are out 
            // of heap and cannot make new packets so reset the runtime
            goto execute_req_cleanup_fatal;
        }
        int ret = jmp_serialize_error(err_content, err_max, &error, &content_len);
        printf("jmp_serialize_error return code: %d", ret);

        //websocket_bin_tx(websock, payload, offset);   
        ucore_send_reply(req_ctx, JMP_EXECUTE_REPLY, err_content, content_len);

        //in the case of error , we need to form another message that is sent to IoPub. 
        // this message is of type JMP_ERROR and has the same content as status=error messages. 
        ucore_send_reply(req_ctx, JMP_ERROR, err_content, content_len);
        free(err_content);
    }

execute_req_cleanup_fatal:
    if(error.status > 0){
        free(error.ename);
        free(error.evalue);
        free(error.traceback);
    }

    if (exit_code & PYEXEC_FORCED_EXIT) {
        return true;
    }
    return false;
}

// Returns true (always triggers soft reset)
static bool handle_shutdown_request(request_context_t *req_ctx) {
    size_t out_len;
    jmp_shutdown_request_t shutdown_req;
    jmp_deserialize_shutdown_request(req_ctx->msg->content, req_ctx->msg->content_len, &shutdown_req, &out_len);

    jmp_shutdown_reply_t shutdown_rep = {
        .status = STATUS_OK,
        .restart = 1,
    };
    size_t content_len = 0;
    uint8_t content[sizeof(jmp_shutdown_reply_t)];
    jmp_serialize_shutdown_reply(content, sizeof(content), &shutdown_rep, &content_len);
    //websocket_bin_tx(websock, payload, offset);
    ucore_send_reply(req_ctx, JMP_SHUTDOWN_REPLY, content, content_len);

    //TODO: find out if the jmp standard specifies 
    //if kernel state must necessarily be cleared upon restart
    kcontext.execution_count = 0; 

    //restart the kernel
    return true;
}

// Returns true if soft reset needed (on malloc failure)
static bool handle_complete_request_msg(request_context_t *req_ctx) {
    size_t out_len;
    jmp_complete_request_t complete_req;
    jmp_deserialize_complete_request(req_ctx->msg->content, req_ctx->msg->content_len, &complete_req, &out_len);

    jmp_complete_reply_t complete_rep = {0};
    int ret = handle_complete_request(&complete_req, &complete_rep);
    //TODO: should really make the return codes uniform accross modules and files
    bool do_soft_reset = false;
    if (ret != BINRPC_OK) {
        goto complete_req_cleanup;
    }

    size_t total_match_string_len = 0;
    for (int i = 0; i < complete_rep.matches_count; ++i) {
        total_match_string_len += complete_rep.matches_len[i];
    }

    size_t content_len = 0;
    size_t content_max = sizeof(uint16_t) * (complete_rep.matches_count + 1) +
                         total_match_string_len + 32;

    uint8_t *content = malloc(content_max);
    if (!content) {
        do_soft_reset = true;
        goto complete_req_cleanup;
    }

    jmp_serialize_complete_reply(content, content_max, &complete_rep, &content_len);
    ucore_send_reply(req_ctx, JMP_COMPLETE_REPLY, content, content_len);
    free(content);

complete_req_cleanup:
    for (int i = 0; i < complete_rep.matches_count; ++i) {
        if (complete_rep.matches && complete_rep.matches[i]) {
            free(complete_rep.matches[i]);
        }
    }
    free(complete_rep.matches);
    free(complete_rep.matches_len);

    return do_soft_reset;
}

// ---- MicroPython runtime task ----

void mp_task(void *pvParameter) {
    volatile uint32_t sp = (uint32_t)esp_cpu_get_sp();
    #if MICROPY_PY_THREAD
    mp_thread_init(pxTaskGetStackStart(NULL), MICROPY_TASK_STACK_SIZE / sizeof(uintptr_t));
    #endif

    machine_init();

    // Configure time function, for mbedtls certificate time validation.
    mbedtls_platform_set_time(platform_mbedtls_time);
    void *mp_task_heap = MP_PLAT_ALLOC_HEAP(MICROPY_GC_INITIAL_HEAP_SIZE);
    if (mp_task_heap == NULL) {
        printf("mp_task_heap allocation failed!\n");
        esp_restart();
    }

soft_reset:
    // initialise the stack pointer for the main thread
    mp_cstack_init_with_top((void *)sp, MICROPY_TASK_STACK_SIZE);
    gc_init(mp_task_heap, mp_task_heap + MICROPY_GC_INITIAL_HEAP_SIZE);
    mp_init();
    mp_obj_list_append(mp_sys_path, MP_OBJ_NEW_QSTR(MP_QSTR__slash_lib));
    //readline_init0();

    // initialise peripherals
    machine_pins_init();
    //#if MICROPY_PY_MACHINE_I2S
    //machine_i2s_init0();
    //#endif

    // run boot-up scripts
    pyexec_frozen_module("_boot.py", false);
    int ret = pyexec_file_if_exists("boot.py");
    if (ret & PYEXEC_FORCED_EXIT) {
        goto soft_reset_exit;
    }
    /*
    if (pyexec_mode_kind == PYEXEC_MODE_FRIENDLY_REPL && ret != 0) {
        int ret = pyexec_file_if_exists("main.py");
        if (ret & PYEXEC_FORCED_EXIT) {
            goto soft_reset_exit;
        }
    }
    */
// main task logic
    
    queue_pkt_t pkt = {0}; 
    while (1) {
        pkt.payload = NULL;
        if (xQueueReceive(mpyruntime_queue, &pkt, portMAX_DELAY) == pdTRUE) {   
            size_t out_len;
            jmp_message_t msg; 
            jmp_dissassemble_message((uint8_t*)pkt.payload + UCORE_SANS_PREFIX_LEN, pkt.payload_len - UCORE_SANS_PREFIX_LEN, &msg);
            jmp_header_t req_header; 
            jmp_deserialize_header(msg.header, msg.header_len, &req_header, &out_len);

            request_context_t req_ctx = {
                .msg = &msg,
                .header = &req_header,
                .payload = (uint8_t*)pkt.payload,
                .payload_len = pkt.payload_len,
            };

            //store a references to the items in 
            //in the execution  state so that execution side effects can use it
            kcontext.current_req = &req_ctx;

            bool do_soft_reset = false;

            iopub_status(KERNEL_BUSY);
          
            switch(req_header.msg_type){
                case JMP_EXECUTE_REQUEST:
                    do_soft_reset = handle_execute_request(&req_ctx);
                    break;
                case JMP_SHUTDOWN_REQUEST:
                    do_soft_reset = handle_shutdown_request(&req_ctx);
                    break;
                case JMP_COMPLETE_REQUEST:
                    do_soft_reset = handle_complete_request_msg(&req_ctx);
                    break;
                default:
                    break;    
            }

//loop_cleanup:            
            iopub_status(KERNEL_IDLE);

            kcontext.current_req = NULL;

            if(pkt.payload) {
                free(pkt.payload);
                pkt.payload = NULL;
            }
            

            if(do_soft_reset){
                goto soft_reset_exit;
            }

        }
    }

soft_reset_exit:
    kcontext.soft_reset_count++;
    #if MICROPY_BLUETOOTH_NIMBLE
    mp_bluetooth_deinit();
    #endif

    #if MICROPY_PY_ESPNOW
    espnow_deinit(mp_const_none);
    MP_STATE_PORT(espnow_singleton) = NULL;
    #endif

    machine_timer_deinit_all();

    #if MICROPY_PY_THREAD
    mp_thread_deinit();
    #endif

    #if MICROPY_HW_ENABLE_USB_RUNTIME_DEVICE
    mp_usbd_deinit();
    #endif

    gc_sweep_all();

    // Free any native code pointers that point to iRAM.
    esp_native_code_free_all();

    mp_hal_stdout_tx_str("MPY: soft reboot\r\n");

    // deinitialise peripherals
    machine_pwm_deinit_all();
    // TODO: machine_rmt_deinit_all();
    machine_pins_deinit();
    machine_deinit();
    #if MICROPY_PY_SOCKET_EVENTS
    socket_events_deinit();
    #endif

    mp_deinit();
    fflush(stdout);
    goto soft_reset;
}

void mpyruntime_start(){
    mpyruntime_queue = xQueueCreate(QUEUE_GENERIC_LEN, sizeof(queue_pkt_t));

    // register the keyboard interrupt callback so ucore can trigger it without depending on mpy_bindings
    kcontext.keyboard_interrupt_fn = mpyruntime_keyboard_interrupt;

    xTaskCreatePinnedToCore(mp_task, "mp_task", MICROPY_TASK_STACK_SIZE / sizeof(StackType_t), NULL, MP_TASK_PRIORITY, &mp_main_task_handle, MP_TASK_COREID);
}


void nlr_jump_fail(void *val) {
    printf("NLR jump failed, val=%p\n", val);
    esp_restart();
}

static void esp_native_code_free_all(void) {
    while (native_code_head != NULL) {
        native_code_node_t *next = native_code_head->next;
        heap_caps_free(native_code_head);
        native_code_head = next;
    }
}

void *esp_native_code_commit(void *buf, size_t len, void *reloc) {
    len = (len + 3) & ~3;
    size_t len_node = sizeof(native_code_node_t) + len;
    native_code_node_t *node = heap_caps_malloc(len_node, MALLOC_CAP_EXEC);
    #if CONFIG_IDF_TARGET_ESP32S2
    // Workaround for ESP-IDF bug https://github.com/espressif/esp-idf/issues/14835
    if (node != NULL && !esp_ptr_executable(node)) {
        free(node);
        node = NULL;
    }
    #endif // CONFIG_IDF_TARGET_ESP32S2
    if (node == NULL) {
        m_malloc_fail(len_node);
    }
    node->next = native_code_head;
    native_code_head = node;
    void *p = node->data;
    if (reloc) {
        mp_native_relocate(reloc, buf, (uintptr_t)p);
    }
    memcpy(p, buf, len);
    return p;
}

/**
//vfs bindings
static mp_obj_t create_blockdev(const char* partition_label) {
    mp_obj_t args[2];
    args[0] = mp_obj_new_str(partition_label, strlen(partition_label));
    args[1] = MP_OBJ_NEW_SMALL_INT(4096);
    
    mp_obj_t partition = MP_OBJ_TYPE_GET_SLOT(&esp32_partition_type, make_new)(
        &esp32_partition_type, 2, 0, args);
    
    return partition;
}

//given a mount point, return the associated fatfs handle if any vfs is mounted
esp_err_t __fs_fatfs_at_mount_point(const char *path, const char **path_out, FATFS **out_fatfs) {
    mp_vfs_mount_t *vfs = mp_vfs_lookup_path(path, path_out);
    if (vfs == MP_VFS_NONE || vfs == MP_VFS_ROOT) {
        return ESP_FAIL;
    }

    fs_user_mount_t *umount = MP_OBJ_TO_PTR(vfs->obj);
    if (!mp_obj_is_type(vfs->obj, &mp_fat_vfs_type)) {
        return ESP_FAIL;
    }

    *out_fatfs = &umount->fatfs;

    return ESP_OK;
}

//proxied call to fs_mount from fatfs lib, does the following:
//creates a new fs_user_mount_t, inits it with the esp_partition_type blockdev,
//wraps the fs_user_mount_t object in a mp_vfs_mount_t object and registers it with micropythons vm,
//note that the fs_mount_t.fatfs.drv points back to the parent. this is how the fatfs diskio abstraction
// can operate on the underlying block device
//after this function runs, can now use other fatfs apis . 

//micropython does not support mounting a filesystem at / like unix does, mount points must always be a 
// /some_dir ..
esp_err_t __fs_mount(const char* partition_label, const char* mount_point, bool readonly, bool mkfs) {
    mp_obj_t partition = create_blockdev(partition_label);
    
    static mp_map_elem_t map_elements[2];
    mp_map_t kw_args = {
        .all_keys_are_qstrs = 1,
        .is_fixed = 1,
        .is_ordered = 0,
        .used = 0,
        .alloc = 2,
        .table = map_elements
    };
    
    if (readonly) {
        map_elements[kw_args.used].key = MP_QSTR_readonly;
        map_elements[kw_args.used].value = mp_const_true;
        kw_args.used++;
    }
    
    if (mkfs) {
        map_elements[kw_args.used].key = MP_QSTR_mkfs;
        map_elements[kw_args.used].value = mp_const_true;
        kw_args.used++;
    }
    
    mp_obj_t pos_args[2] = {partition, mp_obj_new_str(mount_point, strlen(mount_point))};
    
    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
        mp_vfs_mount(2, pos_args, &kw_args);
        nlr_pop();
        return ESP_OK;
    } else {
        if (mp_obj_is_subclass_fast(MP_OBJ_FROM_PTR(((mp_obj_base_t*)nlr.ret_val)->type),
                                    MP_OBJ_FROM_PTR(&mp_type_OSError))) {
            mp_obj_exception_t *exc = MP_OBJ_TO_PTR(nlr.ret_val);
            return (esp_err_t)mp_obj_get_int(exc->args->items[0]); 
        }
        return ESP_FAIL; 
    }
}

//check alloc'ed string is cleaned up properly!!
esp_err_t __fs_unmount(const char *mount_point){
    mp_obj_t mount_obj = mp_obj_new_str(mount_point, strlen(mount_point));
    mp_vfs_umount(mount_obj);

    return ESP_OK;
}

*/