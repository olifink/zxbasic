#ifndef ZXBASIC_CONSOLE_H
#define ZXBASIC_CONSOLE_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include "error.h"

#define ATTR_DEFAULT   (-1)  // Terminal default color (SGR 39 fg / SGR 49 bg)
#define ATTR_INACTIVE  (-2)  // No temporary override active

// Forward declaration of Runtime
struct Runtime;

typedef struct {
    int ink;     // Sinclair 0..7, ATTR_DEFAULT (-1), or ATTR_INACTIVE (-2)
    int paper;   // Sinclair 0..7, ATTR_DEFAULT (-1), or ATTR_INACTIVE (-2)
    int bright;  // 0, 1, or ATTR_INACTIVE (-2)
    int inverse; // 0, 1, or ATTR_INACTIVE (-2)
} ConsoleTermAttrs;

void console_init(struct Runtime *rt);
void console_reset_term_attrs(struct Runtime *rt);
void console_apply_delta(struct Runtime *rt);
void console_reapply_permanent_attrs(struct Runtime *rt);

bool console_set_permanent_ink(struct Runtime *rt, int64_t color, BasicError *err);
bool console_set_permanent_paper(struct Runtime *rt, int64_t color, BasicError *err);
bool console_set_permanent_bright(struct Runtime *rt, int64_t flag, BasicError *err);
bool console_set_permanent_inverse(struct Runtime *rt, int64_t flag, BasicError *err);

bool console_set_temporary_ink(struct Runtime *rt, int64_t color, BasicError *err);
bool console_set_temporary_paper(struct Runtime *rt, int64_t color, BasicError *err);
bool console_set_temporary_bright(struct Runtime *rt, int64_t flag, BasicError *err);
bool console_set_temporary_inverse(struct Runtime *rt, int64_t flag, BasicError *err);

bool console_has_temporary_attrs(const struct Runtime *rt);
void console_reset_temporary_attrs(struct Runtime *rt);

bool console_cursor_at(struct Runtime *rt, int64_t row, int64_t col, BasicError *err);
void console_cls(struct Runtime *rt);

#endif // ZXBASIC_CONSOLE_H
