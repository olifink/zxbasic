#define _POSIX_C_SOURCE 200809L

#include "sysvars.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>

void sysvar_table_init(SysVarTable *table) {
    if (!table) return;
    memset(table, 0, sizeof(*table));

    // Register Core Console System Variables
    sysvar_register(table, "ATTR_P_INK",     SVAR_TYPE_INT, SVAR_FLAG_PERSISTENT);
    sysvar_set_int(table, "ATTR_P_INK", -1); // -1 = Terminal Default

    sysvar_register(table, "ATTR_P_PAPER",   SVAR_TYPE_INT, SVAR_FLAG_PERSISTENT);
    sysvar_set_int(table, "ATTR_P_PAPER", -1); // -1 = Terminal Default

    sysvar_register(table, "ATTR_P_BRIGHT",  SVAR_TYPE_INT, SVAR_FLAG_PERSISTENT);
    sysvar_set_int(table, "ATTR_P_BRIGHT", 0);

    sysvar_register(table, "ATTR_P_INVERSE", SVAR_TYPE_INT, SVAR_FLAG_PERSISTENT);
    sysvar_set_int(table, "ATTR_P_INVERSE", 0);

    sysvar_register(table, "ATTR_T_INK",     SVAR_TYPE_INT, SVAR_FLAG_NONE);
    sysvar_set_int(table, "ATTR_T_INK", -2); // -2 = Inactive

    sysvar_register(table, "ATTR_T_PAPER",   SVAR_TYPE_INT, SVAR_FLAG_NONE);
    sysvar_set_int(table, "ATTR_T_PAPER", -2); // -2 = Inactive

    sysvar_register(table, "ATTR_T_BRIGHT",  SVAR_TYPE_INT, SVAR_FLAG_NONE);
    sysvar_set_int(table, "ATTR_T_BRIGHT", -2); // -2 = Inactive

    sysvar_register(table, "ATTR_T_INVERSE", SVAR_TYPE_INT, SVAR_FLAG_NONE);
    sysvar_set_int(table, "ATTR_T_INVERSE", -2); // -2 = Inactive

    sysvar_register(table, "S_POSN_ROW",     SVAR_TYPE_INT, SVAR_FLAG_NONE);
    sysvar_set_int(table, "S_POSN_ROW", 0);

    sysvar_register(table, "S_POSN_COL",     SVAR_TYPE_INT, SVAR_FLAG_NONE);
    sysvar_set_int(table, "S_POSN_COL", 0);

    int rows = 24;
    int cols = 32;
#ifdef TIOCGWINSZ
    if (isatty(STDOUT_FILENO)) {
        struct winsize ws;
        if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0) {
            if (ws.ws_row > 0) rows = (int)ws.ws_row;
            if (ws.ws_col > 0) cols = (int)ws.ws_col;
        }
    }
#endif
    sysvar_register(table, "SCR_ROWS", SVAR_TYPE_INT, SVAR_FLAG_READ_ONLY);
    sysvar_set_int(table, "SCR_ROWS", rows);

    sysvar_register(table, "SCR_COLS", SVAR_TYPE_INT, SVAR_FLAG_READ_ONLY);
    sysvar_set_int(table, "SCR_COLS", cols);
}

void sysvar_table_free(SysVarTable *table) {
    if (!table) return;
    for (size_t i = 0; i < table->count; i++) {
        if (table->vars[i].type == SVAR_TYPE_STR && table->vars[i].val.s_val) {
            free(table->vars[i].val.s_val);
            table->vars[i].val.s_val = NULL;
        }
    }
    table->count = 0;
}

