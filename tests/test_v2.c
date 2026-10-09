#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <sys/time.h>
#include <signal.h>
#include "../src/runtime.h"
#include "../src/normalize.h"

static void test_auto_capitalization(void) {
    // Example from SPEC 1.1:
    // Input: 10 for i=1 to 10: print "count: "; i: next i
    // Stored: 10 FOR i=1 TO 10: PRINT "count: "; i: NEXT i
    char *res1 = normalize_keywords("for i=1 to 10: print \"count: \"; i: next i");
    assert(strcmp(res1, "FOR i=1 TO 10: PRINT \"count: \"; i: NEXT i") == 0);
    free(res1);

    // String literals retain exact casing
    char *res2 = normalize_keywords("print \"Hello World: for i=1 to 5\"");
    assert(strcmp(res2, "PRINT \"Hello World: for i=1 to 5\"") == 0);
    free(res2);

    // REM comments retain exact casing
    char *res3 = normalize_keywords("rem This is a comment with for i=1 to 10");
    assert(strcmp(res3, "REM This is a comment with for i=1 to 10") == 0);
    free(res3);

    // Variable identifiers retain casing
    char *res4 = normalize_keywords("let myVar = val(\"123\") + len(a$)");
    assert(strcmp(res4, "LET myVar = VAL(\"123\") + LEN(a$)") == 0);
    free(res4);

    printf("test_auto_capitalization: PASS\n");
}

static void test_syntax_validation(void) {
    size_t err_col = 0;

    // Valid lines
    assert(syntax_validate_line("LET x = 1", &err_col));
    assert(syntax_validate_line("FOR i = 1 TO 10 STEP 2: NEXT i", &err_col));
    assert(syntax_validate_line("IF x = 1 THEN 100", &err_col));
    assert(syntax_validate_line("IF x = 1 THEN GOTO 100", &err_col));
    assert(syntax_validate_line("PRINT \"A\"; B, C' D", &err_col));
    assert(syntax_validate_line("CLS", &err_col));

    // Invalid lines
    assert(!syntax_validate_line("FOR i = 1 10", &err_col)); // missing TO
    assert(!syntax_validate_line("IF x = 1 GOTO 100", &err_col)); // missing THEN
    assert(!syntax_validate_line("LET x = (1 + 2", &err_col)); // unclosed paren
    assert(!syntax_validate_line("PRINT \"unclosed", &err_col)); // unclosed quote
    assert(!syntax_validate_line("EDIT 10", &err_col)); // immediate-only command forbidden in stored line
    assert(!syntax_validate_line("AUTO 10, 10", &err_col));
    assert(!syntax_validate_line("RENUM", &err_col));

    printf("test_syntax_validation: PASS\n");
}

static void test_ingestion_gating(void) {
    Runtime rt;
    runtime_init(&rt);
    FILE *out = tmpfile();
    rt.out = out;

    // Initial valid line
    runtime_process_input(&rt, "10 PRINT \"INITIAL\"", false);
    assert(rt.program.count == 1);
    assert(strcmp(rt.program.lines[0].source, "PRINT \"INITIAL\"") == 0);

    // Ingest invalid line with same line number
    runtime_process_input(&rt, "10 FOR i = 1 10", false);

    // Line 10 must NOT have been overwritten or deleted
    assert(rt.program.count == 1);
    assert(strcmp(rt.program.lines[0].source, "PRINT \"INITIAL\"") == 0);

    // Output should contain report: C Nonsense in BASIC, 10:<column>
    rewind(out);
    char buf[256];
    assert(fgets(buf, sizeof(buf), out) != NULL);
    assert(strstr(buf, "C Nonsense in BASIC, 10:") != NULL);

    fclose(out);
    runtime_free(&rt);
    printf("test_ingestion_gating: PASS\n");
}

static void test_renum(void) {
    Runtime rt;
    runtime_init(&rt);
    FILE *out = tmpfile();
    rt.out = out;

    runtime_process_input(&rt, "5 PRINT \"START\"", false);
    runtime_process_input(&rt, "15 GOTO 5", false);
    runtime_process_input(&rt, "25 GOSUB 50", false);
    runtime_process_input(&rt, "50 RETURN", false);

    // Execute RENUM (defaults start=10, step=10)
    runtime_process_input(&rt, "RENUM", false);

    assert(rt.program.count == 4);
    assert(rt.program.lines[0].line_no == 10);
    assert(strcmp(rt.program.lines[0].source, "PRINT \"START\"") == 0);

    assert(rt.program.lines[1].line_no == 20);
    assert(strcmp(rt.program.lines[1].source, "GOTO 10") == 0);

    assert(rt.program.lines[2].line_no == 30);
    assert(strcmp(rt.program.lines[2].source, "GOSUB 40") == 0);

    assert(rt.program.lines[3].line_no == 40);
    assert(strcmp(rt.program.lines[3].source, "RETURN") == 0);

    // Test warning on unmapped target
    runtime_process_input(&rt, "50 GOTO 999", false);
    runtime_process_input(&rt, "RENUM 100, 10", false);
    rewind(out);
    char buf[1024];
    size_t n = fread(buf, 1, sizeof(buf) - 1, out);
    buf[n] = '\0';
    assert(strstr(buf, "Warning: Line reference 999 not found at line 50") != NULL);

    // Test overflow > 9999 aborts with memory unmodified
    BasicError err = { .code = ERR_OK, .custom_msg = NULL, .line_no = -1, .stmt_index = 0 };
    assert(!program_renumber(&rt.program, 9950, 50, &err, out)); // 9950 + 4*50 = 10150 > 9999
    assert(err.code == ERR_INTEGER_RANGE);

    fclose(out);
    runtime_free(&rt);
    printf("test_renum: PASS\n");
}

