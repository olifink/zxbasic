#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include "../src/runtime.h"
#include "../src/normalize.h"
#include "../src/sysvars.h"
#include "../src/console.h"

static void test_sysvars_subsystem(void) {
    SysVarTable table;
    sysvar_table_init(&table);

    int64_t val = 0;
    assert(sysvar_get_int(&table, "ATTR_P_INK", &val) && val == -1);
    assert(sysvar_get_int(&table, "ATTR_P_PAPER", &val) && val == -1);
    assert(sysvar_get_int(&table, "ATTR_P_BRIGHT", &val) && val == 0);
    assert(sysvar_get_int(&table, "ATTR_P_INVERSE", &val) && val == 0);

    assert(sysvar_get_int(&table, "ATTR_T_INK", &val) && val == -2);
    assert(sysvar_get_int(&table, "ATTR_T_PAPER", &val) && val == -2);
    assert(sysvar_get_int(&table, "ATTR_T_BRIGHT", &val) && val == -2);
    assert(sysvar_get_int(&table, "ATTR_T_INVERSE", &val) && val == -2);

    assert(sysvar_get_int(&table, "S_POSN_ROW", &val) && val == 0);
    assert(sysvar_get_int(&table, "S_POSN_COL", &val) && val == 0);
    assert(sysvar_get_int(&table, "SCR_ROWS", &val) && val > 0);
    assert(sysvar_get_int(&table, "SCR_COLS", &val) && val > 0);

    const SysVar *v = sysvar_lookup_const(&table, "SCR_ROWS");
    assert(v != NULL && (v->flags & SVAR_FLAG_READ_ONLY));

    // Test get and set int/double/string
    assert(sysvar_set_int(&table, "ATTR_P_INK", 2));
    assert(sysvar_get_int(&table, "ATTR_P_INK", &val) && val == 2);

    assert(sysvar_register(&table, "CUSTOM_DOUBLE", SVAR_TYPE_DOUBLE, SVAR_FLAG_NONE));
    assert(sysvar_set_double(&table, "CUSTOM_DOUBLE", 3.14159));
    double dval = 0.0;
    assert(sysvar_get_double(&table, "CUSTOM_DOUBLE", &dval) && dval > 3.14);

    assert(sysvar_register(&table, "CUSTOM_STR", SVAR_TYPE_STR, SVAR_FLAG_NONE));
    assert(sysvar_set_str(&table, "CUSTOM_STR", "ZX-Spectrum"));
    const char *sval = NULL;
    assert(sysvar_get_str(&table, "CUSTOM_STR", &sval) && strcmp(sval, "ZX-Spectrum") == 0);

    sysvar_table_free(&table);
    printf("test_sysvars_subsystem: PASS\n");
}

static void test_normalization_and_syntax(void) {
    // Normalization / auto-capitalization
    char *res1 = normalize_keywords("ink 2: paper 7: bright 1: inverse 0");
    assert(strcmp(res1, "INK 2: PAPER 7: BRIGHT 1: INVERSE 0") == 0);
    free(res1);

    char *res2 = normalize_keywords("print at 10, 5; ink 4; \"Green\"; ink 2; \"Red\"");
    assert(strcmp(res2, "PRINT AT 10, 5; INK 4; \"Green\"; INK 2; \"Red\"") == 0);
    free(res2);

    char *res3 = normalize_keywords("input at 5, 2; ink 1; \"Prompt\"; x$");
    assert(strcmp(res3, "INPUT AT 5, 2; INK 1; \"Prompt\"; x$") == 0);
    free(res3);

    // Syntax validation
    size_t err_col = 0;
    assert(syntax_validate_line("INK 2", &err_col));
    assert(syntax_validate_line("PAPER 7", &err_col));
    assert(syntax_validate_line("BRIGHT 1", &err_col));
    assert(syntax_validate_line("INVERSE 0", &err_col));
    assert(syntax_validate_line("PRINT AT 10, 5; INK 2; \"Red\"", &err_col));
    assert(syntax_validate_line("INPUT AT 10, 5; INK 2; \"Prompt\"; x", &err_col));
    assert(syntax_validate_line("CLS", &err_col));

    // Standalone AT is illegal
    assert(!syntax_validate_line("AT 10, 5", &err_col));

    // Missing comma in AT
    assert(!syntax_validate_line("PRINT AT 10 5; \"Err\"", &err_col));

    printf("test_normalization_and_syntax: PASS\n");
}

