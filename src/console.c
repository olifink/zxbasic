#define _POSIX_C_SOURCE 200809L

#include "console.h"
#include "runtime.h"
#include "sysvars.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Sinclair 3-bit color palette mapped to standard ANSI 8-color matrix offsets
// Sinclair: 0=Black, 1=Blue, 2=Red, 3=Magenta, 4=Green, 5=Cyan, 6=Yellow, 7=White
// ANSI:     0=Black, 1=Red,  2=Green, 3=Yellow, 4=Blue, 5=Magenta, 6=Cyan, 7=White
static const int sinclair_to_ansi[8] = { 0, 4, 1, 5, 2, 6, 3, 7 };

void console_init(Runtime *rt) {
    if (!rt) return;
    console_reset_term_attrs(rt);
}

void console_reset_term_attrs(Runtime *rt) {
    if (!rt) return;
    rt->term_attrs.ink = ATTR_INACTIVE;
    rt->term_attrs.paper = ATTR_INACTIVE;
    rt->term_attrs.bright = ATTR_INACTIVE;
    rt->term_attrs.inverse = ATTR_INACTIVE;
    rt->ink_explicitly_set = false;
    rt->paper_explicitly_set = false;
}

void console_apply_delta(Runtime *rt) {
    if (!rt || !rt->out) return;

    int64_t p_ink = ATTR_DEFAULT, p_paper = ATTR_DEFAULT, p_bright = 0, p_inv = 0;
    int64_t t_ink = ATTR_INACTIVE, t_paper = ATTR_INACTIVE, t_bright = ATTR_INACTIVE, t_inv = ATTR_INACTIVE;

    sysvar_get_int(&rt->sysvars, "ATTR_P_INK", &p_ink);
    sysvar_get_int(&rt->sysvars, "ATTR_P_PAPER", &p_paper);
    sysvar_get_int(&rt->sysvars, "ATTR_P_BRIGHT", &p_bright);
    sysvar_get_int(&rt->sysvars, "ATTR_P_INVERSE", &p_inv);

    sysvar_get_int(&rt->sysvars, "ATTR_T_INK", &t_ink);
    sysvar_get_int(&rt->sysvars, "ATTR_T_PAPER", &t_paper);
    sysvar_get_int(&rt->sysvars, "ATTR_T_BRIGHT", &t_bright);
    sysvar_get_int(&rt->sysvars, "ATTR_T_INVERSE", &t_inv);

    int act_ink = (t_ink != ATTR_INACTIVE) ? (int)t_ink : (int)p_ink;
    int act_paper = (t_paper != ATTR_INACTIVE) ? (int)t_paper : (int)p_paper;
    int act_bright = (t_bright != ATTR_INACTIVE) ? (int)t_bright : (int)p_bright;
    int act_inv = (t_inv != ATTR_INACTIVE) ? (int)t_inv : (int)p_inv;
    if (act_bright < 0) act_bright = 0;
    if (act_inv < 0) act_inv = 0;

    // 1. Inverse delta
    if (rt->term_attrs.inverse != act_inv) {
        if (act_inv == 1) {
            fprintf(rt->out, "\033[7m");
            rt->term_attrs.inverse = 1;
        } else {
            if (rt->term_attrs.inverse == 1) {
                fprintf(rt->out, "\033[27m");
            }
            rt->term_attrs.inverse = 0;
        }
    }

    // 2. Bright delta
    if (rt->term_attrs.bright != act_bright) {
        if (rt->term_attrs.bright != ATTR_INACTIVE) {
            // Brightness changed, re-emit any active ink and paper colors
            if (rt->term_attrs.ink != ATTR_INACTIVE || rt->ink_explicitly_set || t_ink != ATTR_INACTIVE) {
                rt->term_attrs.ink = ATTR_INACTIVE;
            }
            if (rt->term_attrs.paper != ATTR_INACTIVE || rt->paper_explicitly_set || t_paper != ATTR_INACTIVE) {
                rt->term_attrs.paper = ATTR_INACTIVE;
            }
        }
        rt->term_attrs.bright = act_bright;
    }

    // 3. Ink delta
    if (rt->term_attrs.ink != act_ink) {
        if (t_ink != ATTR_INACTIVE || rt->ink_explicitly_set || rt->term_attrs.ink != ATTR_INACTIVE) {
            if (act_ink == ATTR_DEFAULT) {
                fprintf(rt->out, "\033[39m");
                rt->term_attrs.ink = ATTR_DEFAULT;
            } else if (act_ink >= 0 && act_ink <= 7) {
                int ansi_c = sinclair_to_ansi[act_ink];
                int code = (act_bright ? 90 : 30) + ansi_c;
                fprintf(rt->out, "\033[%dm", code);
                rt->term_attrs.ink = act_ink;
            }
        }
    }

    // 4. Paper delta
    if (rt->term_attrs.paper != act_paper) {
        if (t_paper != ATTR_INACTIVE || rt->paper_explicitly_set || rt->term_attrs.paper != ATTR_INACTIVE) {
            if (act_paper == ATTR_DEFAULT) {
                fprintf(rt->out, "\033[49m");
                rt->term_attrs.paper = ATTR_DEFAULT;
            } else if (act_paper >= 0 && act_paper <= 7) {
                int ansi_c = sinclair_to_ansi[act_paper];
                int code = (act_bright ? 100 : 40) + ansi_c;
                fprintf(rt->out, "\033[%dm", code);
                rt->term_attrs.paper = act_paper;
            }
        }
    }

    fflush(rt->out);
}

