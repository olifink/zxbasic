#ifndef ZXBASIC_SYSVARS_H
#define ZXBASIC_SYSVARS_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

typedef enum {
    SVAR_TYPE_INT,
    SVAR_TYPE_DOUBLE,
    SVAR_TYPE_STR
} SvarType;

#define SVAR_FLAG_NONE       0
#define SVAR_FLAG_READ_ONLY  (1 << 0)
#define SVAR_FLAG_PERSISTENT (1 << 1)

typedef struct {
    const char *name;
    SvarType type;
    union {
        int64_t i_val;
        double  d_val;
        char   *s_val;
    } val;
    uint8_t flags; // e.g., READ_ONLY, PERSISTENT
} SysVar;

#define MAX_SYSVARS 64

typedef struct SysVarTable {
    SysVar vars[MAX_SYSVARS];
    size_t count;
} SysVarTable;

void sysvar_table_init(SysVarTable *table);
void sysvar_table_free(SysVarTable *table);
void sysvar_table_reset_defaults(SysVarTable *table);

SysVar *sysvar_lookup(SysVarTable *table, const char *name);
const SysVar *sysvar_lookup_const(const SysVarTable *table, const char *name);

bool sysvar_register(SysVarTable *table, const char *name, SvarType type, uint8_t flags);

bool sysvar_get_int(const SysVarTable *table, const char *name, int64_t *out_val);
bool sysvar_set_int(SysVarTable *table, const char *name, int64_t val);

bool sysvar_get_double(const SysVarTable *table, const char *name, double *out_val);
bool sysvar_set_double(SysVarTable *table, const char *name, double val);

bool sysvar_get_str(const SysVarTable *table, const char *name, const char **out_val);
bool sysvar_set_str(SysVarTable *table, const char *name, const char *val);

#endif // ZXBASIC_SYSVARS_H
