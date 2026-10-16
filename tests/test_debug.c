#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include "../src/runtime.h"
#include "../src/normalize.h"

static void test_continue_after_stop(void) {
    Runtime rt;
    runtime_init(&rt);
    FILE *out = tmpfile();
    assert(out != NULL);
    rt.out = out;

    // 10 PRINT "First"
    // 20 STOP
    // 30 PRINT "Second"
    runtime_process_input(&rt, "10 PRINT \"First\"", true);
    runtime_process_input(&rt, "20 STOP", true);
    runtime_process_input(&rt, "30 PRINT \"Second\"", true);

    rewind(out);
    ftruncate(fileno(out), 0);

    runtime_process_input(&rt, "RUN", true);
    assert(rt.last_error.code == ERR_STOP);
    assert(rt.can_continue == true);

    rewind(out);
    char buf[512];
    size_t n = fread(buf, 1, sizeof(buf) - 1, out);
    buf[n] = '\0';
    assert(strstr(buf, "First") != NULL);
    assert(strstr(buf, "Second") == NULL);
    assert(strstr(buf, "9 STOP statement, 20:1") != NULL);

    // Now CONTINUE
    rewind(out);
    ftruncate(fileno(out), 0);

    runtime_process_input(&rt, "CONTINUE", true);
    assert(rt.last_error.code == ERR_OK);

    rewind(out);
    n = fread(buf, 1, sizeof(buf) - 1, out);
    buf[n] = '\0';
    assert(strstr(buf, "Second") != NULL);
    assert(strstr(buf, "0 OK, 30:1") != NULL);

    fclose(out);
    runtime_free(&rt);
    printf("test_continue_after_stop: PASS\n");
}

static void test_continue_multi_statement_line(void) {
    Runtime rt;
    runtime_init(&rt);
    FILE *out = tmpfile();
    rt.out = out;

    // 10 PRINT "A";: STOP : PRINT "B";
    // 20 PRINT "C"
    runtime_process_input(&rt, "10 PRINT \"A\";: STOP : PRINT \"B\";", true);
    runtime_process_input(&rt, "20 PRINT \"C\"", true);

    rewind(out);
    ftruncate(fileno(out), 0);

    runtime_process_input(&rt, "RUN", true);
    assert(rt.last_error.code == ERR_STOP);
    assert(rt.can_continue == true);
    assert(rt.cont_line_idx == 0); // Still on line 10
    assert(rt.cont_stmt_idx == 3); // Statement 3 is PRINT "B"

    rewind(out);
    char buf[512];
    size_t n = fread(buf, 1, sizeof(buf) - 1, out);
    buf[n] = '\0';
    assert(strstr(buf, "A") != NULL);
    assert(strstr(buf, "B") == NULL);
    assert(strstr(buf, "9 STOP statement, 10:2") != NULL);

    // Resume execution
    rewind(out);
    ftruncate(fileno(out), 0);

    runtime_process_input(&rt, "CONTINUE", true);
    assert(rt.last_error.code == ERR_OK);

    rewind(out);
    n = fread(buf, 1, sizeof(buf) - 1, out);
    buf[n] = '\0';
    assert(strstr(buf, "BC") != NULL);
    assert(strstr(buf, "0 OK, 20:1") != NULL);

    fclose(out);
    runtime_free(&rt);
    printf("test_continue_multi_statement_line: PASS\n");
}