static void test_standalone_attributes(void) {
    Runtime rt;
    runtime_init(&rt);
    FILE *out = tmpfile();
    assert(out != NULL);
    rt.out = out;

    // Default: ATTR_P_INK = -1 (Default), ATTR_P_PAPER = -1 (Default), BRIGHT = 0, INVERSE = 0
    int64_t val = 0;
    assert(sysvar_get_int(&rt.sysvars, "ATTR_P_INK", &val) && val == -1);
    assert(sysvar_get_int(&rt.sysvars, "ATTR_P_PAPER", &val) && val == -1);

    // INK 2 (Red -> ANSI fg 31)
    runtime_process_input(&rt, "INK 2", true);
    assert(rt.last_error.code == ERR_OK);
    assert(sysvar_get_int(&rt.sysvars, "ATTR_P_INK", &val) && val == 2);

    rewind(out);
    char buf[512];
    size_t n = fread(buf, 1, sizeof(buf) - 1, out);
    buf[n] = '\0';
    assert(strstr(buf, "\033[31m") != NULL);

    // PAPER 4 (Green -> ANSI bg 42)
    rewind(out);
    ftruncate(fileno(out), 0);
    runtime_process_input(&rt, "PAPER 4", true);
    assert(rt.last_error.code == ERR_OK);
    assert(sysvar_get_int(&rt.sysvars, "ATTR_P_PAPER", &val) && val == 4);

    rewind(out);
    n = fread(buf, 1, sizeof(buf) - 1, out);
    buf[n] = '\0';
    assert(strstr(buf, "\033[42m") != NULL);

    // BRIGHT 1 (High intensity: fg becomes 91, bg becomes 102)
    rewind(out);
    ftruncate(fileno(out), 0);
    runtime_process_input(&rt, "BRIGHT 1", true);
    assert(rt.last_error.code == ERR_OK);
    assert(sysvar_get_int(&rt.sysvars, "ATTR_P_BRIGHT", &val) && val == 1);

    rewind(out);
    n = fread(buf, 1, sizeof(buf) - 1, out);
    buf[n] = '\0';
    assert(strstr(buf, "\033[91m") != NULL);
    assert(strstr(buf, "\033[102m") != NULL);

    // INVERSE 1 (\033[7m)
    rewind(out);
    ftruncate(fileno(out), 0);
    runtime_process_input(&rt, "INVERSE 1", true);
    assert(rt.last_error.code == ERR_OK);
    assert(sysvar_get_int(&rt.sysvars, "ATTR_P_INVERSE", &val) && val == 1);

    rewind(out);
    n = fread(buf, 1, sizeof(buf) - 1, out);
    buf[n] = '\0';
    assert(strstr(buf, "\033[7m") != NULL);

    // INVERSE 0 (\033[27m)
    rewind(out);
    ftruncate(fileno(out), 0);
    runtime_process_input(&rt, "INVERSE 0", true);
    assert(rt.last_error.code == ERR_OK);
    assert(sysvar_get_int(&rt.sysvars, "ATTR_P_INVERSE", &val) && val == 0);

    rewind(out);
    n = fread(buf, 1, sizeof(buf) - 1, out);
    buf[n] = '\0';
    assert(strstr(buf, "\033[27m") != NULL);

    // 8 is transparent / keep current (no change)
    runtime_process_input(&rt, "INK 8", true);
    assert(rt.last_error.code == ERR_OK);
    assert(sysvar_get_int(&rt.sysvars, "ATTR_P_INK", &val) && val == 2);

    runtime_process_input(&rt, "PAPER 8", true);
    assert(rt.last_error.code == ERR_OK);
    assert(sysvar_get_int(&rt.sysvars, "ATTR_P_PAPER", &val) && val == 4);

    // -1 resets to terminal defaults: INK -1 (\033[39m) and PAPER -1 (\033[49m)
    rewind(out);
    ftruncate(fileno(out), 0);
    runtime_process_input(&rt, "INK -1", true);
    assert(rt.last_error.code == ERR_OK);
    assert(sysvar_get_int(&rt.sysvars, "ATTR_P_INK", &val) && val == -1);

    rewind(out);
    n = fread(buf, 1, sizeof(buf) - 1, out);
    buf[n] = '\0';
    assert(strstr(buf, "\033[39m") != NULL);

    rewind(out);
    ftruncate(fileno(out), 0);
    runtime_process_input(&rt, "PAPER -1", true);
    assert(rt.last_error.code == ERR_OK);
    assert(sysvar_get_int(&rt.sysvars, "ATTR_P_PAPER", &val) && val == -1);

    rewind(out);
    n = fread(buf, 1, sizeof(buf) - 1, out);
    buf[n] = '\0';
    assert(strstr(buf, "\033[49m") != NULL);

    runtime_process_input(&rt, "BRIGHT -1", true);
    assert(rt.last_error.code == ERR_OK);
    assert(sysvar_get_int(&rt.sysvars, "ATTR_P_BRIGHT", &val) && val == 0);

    runtime_process_input(&rt, "INVERSE -1", true);
    assert(rt.last_error.code == ERR_OK);
    assert(sysvar_get_int(&rt.sysvars, "ATTR_P_INVERSE", &val) && val == 0);

    // Out of range errors
    runtime_process_input(&rt, "INK 9", true);
    assert(rt.last_error.code == ERR_INTEGER_RANGE);

    runtime_process_input(&rt, "INK -2", true);
    assert(rt.last_error.code == ERR_INTEGER_RANGE);

    runtime_process_input(&rt, "PAPER 9", true);
    assert(rt.last_error.code == ERR_INTEGER_RANGE);

    runtime_process_input(&rt, "PAPER -2", true);
    assert(rt.last_error.code == ERR_INTEGER_RANGE);

    runtime_process_input(&rt, "BRIGHT 2", true);
    assert(rt.last_error.code == ERR_INTEGER_RANGE);

    runtime_process_input(&rt, "BRIGHT -2", true);
    assert(rt.last_error.code == ERR_INTEGER_RANGE);

    runtime_process_input(&rt, "INVERSE 2", true);
    assert(rt.last_error.code == ERR_INTEGER_RANGE);

    runtime_process_input(&rt, "INVERSE -2", true);
    assert(rt.last_error.code == ERR_INTEGER_RANGE);

    fclose(out);
    runtime_free(&rt);
    printf("test_standalone_attributes: PASS\n");
}