void console_reapply_permanent_attrs(Runtime *rt) {
    if (!rt || !rt->out) return;

    int64_t p_ink = ATTR_DEFAULT, p_paper = ATTR_DEFAULT, p_bright = 0, p_inv = 0;
    sysvar_get_int(&rt->sysvars, "ATTR_P_INK", &p_ink);
    sysvar_get_int(&rt->sysvars, "ATTR_P_PAPER", &p_paper);
    sysvar_get_int(&rt->sysvars, "ATTR_P_BRIGHT", &p_bright);
    sysvar_get_int(&rt->sysvars, "ATTR_P_INVERSE", &p_inv);

    if (p_inv == 1) {
        fprintf(rt->out, "\033[7m");
    } else {
        fprintf(rt->out, "\033[27m");
    }

    if (p_ink == ATTR_DEFAULT) {
        fprintf(rt->out, "\033[39m");
    } else if (p_ink >= 0 && p_ink <= 7) {
        int ink_code = (p_bright ? 90 : 30) + sinclair_to_ansi[p_ink & 7];
        fprintf(rt->out, "\033[%dm", ink_code);
    }

    if (p_paper == ATTR_DEFAULT) {
        fprintf(rt->out, "\033[49m");
    } else if (p_paper >= 0 && p_paper <= 7) {
        int paper_code = (p_bright ? 100 : 40) + sinclair_to_ansi[p_paper & 7];
        fprintf(rt->out, "\033[%dm", paper_code);
    }

    rt->term_attrs.ink = (int)p_ink;
    rt->term_attrs.paper = (int)p_paper;
    rt->term_attrs.bright = (int)p_bright;
    rt->term_attrs.inverse = (int)p_inv;
    rt->ink_explicitly_set = true;
    rt->paper_explicitly_set = true;
    fflush(rt->out);
}

bool console_set_permanent_ink(Runtime *rt, int64_t color, BasicError *err) {
    if (!rt) return false;
    if (color < -1 || color > 8) {
        if (err) err->code = ERR_INTEGER_RANGE;
        return false;
    }
    if (color == 8) {
        return true;
    }
    sysvar_set_int(&rt->sysvars, "ATTR_P_INK", color);
    rt->ink_explicitly_set = true;
    console_apply_delta(rt);
    return true;
}

bool console_set_temporary_ink(Runtime *rt, int64_t color, BasicError *err) {
    if (!rt) return false;
    if (color < -1 || color > 8) {
        if (err) err->code = ERR_INTEGER_RANGE;
        return false;
    }
    if (color == 8) {
        return true;
    }
    sysvar_set_int(&rt->sysvars, "ATTR_T_INK", color);
    console_apply_delta(rt);
    return true;
}

bool console_set_permanent_paper(Runtime *rt, int64_t color, BasicError *err) {
    if (!rt) return false;
    if (color < -1 || color > 8) {
        if (err) err->code = ERR_INTEGER_RANGE;
        return false;
    }
    if (color == 8) {
        return true;
    }
    sysvar_set_int(&rt->sysvars, "ATTR_P_PAPER", color);
    rt->paper_explicitly_set = true;
    console_apply_delta(rt);
    return true;
}

bool console_set_temporary_paper(Runtime *rt, int64_t color, BasicError *err) {
    if (!rt) return false;
    if (color < -1 || color > 8) {
        if (err) err->code = ERR_INTEGER_RANGE;
        return false;
    }
    if (color == 8) {
        return true;
    }
    sysvar_set_int(&rt->sysvars, "ATTR_T_PAPER", color);
    console_apply_delta(rt);
    return true;
}

bool console_set_permanent_bright(Runtime *rt, int64_t flag, BasicError *err) {
    if (!rt) return false;
    if (flag != -1 && flag != 0 && flag != 1 && flag != 8) {
        if (err) err->code = ERR_INTEGER_RANGE;
        return false;
    }
    if (flag == 8) {
        return true;
    }
    int64_t f = (flag == -1) ? 0 : flag;
    sysvar_set_int(&rt->sysvars, "ATTR_P_BRIGHT", f);
    console_apply_delta(rt);
    return true;
}

