#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "../src/runtime.h"

static void test_program_execution(void) {
    Runtime rt;
    runtime_init(&rt);

    // Redirect output to temporary file / buffer
    FILE *out = tmpfile();
    assert(out != NULL);
    rt.out = out;

    // Load simple program
    runtime_process_input(&rt, "10 LET sum = 0", false);
    runtime_process_input(&rt, "20 FOR i = 1 TO 5", false);
    runtime_process_input(&rt, "30 LET sum = sum + i", false);
    runtime_process_input(&rt, "40 NEXT i", false);
    runtime_process_input(&rt, "50 PRINT \"SUM=\"; sum", false);

    assert(rt.program.count == 5);

    runtime_run(&rt, 0);
    if (rt.last_error.code != ERR_OK) {
        char errbuf[128];
        error_format(&rt.last_error, errbuf, sizeof(errbuf));
        fprintf(stderr, "RUN FAILED WITH: %s\n", errbuf);
    }
    assert(rt.last_error.code == ERR_OK);

    // Check variable state
    double sum = 0.0;
    assert(symtab_get_num(&rt.symtab, "sum", &sum));
    assert(sum == 15.0);

    // Check printed output
    rewind(out);
    char buf[256];
    assert(fgets(buf, sizeof(buf), out) != NULL);
    assert(strstr(buf, "SUM=15") != NULL);

    fclose(out);
    runtime_free(&rt);
    printf("test_program_execution (FOR loop): PASS\n");
}

static void test_subroutines(void) {
    Runtime rt;
    runtime_init(&rt);
    FILE *out = tmpfile();
    rt.out = out;

    runtime_process_input(&rt, "10 LET x = 5", false);
    runtime_process_input(&rt, "20 GOSUB 100", false);
    runtime_process_input(&rt, "30 PRINT \"FINAL=\"; x : STOP", false);
    runtime_process_input(&rt, "100 LET x = x * 2", false);
    runtime_process_input(&rt, "110 RETURN", false);

    runtime_run(&rt, 0);
    assert(rt.last_error.code == ERR_STOP);

    double x = 0.0;
    assert(symtab_get_num(&rt.symtab, "x", &x));
    assert(x == 10.0);

    fclose(out);
    runtime_free(&rt);
    printf("test_subroutines (GOSUB / RETURN): PASS\n");
}

static void test_if_then(void) {
    Runtime rt;
    runtime_init(&rt);
    FILE *out = tmpfile();
    rt.out = out;

    runtime_process_input(&rt, "10 LET a = 0", false);
    runtime_process_input(&rt, "20 LET b = 0", false);
    runtime_process_input(&rt, "30 IF 1 = 1 THEN LET a = 42 : LET b = 99", false);
    runtime_process_input(&rt, "40 IF 1 = 2 THEN LET a = 0 : LET b = 0", false);

    runtime_run(&rt, 0);
    if (rt.last_error.code != ERR_OK) {
        char errbuf[128];
        error_format(&rt.last_error, errbuf, sizeof(errbuf));
        fprintf(stderr, "test_if_then FAILED WITH: %s\n", errbuf);
    }
    assert(rt.last_error.code == ERR_OK);

    double a = 0.0, b = 0.0;
    assert(symtab_get_num(&rt.symtab, "a", &a));
    assert(symtab_get_num(&rt.symtab, "b", &b));
    assert(a == 42.0);
    assert(b == 99.0);

    fclose(out);
    runtime_free(&rt);
    printf("test_if_then: PASS\n");
}

