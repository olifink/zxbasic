#define _POSIX_C_SOURCE 200809L

#include "error.h"
#include <stdio.h>
#include <string.h>

const char *error_code_to_report_char(ErrorCode code) {
    switch (code) {
        case ERR_OK:              return "0";
        case ERR_VAR_NOT_FOUND:   return "2";
        case ERR_SUBSCRIPT_RANGE: return "3";
        case ERR_OUT_OF_MEMORY:   return "4";
        case ERR_RETURN_NO_GOSUB: return "7";
        case ERR_END_OF_DATA:     return "8";
        case ERR_STOP:            return "9";
        case ERR_INTEGER_RANGE:   return "B";
        case ERR_NONSENSE:        return "C";
        default:                  return "?";
    }
}

const char *error_code_to_default_msg(ErrorCode code) {
    switch (code) {
        case ERR_OK:              return "OK";
        case ERR_VAR_NOT_FOUND:   return "Variable not found";
        case ERR_SUBSCRIPT_RANGE: return "Subscript out of range";
        case ERR_OUT_OF_MEMORY:   return "Out of memory";
        case ERR_RETURN_NO_GOSUB: return "Return without GOSUB";
        case ERR_END_OF_DATA:     return "End of DATA";
        case ERR_STOP:            return "STOP statement";
        case ERR_INTEGER_RANGE:   return "Integer out of range";
        case ERR_NONSENSE:        return "Nonsense in BASIC";
        default:                  return "Unknown error";
    }
}

void error_format(const BasicError *err, char *buf, size_t buf_size) {
    if (!err || !buf || buf_size == 0) return;

    const char *rep_code = error_code_to_report_char(err->code);
    const char *msg = err->custom_msg ? err->custom_msg : error_code_to_default_msg(err->code);

    if (err->line_no >= 0) {
        snprintf(buf, buf_size, "%s %s, %d:%d", rep_code, msg, (int)err->line_no, (int)err->stmt_index);
    } else {
        snprintf(buf, buf_size, "%s %s", rep_code, msg);
    }
}