static void test_inline_temporary_attributes(void) {
    Runtime rt;
    runtime_init(&rt);
    FILE *out = tmpfile();
    rt.out = out;

    // Set permanent INK to 1 (Blue -> ANSI fg 34)
    runtime_process_input(&rt, "INK 1", true);
    assert(rt.last_error.code == ERR_OK);

    // Inline modifiers: PRINT INK 2; "Red"; INK 7; "White"
    // At conclusion of PRINT, should restore permanent INK 1 (\033[34m)
    rewind(out);
    ftruncate(fileno(out), 0);
    runtime_process_input(&rt, "PRINT INK 2; \"Red\"; INK 7; \"White\"", true);
    assert(rt.last_error.code == ERR_OK);

    rewind(out);
    char buf[512];
    size_t n = fread(buf, 1, sizeof(buf) - 1, out);
    buf[n] = '\0';

    // Must have INK 2 (\033[31m), Red, INK 7 (\033[37m), White, and restored INK 1 (\033[34m)
    char *p_red_code = strstr(buf, "\033[31m");
    assert(p_red_code != NULL);
    char *p_red_text = strstr(buf, "Red");
    assert(p_red_text != NULL && p_red_text > p_red_code);

    char *p_white_code = strstr(buf, "\033[37m");
    assert(p_white_code != NULL && p_white_code > p_red_text);
    char *p_white_text = strstr(buf, "White");
    assert(p_white_text != NULL && p_white_text > p_white_code);

    char *p_restore_blue = strstr(p_white_text, "\033[34m");
    assert(p_restore_blue != NULL);

    // Verify sysvars temporary attributes were reset to -2 (ATTR_INACTIVE)
    int64_t t_ink = 0;
    assert(sysvar_get_int(&rt.sysvars, "ATTR_T_INK", &t_ink) && t_ink == -2);

    // Inline modifier resetting to terminal default: PRINT INK 2; "Red"; INK -1; "Default"
    rewind(out);
    ftruncate(fileno(out), 0);
    runtime_process_input(&rt, "PRINT INK 2; \"Red\"; INK -1; \"Default\"", true);
    assert(rt.last_error.code == ERR_OK);

    rewind(out);
    n = fread(buf, 1, sizeof(buf) - 1, out);
    buf[n] = '\0';

    p_red_code = strstr(buf, "\033[31m");
    assert(p_red_code != NULL);
    p_red_text = strstr(buf, "Red");
    assert(p_red_text != NULL && p_red_text > p_red_code);

    char *p_def_code = strstr(buf, "\033[39m");
    assert(p_def_code != NULL && p_def_code > p_red_text);
    char *p_def_text = strstr(buf, "Default");
    assert(p_def_text != NULL && p_def_text > p_def_code);

    p_restore_blue = strstr(p_def_text, "\033[34m");
    assert(p_restore_blue != NULL);

    assert(sysvar_get_int(&rt.sysvars, "ATTR_T_INK", &t_ink) && t_ink == -2);

    fclose(out);
    runtime_free(&rt);
    printf("test_inline_temporary_attributes: PASS\n");
}

