#ifndef ZXBASIC_ERROR_H
#define ZXBASIC_ERROR_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

typedef enum {
    ERR_OK = 0,               // 0 OK
    ERR_VAR_NOT_FOUND = 2,    // 2 Variable not found
    ERR_SUBSCRIPT_RANGE = 3,  // 3 Subscript out of range
    ERR_OUT_OF_MEMORY = 4,    // 4 Out of memory
    ERR_RETURN_NO_GOSUB = 7,  // 7 Return without GOSUB
    ERR_END_OF_DATA = 8,      // 8 End of DATA
    ERR_STOP = 9,             // 9 STOP statement
    ERR_INTEGER_RANGE = 11,   // B Integer out of range
    ERR_NONSENSE = 12         // C Nonsense in BASIC
} ErrorCode;

typedef struct {
    ErrorCode code;
    const char *custom_msg;
    int32_t line_no; // -1 if in direct mode
    int32_t stmt_index; // 1-based index or statement offset
} BasicError;

const char *error_code_to_report_char(ErrorCode code);
const char *error_code_to_default_msg(ErrorCode code);
void error_format(const BasicError *err, char *buf, size_t buf_size);

#endif // ZXBASIC_ERROR_H