static void test_data_read(void) {
    Runtime rt;
    runtime_init(&rt);
    FILE *out = tmpfile();
    rt.out = out;

    runtime_process_input(&rt, "10 DATA 10, 20, \"SPECTRUM\", 30", false);
    runtime_process_input(&rt, "20 READ a, b, s$, c", false);

    runtime_run(&rt, 0);
    assert(rt.last_error.code == ERR_OK);

    double a = 0, b = 0, c = 0;
    const char *s = NULL;
    size_t slen = 0;

    assert(symtab_get_num(&rt.symtab, "a", &a) && a == 10.0);
    assert(symtab_get_num(&rt.symtab, "b", &b) && b == 20.0);
    assert(symtab_get_num(&rt.symtab, "c", &c) && c == 30.0);
    assert(symtab_get_str(&rt.symtab, "s$", &s, &slen) && strcmp(s, "SPECTRUM") == 0);

    // End of data test
    runtime_process_input(&rt, "30 READ extra", false);
    runtime_run(&rt, 0);
    assert(rt.last_error.code == ERR_END_OF_DATA);

    fclose(out);
    runtime_free(&rt);
    printf("test_data_read: PASS\n");
}

static void test_save_and_load(void) {
    Runtime rt;
    runtime_init(&rt);
    FILE *out = tmpfile();
    rt.out = out;

    runtime_process_input(&rt, "10 REM Test Program", false);
    runtime_process_input(&rt, "20 LET z = 123", false);
    runtime_process_input(&rt, "30 PRINT z", false);

    const char *test_path = "/tmp/test_zxbasic_save.bas";
    assert(runtime_save(&rt, test_path));

    Runtime rt2;
    runtime_init(&rt2);
    rt2.out = out;
    assert(runtime_load(&rt2, test_path));
    assert(rt2.program.count == 3);
    assert(rt2.program.lines[0].line_no == 10);
    assert(rt2.program.lines[1].line_no == 20);
    assert(rt2.program.lines[2].line_no == 30);

    runtime_run(&rt2, 0);
    assert(rt2.last_error.code == ERR_OK);

    double z = 0.0;
    assert(symtab_get_num(&rt2.symtab, "z", &z) && z == 123.0);

    remove(test_path);
    fclose(out);
    runtime_free(&rt);
    runtime_free(&rt2);
    printf("test_save_and_load: PASS\n");
}

static void test_cls(void) {
    Runtime rt;
    runtime_init(&rt);
    FILE *out = tmpfile();
    rt.out = out;

    runtime_process_input(&rt, "10 PRINT \"BEFORE\";", false);
    runtime_process_input(&rt, "20 CLS", false);
    runtime_process_input(&rt, "30 PRINT \"AFTER\";", false);

    runtime_run(&rt, 0);
    assert(rt.last_error.code == ERR_OK);

    rewind(out);
    char buf[256];
    size_t n = fread(buf, 1, sizeof(buf) - 1, out);
    buf[n] = '\0';

    assert(strstr(buf, "\033[2J\033[H") != NULL);
    assert(strstr(buf, "BEFORE") != NULL);
    assert(strstr(buf, "AFTER") != NULL);

    fclose(out);
    runtime_free(&rt);
    printf("test_cls: PASS\n");
}

static void test_print_comma_tabs(void) {
    Runtime rt;
    runtime_init(&rt);
    FILE *out = tmpfile();
    rt.out = out;

    runtime_process_input(&rt, "10 FOR i = 0 TO 10", false);
    runtime_process_input(&rt, "20 PRINT i,", false);
    runtime_process_input(&rt, "30 NEXT i", false);

    runtime_run(&rt, 0);
    assert(rt.last_error.code == ERR_OK);

    rewind(out);
    char buf[512];
    size_t n = fread(buf, 1, sizeof(buf) - 1, out);
    buf[n] = '\0';

    assert(strstr(buf, "0") != NULL);
    assert(strstr(buf, "10") != NULL);

    fclose(out);
    runtime_free(&rt);
    printf("test_print_comma_tabs: PASS\n");
}

int main(void) {
    test_program_execution();
    test_subroutines();
    test_if_then();
    test_data_read();
    test_save_and_load();
    test_cls();
    test_print_comma_tabs();
    return 0;
}