static void test_continue_with_line_number(void) {
    Runtime rt;
    runtime_init(&rt);
    FILE *out = tmpfile();
    rt.out = out;

    runtime_process_input(&rt, "10 PRINT \"Line 10\"", true);
    runtime_process_input(&rt, "20 STOP", true);
    runtime_process_input(&rt, "30 PRINT \"Line 30\"", true);
    runtime_process_input(&rt, "40 PRINT \"Line 40\"", true);
    runtime_process_input(&rt, "50 PRINT \"Line 50\"", true);

    rewind(out);
    ftruncate(fileno(out), 0);

    runtime_process_input(&rt, "RUN", true);
    assert(rt.last_error.code == ERR_STOP);

    // CONTINUE 40 (pause before executing line 40)
    rewind(out);
    ftruncate(fileno(out), 0);

    runtime_process_input(&rt, "CONTINUE 40", true);
    assert(rt.last_error.code == ERR_STOP);
    assert(rt.can_continue == true);
    assert(rt.cont_line_idx == 3); // Line 40 is at index 3
    assert(rt.cont_stmt_idx == 1);

    rewind(out);
    char buf[512];
    size_t n = fread(buf, 1, sizeof(buf) - 1, out);
    buf[n] = '\0';
    assert(strstr(buf, "Line 30") != NULL);
    assert(strstr(buf, "Line 40") == NULL);
    assert(strstr(buf, "9 Breakpoint reached, 40:1") != NULL);

    // CONTINUE to end
    rewind(out);
    ftruncate(fileno(out), 0);

    runtime_process_input(&rt, "CONTINUE", true);
    assert(rt.last_error.code == ERR_OK);

    rewind(out);
    n = fread(buf, 1, sizeof(buf) - 1, out);
    buf[n] = '\0';
    assert(strstr(buf, "Line 40") != NULL);
    assert(strstr(buf, "Line 50") != NULL);
    assert(strstr(buf, "0 OK, 50:1") != NULL);

    fclose(out);
    runtime_free(&rt);
    printf("test_continue_with_line_number: PASS\n");
}

static void test_continue_errors(void) {
    Runtime rt;
    runtime_init(&rt);
    FILE *out = tmpfile();
    rt.out = out;

    // Fresh runtime cannot CONTINUE
    runtime_process_input(&rt, "CONTINUE", true);
    assert(rt.last_error.code == ERR_NONSENSE);

    // Program that completes normally cannot CONTINUE
    runtime_process_input(&rt, "10 PRINT \"Done\"", true);
    runtime_process_input(&rt, "RUN", true);
    assert(rt.last_error.code == ERR_OK);

    runtime_process_input(&rt, "CONTINUE", true);
    assert(rt.last_error.code == ERR_NONSENSE);

    // Stopped program but invalid line target
    runtime_process_input(&rt, "20 STOP", true);
    runtime_process_input(&rt, "30 PRINT \"After\"", true);
    runtime_process_input(&rt, "RUN", true);
    assert(rt.last_error.code == ERR_STOP);

    // Line 999 does not exist
    runtime_process_input(&rt, "CONTINUE 999", true);
    assert(rt.last_error.code == ERR_INTEGER_RANGE);

    // Negative line
    runtime_process_input(&rt, "CONTINUE -5", true);
    assert(rt.last_error.code == ERR_INTEGER_RANGE);

    fclose(out);
    runtime_free(&rt);
    printf("test_continue_errors: PASS\n");
}

static void test_continue_in_program_statement(void) {
    Runtime rt;
    runtime_init(&rt);
    FILE *out = tmpfile();
    rt.out = out;

    // 10 PRINT "Start"
    // 20 CONTINUE 40
    // 30 PRINT "Between"
    // 40 PRINT "End"
    runtime_process_input(&rt, "10 PRINT \"Start\"", true);
    runtime_process_input(&rt, "20 CONTINUE 40", true);
    runtime_process_input(&rt, "30 PRINT \"Between\"", true);
    runtime_process_input(&rt, "40 PRINT \"End\"", true);

    rewind(out);
    ftruncate(fileno(out), 0);

    runtime_process_input(&rt, "RUN", true);
    assert(rt.last_error.code == ERR_STOP);
    assert(rt.can_continue == true);
    assert(rt.cont_line_idx == 3); // Line 40

    rewind(out);
    char buf[512];
    size_t n = fread(buf, 1, sizeof(buf) - 1, out);
    buf[n] = '\0';
    assert(strstr(buf, "Start") != NULL);
    assert(strstr(buf, "Between") != NULL);
    assert(strstr(buf, "End") == NULL);
    assert(strstr(buf, "9 Breakpoint reached, 40:1") != NULL);

    // Direct CONTINUE finishes the program
    rewind(out);
    ftruncate(fileno(out), 0);

    runtime_process_input(&rt, "CONTINUE", true);
    assert(rt.last_error.code == ERR_OK);

    rewind(out);
    n = fread(buf, 1, sizeof(buf) - 1, out);
    buf[n] = '\0';
    assert(strstr(buf, "End") != NULL);

    fclose(out);
    runtime_free(&rt);
    printf("test_continue_in_program_statement: PASS\n");
}