static void test_cursor_positioning_at(void) {
    Runtime rt;
    runtime_init(&rt);
    FILE *out = tmpfile();
    rt.out = out;

    // Ensure known SCR dimensions: 24 rows, 32 cols
    sysvar_set_int(&rt.sysvars, "SCR_ROWS", 24);
    sysvar_set_int(&rt.sysvars, "SCR_COLS", 32);

    // 20 PRINT AT 10, 5; INK 4; "Green Text"; INK 2; "Red Text"
    runtime_process_input(&rt, "PRINT AT 10, 5; INK 4; \"Green Text\"; INK 2; \"Red Text\"", true);
    assert(rt.last_error.code == ERR_OK);

    rewind(out);
    char buf[512];
    size_t n = fread(buf, 1, sizeof(buf) - 1, out);
    buf[n] = '\0';

    // AT 10, 5 emits ANSI \033[11;6H
    assert(strstr(buf, "\033[11;6H") != NULL);
    assert(strstr(buf, "\033[32m") != NULL);
    assert(strstr(buf, "Green Text") != NULL);
    assert(strstr(buf, "\033[31m") != NULL);
    assert(strstr(buf, "Red Text") != NULL);

    // S_POSN coordinates after newline
    int64_t row = 0, col = 0;
    sysvar_get_int(&rt.sysvars, "S_POSN_ROW", &row);
    sysvar_get_int(&rt.sysvars, "S_POSN_COL", &col);
    assert(row == 11);
    assert(col == 0);

    // Out-of-bounds checks
    runtime_process_input(&rt, "PRINT AT -1, 5; \"Bad\"", true);
    assert(rt.last_error.code == ERR_INTEGER_RANGE);

    runtime_process_input(&rt, "PRINT AT 24, 0; \"Bad\"", true);
    assert(rt.last_error.code == ERR_INTEGER_RANGE);

    runtime_process_input(&rt, "PRINT AT 0, 32; \"Bad\"", true);
    assert(rt.last_error.code == ERR_INTEGER_RANGE);

    fclose(out);
    runtime_free(&rt);
    printf("test_cursor_positioning_at: PASS\n");
}