bool console_set_temporary_bright(Runtime *rt, int64_t flag, BasicError *err) {
    if (!rt) return false;
    if (flag != -1 && flag != 0 && flag != 1 && flag != 8) {
        if (err) err->code = ERR_INTEGER_RANGE;
        return false;
    }
    if (flag == 8) {
        return true;
    }
    int64_t f = (flag == -1) ? 0 : flag;
    sysvar_set_int(&rt->sysvars, "ATTR_T_BRIGHT", f);
    console_apply_delta(rt);
    return true;
}

bool console_set_permanent_inverse(Runtime *rt, int64_t flag, BasicError *err) {
    if (!rt) return false;
    if (flag != -1 && flag != 0 && flag != 1 && flag != 8) {
        if (err) err->code = ERR_INTEGER_RANGE;
        return false;
    }
    if (flag == 8) {
        return true;
    }
    int64_t f = (flag == -1) ? 0 : flag;
    sysvar_set_int(&rt->sysvars, "ATTR_P_INVERSE", f);
    console_apply_delta(rt);
    return true;
}

bool console_set_temporary_inverse(Runtime *rt, int64_t flag, BasicError *err) {
    if (!rt) return false;
    if (flag != -1 && flag != 0 && flag != 1 && flag != 8) {
        if (err) err->code = ERR_INTEGER_RANGE;
        return false;
    }
    if (flag == 8) {
        return true;
    }
    int64_t f = (flag == -1) ? 0 : flag;
    sysvar_set_int(&rt->sysvars, "ATTR_T_INVERSE", f);
    console_apply_delta(rt);
    return true;
}

bool console_has_temporary_attrs(const Runtime *rt) {
    if (!rt) return false;
    int64_t ti = ATTR_INACTIVE, tp = ATTR_INACTIVE, tb = ATTR_INACTIVE, tinv = ATTR_INACTIVE;
    sysvar_get_int(&rt->sysvars, "ATTR_T_INK", &ti);
    sysvar_get_int(&rt->sysvars, "ATTR_T_PAPER", &tp);
    sysvar_get_int(&rt->sysvars, "ATTR_T_BRIGHT", &tb);
    sysvar_get_int(&rt->sysvars, "ATTR_T_INVERSE", &tinv);
    return (ti != ATTR_INACTIVE || tp != ATTR_INACTIVE || tb != ATTR_INACTIVE || tinv != ATTR_INACTIVE);
}

void console_reset_temporary_attrs(Runtime *rt) {
    if (!rt) return;
    sysvar_set_int(&rt->sysvars, "ATTR_T_INK", ATTR_INACTIVE);
    sysvar_set_int(&rt->sysvars, "ATTR_T_PAPER", ATTR_INACTIVE);
    sysvar_set_int(&rt->sysvars, "ATTR_T_BRIGHT", ATTR_INACTIVE);
    sysvar_set_int(&rt->sysvars, "ATTR_T_INVERSE", ATTR_INACTIVE);
    console_apply_delta(rt);
}

bool console_cursor_at(Runtime *rt, int64_t row, int64_t col, BasicError *err) {
    if (!rt) return false;
    int64_t scr_rows = 24;
    int64_t scr_cols = 32;
    sysvar_get_int(&rt->sysvars, "SCR_ROWS", &scr_rows);
    sysvar_get_int(&rt->sysvars, "SCR_COLS", &scr_cols);

    if (row < 0 || row >= scr_rows || col < 0 || col >= scr_cols) {
        if (err) err->code = ERR_INTEGER_RANGE;
        return false;
    }

    if (rt->out) {
        fprintf(rt->out, "\033[%ld;%ldH", (long)(row + 1), (long)(col + 1));
        fflush(rt->out);
    }
    sysvar_set_int(&rt->sysvars, "S_POSN_ROW", row);
    sysvar_set_int(&rt->sysvars, "S_POSN_COL", col);
    rt->print_col = (int)col;
    return true;
}

void console_cls(Runtime *rt) {
    if (!rt || !rt->out) return;
    // 1. Emits terminal clear screen: \033[2J
    // 2. Moves cursor to top-left origin: \033[H
    fprintf(rt->out, "\033[2J\033[H");
    // 3. Re-applies current permanent attributes (ATTR_P_PAPER, ATTR_P_INK, etc.)
    console_reapply_permanent_attrs(rt);
    // 4. Resets S_POSN_ROW = 0 and S_POSN_COL = 0
    sysvar_set_int(&rt->sysvars, "S_POSN_ROW", 0);
    sysvar_set_int(&rt->sysvars, "S_POSN_COL", 0);
    rt->print_col = 0;
    fflush(rt->out);
}