static void test_breakpoints_and_clear(void) {
    Runtime rt;
    runtime_init(&rt);
    FILE *out = tmpfile();
    rt.out = out;

    runtime_process_input(&rt, "10 PRINT \"L10\"", true);
    runtime_process_input(&rt, "20 PRINT \"L20\"", true);
    runtime_process_input(&rt, "30 PRINT \"L30\"", true);
    runtime_process_input(&rt, "40 PRINT \"L40\"", true);

    // Set breakpoints on 20 and 40
    runtime_process_input(&rt, "BREAK 20, 40", true);
    assert(runtime_has_breakpoint(&rt, 20));
    assert(runtime_has_breakpoint(&rt, 40));
    assert(!runtime_has_breakpoint(&rt, 30));

    rewind(out);
    ftruncate(fileno(out), 0);

    runtime_process_input(&rt, "RUN", true);
    assert(rt.last_error.code == ERR_STOP);
    assert(rt.can_continue == true);
    assert(rt.cont_line_idx == 1); // Line 20

    rewind(out);
    char buf[512];
    size_t n = fread(buf, 1, sizeof(buf) - 1, out);
    buf[n] = '\0';
    assert(strstr(buf, "L10") != NULL);
    assert(strstr(buf, "L20") == NULL);
    assert(strstr(buf, "9 Breakpoint reached, 20:1") != NULL);

    // CONTINUE to line 40 breakpoint
    rewind(out);
    ftruncate(fileno(out), 0);

    runtime_process_input(&rt, "CONTINUE", true);
    assert(rt.last_error.code == ERR_STOP);
    assert(rt.cont_line_idx == 3); // Line 40

    rewind(out);
    n = fread(buf, 1, sizeof(buf) - 1, out);
    buf[n] = '\0';
    assert(strstr(buf, "L20") != NULL);
    assert(strstr(buf, "L30") != NULL);
    assert(strstr(buf, "L40") == NULL);
    assert(strstr(buf, "9 Breakpoint reached, 40:1") != NULL);

    // CLEAR clears breakpoints!
    runtime_process_input(&rt, "CLEAR", true);
    assert(rt.breakpoint_count == 0);
    assert(!runtime_has_breakpoint(&rt, 20));
    assert(!runtime_has_breakpoint(&rt, 40));
    assert(!rt.can_continue);

    // RUN now runs through all lines without stopping
    rewind(out);
    ftruncate(fileno(out), 0);

    runtime_process_input(&rt, "RUN", true);
    assert(rt.last_error.code == ERR_OK);

    rewind(out);
    n = fread(buf, 1, sizeof(buf) - 1, out);
    buf[n] = '\0';
    assert(strstr(buf, "L10") != NULL);
    assert(strstr(buf, "L20") != NULL);
    assert(strstr(buf, "L30") != NULL);
    assert(strstr(buf, "L40") != NULL);
    assert(strstr(buf, "0 OK, 40:1") != NULL);

    fclose(out);
    runtime_free(&rt);
    printf("test_breakpoints_and_clear: PASS\n");
}

static void test_inline_break_statement(void) {
    Runtime rt;
    runtime_init(&rt);
    FILE *out = tmpfile();
    rt.out = out;

    // 10 PRINT "Before break"
    // 20 BREAK
    // 30 PRINT "After break"
    runtime_process_input(&rt, "10 PRINT \"Before break\"", true);
    runtime_process_input(&rt, "20 BREAK", true);
    runtime_process_input(&rt, "30 PRINT \"After break\"", true);

    rewind(out);
    ftruncate(fileno(out), 0);

    runtime_process_input(&rt, "RUN", true);
    assert(rt.last_error.code == ERR_STOP);
    assert(rt.can_continue == true);

    rewind(out);
    char buf[512];
    size_t n = fread(buf, 1, sizeof(buf) - 1, out);
    buf[n] = '\0';
    assert(strstr(buf, "Before break") != NULL);
    assert(strstr(buf, "After break") == NULL);
    assert(strstr(buf, "9 Breakpoint reached, 20:1") != NULL);

    // CONTINUE resumes at line 30
    rewind(out);
    ftruncate(fileno(out), 0);

    runtime_process_input(&rt, "CONTINUE", true);
    assert(rt.last_error.code == ERR_OK);

    rewind(out);
    n = fread(buf, 1, sizeof(buf) - 1, out);
    buf[n] = '\0';
    assert(strstr(buf, "After break") != NULL);

    fclose(out);
    runtime_free(&rt);
    printf("test_inline_break_statement: PASS\n");
}