static void test_screen_clearing_cls(void) {
    Runtime rt;
    runtime_init(&rt);
    FILE *out = tmpfile();
    rt.out = out;

    // Set permanent INK 2 (Red) and PAPER 1 (Blue)
    runtime_process_input(&rt, "INK 2: PAPER 1", true);
    assert(rt.last_error.code == ERR_OK);

    rewind(out);
    ftruncate(fileno(out), 0);
    runtime_process_input(&rt, "CLS", true);
    assert(rt.last_error.code == ERR_OK);

    rewind(out);
    char buf[512];
    size_t n = fread(buf, 1, sizeof(buf) - 1, out);
    buf[n] = '\0';

    // Must emit \033[2J\033[H and re-apply permanent attributes
    assert(strstr(buf, "\033[2J\033[H") != NULL);
    assert(strstr(buf, "\033[31m") != NULL); // Red fg
    assert(strstr(buf, "\033[44m") != NULL); // Blue bg

    int64_t row = -1, col = -1;
    sysvar_get_int(&rt.sysvars, "S_POSN_ROW", &row);
    sysvar_get_int(&rt.sysvars, "S_POSN_COL", &col);
    assert(row == 0);
    assert(col == 0);

    fclose(out);
    runtime_free(&rt);

    // Also test CLS with default attributes (-1)
    Runtime rt_def;
    runtime_init(&rt_def);
    FILE *out_def = tmpfile();
    rt_def.out = out_def;

    runtime_process_input(&rt_def, "CLS", true);
    assert(rt_def.last_error.code == ERR_OK);

    rewind(out_def);
    n = fread(buf, 1, sizeof(buf) - 1, out_def);
    buf[n] = '\0';
    assert(strstr(buf, "\033[2J\033[H") != NULL);
    assert(strstr(buf, "\033[39m") != NULL); // Default fg
    assert(strstr(buf, "\033[49m") != NULL); // Default bg

    fclose(out_def);
    runtime_free(&rt_def);
    printf("test_screen_clearing_cls: PASS\n");
}

static void test_input_stream_modifiers(void) {
    Runtime rt;
    runtime_init(&rt);
    FILE *out = tmpfile();
    FILE *in = tmpfile();
    rt.out = out;
    rt.in = in;

    fputs("Antigravity\n", in);
    rewind(in);

    // Set permanent INK 7
    runtime_process_input(&rt, "INK 7", true);

    rewind(out);
    ftruncate(fileno(out), 0);
    runtime_process_input(&rt, "INPUT AT 5, 2; INK 3; \"Your Name: \"; name$", true);
    assert(rt.last_error.code == ERR_OK);

    rewind(out);
    char buf[512];
    size_t n = fread(buf, 1, sizeof(buf) - 1, out);
    buf[n] = '\0';

    // Cursor at line 6, col 3
    assert(strstr(buf, "\033[6;3H") != NULL);
    // Magenta prompt
    assert(strstr(buf, "\033[35m") != NULL);
    assert(strstr(buf, "Your Name: ") != NULL);
    // Restored white fg at conclusion
    assert(strstr(buf, "\033[37m") != NULL);

    const char *str = NULL;
    size_t len = 0;
    assert(symtab_get_str(&rt.symtab, "name$", &str, &len));
    assert(strcmp(str, "Antigravity") == 0);

    fclose(out);
    fclose(in);
    runtime_free(&rt);
    printf("test_input_stream_modifiers: PASS\n");
}

static void test_sysvars_in_basic_expressions(void) {
    Runtime rt;
    runtime_init(&rt);
    FILE *out = tmpfile();
    rt.out = out;

    sysvar_set_int(&rt.sysvars, "SCR_ROWS", 24);
    sysvar_set_int(&rt.sysvars, "SCR_COLS", 32);

    runtime_process_input(&rt, "PRINT SCR_ROWS; \",\"; SCR_COLS", true);
    assert(rt.last_error.code == ERR_OK);

    rewind(out);
    char buf[256];
    assert(fgets(buf, sizeof(buf), out) != NULL);
    assert(strstr(buf, "24,32") != NULL);

    // Read-only sysvars cannot be modified via LET
    runtime_process_input(&rt, "LET SCR_ROWS = 50", true);
    assert(rt.last_error.code == ERR_NONSENSE);

    // Modifiable sysvar ATTR_P_INK
    runtime_process_input(&rt, "LET ATTR_P_INK = 4", true);
    assert(rt.last_error.code == ERR_OK);
    int64_t val = 0;
    assert(sysvar_get_int(&rt.sysvars, "ATTR_P_INK", &val) && val == 4);

    fclose(out);
    runtime_free(&rt);
    printf("test_sysvars_in_basic_expressions: PASS\n");
}

int main(void) {
    test_sysvars_subsystem();
    test_normalization_and_syntax();
    test_standalone_attributes();
    test_inline_temporary_attributes();
    test_cursor_positioning_at();
    test_screen_clearing_cls();
    test_input_stream_modifiers();
    test_sysvars_in_basic_expressions();
    printf("All color tests passed successfully!\n");
    return 0;
}