void sysvar_table_reset_defaults(SysVarTable *table) {
    if (!table) return;
    sysvar_set_int(table, "ATTR_P_INK", -1);
    sysvar_set_int(table, "ATTR_P_PAPER", -1);
    sysvar_set_int(table, "ATTR_P_BRIGHT", 0);
    sysvar_set_int(table, "ATTR_P_INVERSE", 0);
    sysvar_set_int(table, "ATTR_T_INK", -2);
    sysvar_set_int(table, "ATTR_T_PAPER", -2);
    sysvar_set_int(table, "ATTR_T_BRIGHT", -2);
    sysvar_set_int(table, "ATTR_T_INVERSE", -2);
    sysvar_set_int(table, "S_POSN_ROW", 0);
    sysvar_set_int(table, "S_POSN_COL", 0);
}

SysVar *sysvar_lookup(SysVarTable *table, const char *name) {
    if (!table || !name) return NULL;
    for (size_t i = 0; i < table->count; i++) {
        if (strcmp(table->vars[i].name, name) == 0) {
            return &table->vars[i];
        }
    }
    return NULL;
}

const SysVar *sysvar_lookup_const(const SysVarTable *table, const char *name) {
    if (!table || !name) return NULL;
    for (size_t i = 0; i < table->count; i++) {
        if (strcmp(table->vars[i].name, name) == 0) {
            return &table->vars[i];
        }
    }
    return NULL;
}

bool sysvar_register(SysVarTable *table, const char *name, SvarType type, uint8_t flags) {
    if (!table || !name) return false;
    SysVar *v = sysvar_lookup(table, name);
    if (v) {
        v->type = type;
        v->flags = flags;
        return true;
    }
    if (table->count >= MAX_SYSVARS) return false;

    v = &table->vars[table->count++];
    v->name = name;
    v->type = type;
    v->flags = flags;
    memset(&v->val, 0, sizeof(v->val));
    return true;
}

bool sysvar_get_int(const SysVarTable *table, const char *name, int64_t *out_val) {
    const SysVar *v = sysvar_lookup_const(table, name);
    if (!v) return false;
    if (v->type == SVAR_TYPE_INT) {
        if (out_val) *out_val = v->val.i_val;
        return true;
    }
    if (v->type == SVAR_TYPE_DOUBLE) {
        if (out_val) *out_val = (int64_t)v->val.d_val;
        return true;
    }
    return false;
}

bool sysvar_set_int(SysVarTable *table, const char *name, int64_t val) {
    SysVar *v = sysvar_lookup(table, name);
    if (!v) return false;
    if (v->type == SVAR_TYPE_INT) {
        v->val.i_val = val;
        return true;
    }
    if (v->type == SVAR_TYPE_DOUBLE) {
        v->val.d_val = (double)val;
        return true;
    }
    return false;
}

bool sysvar_get_double(const SysVarTable *table, const char *name, double *out_val) {
    const SysVar *v = sysvar_lookup_const(table, name);
    if (!v) return false;
    if (v->type == SVAR_TYPE_DOUBLE) {
        if (out_val) *out_val = v->val.d_val;
        return true;
    }
    if (v->type == SVAR_TYPE_INT) {
        if (out_val) *out_val = (double)v->val.i_val;
        return true;
    }
    return false;
}

bool sysvar_set_double(SysVarTable *table, const char *name, double val) {
    SysVar *v = sysvar_lookup(table, name);
    if (!v) return false;
    if (v->type == SVAR_TYPE_DOUBLE) {
        v->val.d_val = val;
        return true;
    }
    if (v->type == SVAR_TYPE_INT) {
        v->val.i_val = (int64_t)val;
        return true;
    }
    return false;
}

bool sysvar_get_str(const SysVarTable *table, const char *name, const char **out_val) {
    const SysVar *v = sysvar_lookup_const(table, name);
    if (!v || v->type != SVAR_TYPE_STR) return false;
    if (out_val) *out_val = v->val.s_val ? v->val.s_val : "";
    return true;
}

bool sysvar_set_str(SysVarTable *table, const char *name, const char *val) {
    SysVar *v = sysvar_lookup(table, name);
    if (!v || v->type != SVAR_TYPE_STR) return false;
    free(v->val.s_val);
    v->val.s_val = val ? strdup(val) : NULL;
    return true;
}
