#ifndef UCORE_MPY_BINDINGS_H_
#define UCORE_MPY_BINDINGS_H_
#include <stdbool.h>

#include "py/runtime.h"    // Core runtime functions (mp_obj_t, etc.)
#include "py/compile.h"    // For mp_compile
#include "py/lexer.h"      // For mp_lexer_new_from_str_len
#include "py/parse.h"      // For mp_parse, MP_PARSE_FILE_INPUT
#include "py/nlr.h"        // For nlr_push, nlr_pop, nlr_buf_t
#include "py/objexcept.h"  // For exception handling
#include "py/objtype.h"    // For mp_obj_is_subclass_fast

#include "py/misc.h"
#include "esp_log.h"

void mpyruntime_start(void);

/*
primary logging facility for mpyruntime
*/
void mpyruntime_log(const char*log, size_t len);

int readline_over_jmp(vstr_t *line, const char *prompt);

void mpyruntime_keyboard_interrupt();
/*
esp_err_t __fs_unmount(const char *mount_point);
esp_err_t __fs_fatfs_at_mount_point(const char *path, const char **path_out, FATFS **out_fatfs);
esp_err_t __fs_mount(const char* partition_label, const char* mount_point, bool readonly, bool mkfs);
*/
#endif