static void test_verify_keyword(void) {
    Runtime rt;
    runtime_init(&rt);
    FILE *out = tmpfile();
    rt.out = out;

    // Initially no variables defined
    runtime_process_input(&rt, "VERIFY", true);
    rewind(out);
    char buf[1024];
    size_t n = fread(buf, 1, sizeof(buf) - 1, out);
    buf[n] = '\0';
    assert(strstr(buf, "(no variables defined)") != NULL);

    // Define numeric and string variables and arrays
    runtime_process_input(&rt, "LET A = 42.125", true);
    runtime_process_input(&rt, "LET SCORE = 1500", true);
    runtime_process_input(&rt, "LET NAME$ = \"Sinclair User\"", true);
    runtime_process_input(&rt, "DIM M(3, 4)", true);
    runtime_process_input(&rt, "DIM C$(5, 10)", true);

    rewind(out);
    ftruncate(fileno(out), 0);

    runtime_process_input(&rt, "VERIFY", true);
    rewind(out);
    n = fread(buf, 1, sizeof(buf) - 1, out);
    buf[n] = '\0';

    assert(strstr(buf, "Variable") != NULL);
    assert(strstr(buf, "Type") != NULL);
    assert(strstr(buf, "Dimensions / Size") != NULL);
    assert(strstr(buf, "Value Preview") != NULL);

    assert(strstr(buf, "A") != NULL);
    assert(strstr(buf, "NUMBER") != NULL);
    assert(strstr(buf, "42.125") != NULL);

    assert(strstr(buf, "SCORE") != NULL);
    assert(strstr(buf, "1500") != NULL);

    assert(strstr(buf, "NAME$") != NULL);
    assert(strstr(buf, "STRING") != NULL);
    assert(strstr(buf, "13 bytes") != NULL);
    assert(strstr(buf, "\"Sinclair User\"") != NULL);

    assert(strstr(buf, "M") != NULL);
    assert(strstr(buf, "ARRAY(N)") != NULL);
    assert(strstr(buf, "(3, 4)") != NULL);

    assert(strstr(buf, "C$") != NULL);
    assert(strstr(buf, "ARRAY(S)") != NULL);
    assert(strstr(buf, "(5, 10)") != NULL);

    // Test long string truncation (> 27 chars)
    runtime_process_input(&rt, "LET LONGSTR$ = \"This is a very long string that should be truncated\"", true);
    rewind(out);
    ftruncate(fileno(out), 0);

    runtime_process_input(&rt, "VERIFY", true);
    rewind(out);
    n = fread(buf, 1, sizeof(buf) - 1, out);
    buf[n] = '\0';
    assert(strstr(buf, "LONGSTR$") != NULL);
    assert(strstr(buf, "...") != NULL);

    // After CLEAR, VERIFY should report no variables
    runtime_process_input(&rt, "CLEAR", true);
    rewind(out);
    ftruncate(fileno(out), 0);

    runtime_process_input(&rt, "VERIFY", true);
    rewind(out);
    n = fread(buf, 1, sizeof(buf) - 1, out);
    buf[n] = '\0';
    assert(strstr(buf, "(no variables defined)") != NULL);

    fclose(out);
    runtime_free(&rt);
    printf("test_verify_keyword: PASS\n");
}