static void test_edit_and_auto(void) {
    Runtime rt;
    runtime_init(&rt);
    FILE *out = tmpfile();
    rt.out = out;

    runtime_process_input(&rt, "10 PRINT \"EDIT TEST\"", false);

    // EDIT missing line
    runtime_process_input(&rt, "EDIT 99", false);
    assert(rt.last_error.code == ERR_INTEGER_RANGE);

    // EDIT existing line
    runtime_process_input(&rt, "EDIT 10", false);
    assert(rt.edit_prefill_buffer != NULL);
    assert(strcmp(rt.edit_prefill_buffer, "10 PRINT \"EDIT TEST\"") == 0);

    // AUTO command
    runtime_process_input(&rt, "AUTO 100, 20", false);
    assert(rt.auto_mode == true);
    assert(rt.auto_current_line == 100);
    assert(rt.auto_step == 20);

    fclose(out);
    runtime_free(&rt);
    printf("test_edit_and_auto: PASS\n");
}

static void test_ctrl_c_break_program(void) {
    Runtime rt;
    runtime_init(&rt);
    runtime_process_input(&rt, "10 GOTO 10", false);

    // Simulate interrupt flag set
    g_interrupted = 1;
    FILE *out = tmpfile();
    rt.out = out;

    runtime_run(&rt, 0);

    assert(rt.last_error.code == ERR_STOP);
    assert(rt.last_error.custom_msg != NULL);
    assert(strcmp(rt.last_error.custom_msg, "BREAK into program") == 0);
    assert(rt.last_error.line_no == 10);
    assert(rt.last_error.stmt_index == 1);

    char buf[128];
    error_format(&rt.last_error, buf, sizeof(buf));
    assert(strcmp(buf, "9 BREAK into program, 10:1") == 0);

    fclose(out);
    runtime_free(&rt);
    printf("test_ctrl_c_break_program: PASS\n");
}

static void test_ctrl_c_break_input(void) {
    Runtime rt;
    runtime_init(&rt);
    runtime_process_input(&rt, "10 INPUT a", false);

    // Simulate interrupt during input
    g_interrupted = 1;
    FILE *out = tmpfile();
    FILE *in = tmpfile();
    rt.out = out;
    rt.in = in;

    runtime_run(&rt, 0);

    assert(rt.last_error.code == ERR_STOP);
    assert(rt.last_error.custom_msg != NULL);
    assert(strcmp(rt.last_error.custom_msg, "BREAK into program") == 0);
    assert(rt.last_error.line_no == 10);

    char buf[128];
    error_format(&rt.last_error, buf, sizeof(buf));
    assert(strcmp(buf, "9 BREAK into program, 10:1") == 0);

    fclose(in);
    fclose(out);
    runtime_free(&rt);
    printf("test_ctrl_c_break_input: PASS\n");
}

static void test_ctrl_c_break_direct(void) {
    Runtime rt;
    runtime_init(&rt);
    FILE *out = tmpfile();
    rt.out = out;

    g_interrupted = 1;
    runtime_process_input(&rt, "PRINT 1: PRINT 2", false);

    assert(rt.last_error.code == ERR_STOP);
    assert(rt.last_error.custom_msg != NULL);
    assert(strcmp(rt.last_error.custom_msg, "BREAK into program") == 0);
    assert(rt.last_error.line_no == -1);

    char buf[128];
    error_format(&rt.last_error, buf, sizeof(buf));
    assert(strcmp(buf, "9 BREAK into program") == 0);

    fclose(out);
    runtime_free(&rt);
    printf("test_ctrl_c_break_direct: PASS\n");
}

static void on_test_sigalrm(int sig) {
    (void)sig;
    kill(getpid(), SIGINT);
}

static void test_async_sigint(void) {
    setup_signal_handlers();

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = on_test_sigalrm;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGALRM, &sa, NULL);

    Runtime rt;
    runtime_init(&rt);
    runtime_process_input(&rt, "10 LET X = 1: GOTO 10", false);

    FILE *out = tmpfile();
    rt.out = out;

    // Trigger SIGALRM after 20ms
    struct itimerval itv;
    memset(&itv, 0, sizeof(itv));
    itv.it_value.tv_usec = 20000;
    setitimer(ITIMER_REAL, &itv, NULL);

    runtime_run(&rt, 0);

    // Cancel timer
    memset(&itv, 0, sizeof(itv));
    setitimer(ITIMER_REAL, &itv, NULL);

    assert(rt.last_error.code == ERR_STOP);
    assert(rt.last_error.custom_msg != NULL);
    assert(strcmp(rt.last_error.custom_msg, "BREAK into program") == 0);
    assert(rt.last_error.line_no == 10);

    fclose(out);
    runtime_free(&rt);
    printf("test_async_sigint: PASS\n");
}

int main(void) {
    test_auto_capitalization();
    test_syntax_validation();
    test_ingestion_gating();
    test_renum();
    test_edit_and_auto();
    test_ctrl_c_break_program();
    test_ctrl_c_break_input();
    test_ctrl_c_break_direct();
    test_async_sigint();
    return 0;
}