static void test_debug_syntax_and_normalization(void) {
    // Keyword auto-capitalization
    char *res1 = normalize_keywords("continue: verify: break 20, 30");
    assert(strcmp(res1, "CONTINUE: VERIFY: BREAK 20, 30") == 0);
    free(res1);

    char *res2 = normalize_keywords("10 continue 50: 20 verify");
    assert(strcmp(res2, "10 CONTINUE 50: 20 VERIFY") == 0);
    free(res2);

    // Syntax validation
    size_t err_col = 0;
    assert(syntax_validate_line("CONTINUE", &err_col));
    assert(syntax_validate_line("CONTINUE 50", &err_col));
    assert(syntax_validate_line("VERIFY", &err_col));
    assert(syntax_validate_line("BREAK", &err_col));
    assert(syntax_validate_line("BREAK 20", &err_col));
    assert(syntax_validate_line("BREAK 20, 40, 60", &err_col));

    // Standalone illegal syntax
    assert(!syntax_validate_line("VERIFY 10", &err_col));

    printf("test_debug_syntax_and_normalization: PASS\n");
}

static void test_interactive_goto(void) {
    Runtime rt;
    runtime_init(&rt);
    FILE *out = tmpfile();
    assert(out != NULL);
    rt.out = out;

    // 10 PRINT "A="; A; " B$="; B$
    // 20 LET A = A + 1: LET B$ = "Modified"
    // 30 STOP
    runtime_process_input(&rt, "10 PRINT \"A=\"; A; \" B$=\"; B$", true);
    runtime_process_input(&rt, "20 LET A = A + 1: LET B$ = \"Modified\"", true);
    runtime_process_input(&rt, "30 STOP", true);

    // Set initial variables in direct mode
    runtime_process_input(&rt, "LET A = 42: LET B$ = \"Initial\"", true);

    rewind(out);
    ftruncate(fileno(out), 0);

    // GOTO 10 interactively - preserves variables!
    runtime_process_input(&rt, "GOTO 10", true);
    assert(rt.last_error.code == ERR_STOP);

    rewind(out);
    char buf[512];
    size_t n = fread(buf, 1, sizeof(buf) - 1, out);
    buf[n] = '\0';
    assert(strstr(buf, "A=42 B$=Initial") != NULL);

    // Verify variables were modified by line 20
    double val_a = 0;
    const char *val_b = NULL;
    size_t len_b = 0;
    assert(symtab_get_num(&rt.symtab, "A", &val_a) && val_a == 43.0);
    assert(symtab_get_str(&rt.symtab, "B$", &val_b, &len_b) && strcmp(val_b, "Modified") == 0);

    // GOTO 10 again - continues with current modified variables A=43 B$=Modified
    rewind(out);
    ftruncate(fileno(out), 0);

    runtime_process_input(&rt, "GOTO 10", true);
    assert(rt.last_error.code == ERR_STOP);

    rewind(out);
    n = fread(buf, 1, sizeof(buf) - 1, out);
    buf[n] = '\0';
    assert(strstr(buf, "A=43 B$=Modified") != NULL);

    // In contrast, RUN 10 clears variables!
    rewind(out);
    ftruncate(fileno(out), 0);

    runtime_process_input(&rt, "RUN 10", true);
    // Since RUN clears variables, evaluating A in line 10 raises ERR_VAR_NOT_FOUND
    assert(rt.last_error.code == ERR_VAR_NOT_FOUND);
    assert(!symtab_get_num(&rt.symtab, "A", &val_a));

    // GOTO with invalid target
    runtime_process_input(&rt, "GOTO 999", true);
    assert(rt.last_error.code == ERR_INTEGER_RANGE);

    runtime_process_input(&rt, "GOTO -5", true);
    assert(rt.last_error.code == ERR_INTEGER_RANGE);

    fclose(out);
    runtime_free(&rt);
    printf("test_interactive_goto: PASS\n");
}

int main(void) {
    test_continue_after_stop();
    test_continue_multi_statement_line();
    test_continue_with_line_number();
    test_continue_errors();
    test_continue_in_program_statement();
    test_breakpoints_and_clear();
    test_inline_break_statement();
    test_verify_keyword();
    test_debug_syntax_and_normalization();
    test_interactive_goto();
    printf("All debug tests passed successfully!\n");
    return 0;
}
