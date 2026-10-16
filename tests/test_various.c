#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "../src/runtime.h"
#include "../src/normalize.h"

static void test_def_fn_basic_numeric(void) {
    Runtime rt;
    runtime_init(&rt);
    FILE *out = tmpfile();
    assert(out != NULL);
    rt.out = out;

    // Direct mode single-param function
    runtime_process_input(&rt, "DEF FN square(x) = x * x", true);
    assert(rt.last_error.code == ERR_OK);

    runtime_process_input(&rt, "LET res = FN square(9)", true);
    assert(rt.last_error.code == ERR_OK);
    double v = 0;
    assert(symtab_get_num(&rt.symtab, "res", &v) && v == 81.0);

    // Multi-character identifier and multi-parameter function
    runtime_process_input(&rt, "DEF FN hypot(a, b) = SQR(a * a + b * b)", true);
    assert(rt.last_error.code == ERR_OK);

    runtime_process_input(&rt, "LET h = FN hypot(3, 4)", true);
    assert(rt.last_error.code == ERR_OK);
    assert(symtab_get_num(&rt.symtab, "h", &v) && v == 5.0);

    // Zero-parameter function
    runtime_process_input(&rt, "DEF FN my_pi = 3.14159", true);
    assert(rt.last_error.code == ERR_OK);

    runtime_process_input(&rt, "LET p = FN my_pi()", true);
    assert(rt.last_error.code == ERR_OK);
    assert(symtab_get_num(&rt.symtab, "p", &v) && v == 3.14159);

    fclose(out);
    runtime_free(&rt);
    printf("test_def_fn_basic_numeric: PASS\n");
}

static void test_def_fn_strings(void) {
    Runtime rt;
    runtime_init(&rt);
    FILE *out = tmpfile();
    assert(out != NULL);
    rt.out = out;

    // String function with string parameter
    runtime_process_input(&rt, "DEF FN greet$(name$) = \"Hello, \" + name$", true);
    assert(rt.last_error.code == ERR_OK);

    runtime_process_input(&rt, "LET G$ = FN greet$(\"World\")", true);
    assert(rt.last_error.code == ERR_OK);

    const char *s = NULL;
    size_t len = 0;
    assert(symtab_get_str(&rt.symtab, "G$", &s, &len));
    assert(strcmp(s, "Hello, World") == 0);

    // Mixed parameters (string and number)
    runtime_process_input(&rt, "DEF FN tag$(item$, num) = item$ + \"#\" + STR$(num)", true);
    assert(rt.last_error.code == ERR_OK);

    runtime_process_input(&rt, "LET T$ = FN tag$(\"item\", 42)", true);
    assert(rt.last_error.code == ERR_OK);
    assert(symtab_get_str(&rt.symtab, "T$", &s, &len));
    assert(strcmp(s, "item#42") == 0);

    fclose(out);
    runtime_free(&rt);
    printf("test_def_fn_strings: PASS\n");
}

static void test_def_fn_scoping_and_shadowing(void) {
    Runtime rt;
    runtime_init(&rt);
    FILE *out = tmpfile();
    assert(out != NULL);
    rt.out = out;

    // Define global variable x = 100
    runtime_process_input(&rt, "LET x = 100", true);
    assert(rt.last_error.code == ERR_OK);

    // Define function with parameter x
    runtime_process_input(&rt, "DEF FN double(x) = x * 2", true);
    assert(rt.last_error.code == ERR_OK);

    // Call function with 5 -> returns 10
    runtime_process_input(&rt, "LET r = FN double(5)", true);
    assert(rt.last_error.code == ERR_OK);

    double v = 0;
    assert(symtab_get_num(&rt.symtab, "r", &v) && v == 10.0);

    // Global x MUST still be 100!
    assert(symtab_get_num(&rt.symtab, "x", &v) && v == 100.0);

    // Verify parameter that did NOT previously exist is removed after call
    runtime_process_input(&rt, "DEF FN add_temp(z) = z + 10", true);
    runtime_process_input(&rt, "LET r2 = FN add_temp(3)", true);
    assert(symtab_get_num(&rt.symtab, "r2", &v) && v == 13.0);
    assert(!symtab_get_num(&rt.symtab, "z", &v)); // z does not exist globally

    fclose(out);
    runtime_free(&rt);
    printf("test_def_fn_scoping_and_shadowing: PASS\n");
}

static void test_def_fn_forward_reference(void) {
    Runtime rt;
    runtime_init(&rt);
    FILE *out = tmpfile();
    assert(out != NULL);
    rt.out = out;

    // Stored program where FN triple is called on line 10 BEFORE being defined on line 20
    runtime_process_input(&rt, "10 LET res = FN triple(7)", false);
    runtime_process_input(&rt, "20 DEF FN triple(n) = n * 3", false);

    runtime_run(&rt, 0);
    assert(rt.last_error.code == ERR_OK);

    double v = 0;
    assert(symtab_get_num(&rt.symtab, "res", &v) && v == 21.0);

    fclose(out);
    runtime_free(&rt);
    printf("test_def_fn_forward_reference: PASS\n");
}

static void test_def_fn_composition(void) {
    Runtime rt;
    runtime_init(&rt);
    FILE *out = tmpfile();
    assert(out != NULL);
    rt.out = out;

    // Functions calling other functions
    runtime_process_input(&rt, "DEF FN inc(x) = x + 1", true);
    assert(rt.last_error.code == ERR_OK);

    runtime_process_input(&rt, "DEF FN double_inc(x) = FN inc(x) * 2", true);
    assert(rt.last_error.code == ERR_OK);

    runtime_process_input(&rt, "LET res = FN double_inc(4)", true);
    assert(rt.last_error.code == ERR_OK);

    double v = 0;
    assert(symtab_get_num(&rt.symtab, "res", &v) && v == 10.0);

    fclose(out);
    runtime_free(&rt);
    printf("test_def_fn_composition: PASS\n");
}

static void test_def_fn_errors(void) {
    Runtime rt;
    runtime_init(&rt);
    FILE *out = tmpfile();
    assert(out != NULL);
    rt.out = out;

    // 1. Call undefined function
    runtime_process_input(&rt, "LET a = FN undefined(5)", true);
    assert(rt.last_error.code == ERR_NONSENSE);

    // 2. Argument count mismatch
    runtime_process_input(&rt, "DEF FN two_args(a, b) = a + b", true);
    runtime_process_input(&rt, "LET a = FN two_args(5)", true);
    assert(rt.last_error.code == ERR_NONSENSE);

    // 3. Argument type mismatch
    runtime_process_input(&rt, "LET a = FN two_args(\"hello\", 5)", true);
    assert(rt.last_error.code == ERR_NONSENSE);

    // 4. Return type mismatch
    runtime_process_input(&rt, "DEF FN num_but_str(x) = \"bad\"", true);
    runtime_process_input(&rt, "LET a = FN num_but_str(1)", true);
    assert(rt.last_error.code == ERR_NONSENSE);

    fclose(out);
    runtime_free(&rt);
    printf("test_def_fn_errors: PASS\n");
}

static void test_llist_pretty_printer(void) {
    Runtime rt;
    runtime_init(&rt);
    FILE *out = tmpfile();
    assert(out != NULL);
    rt.out = out;

    // Load structured program with loops and a jump target
    runtime_process_input(&rt, "10 REM Test Program", false);
    runtime_process_input(&rt, "20 FOR i = 1 TO 3", false);
    runtime_process_input(&rt, "30 PRINT i", false);
    runtime_process_input(&rt, "40 GOTO 60", false);
    runtime_process_input(&rt, "50 PRINT \"Skipped\"", false);
    runtime_process_input(&rt, "60 NEXT i", false);

    // Run LLIST
    runtime_process_input(&rt, "LLIST", true);
    assert(rt.last_error.code == ERR_OK);

    // Read back buffer
    fflush(out);
    fseek(out, 0, SEEK_SET);
    char buf[4096];
    size_t n = fread(buf, 1, sizeof(buf) - 1, out);
    buf[n] = '\0';

    // 1. Line numbers must be padded to 4 digits right-aligned
    assert(strstr(buf, "  10 ") != NULL);
    assert(strstr(buf, "  20 ") != NULL);

    // 2. Line 60 is targeted by GOTO 60, so must have target marker ▶
    assert(strstr(buf, "▶  60") != NULL);

    // 3. Line 30 and 40 are inside FOR loop, so must be indented by 2 spaces
    assert(strstr(buf, "  \033[1;33mPRINT") != NULL);
    assert(strstr(buf, "  \033[1;33mGOTO") != NULL);
    // Target line number after GOTO has matching bold magenta color and triangle
    assert(strstr(buf, "\033[1;35m▶60\033[0m") != NULL);

    // 4. ANSI colors must be present
    // Yellow for keywords: \033[1;33m
    assert(strstr(buf, "\033[1;33mFOR\033[0m") != NULL);
    assert(strstr(buf, "\033[1;33mPRINT\033[0m") != NULL);
    assert(strstr(buf, "\033[1;33mNEXT\033[0m") != NULL);
    // Green for string: \033[32m
    assert(strstr(buf, "\033[32m\"Skipped\"\033[0m") != NULL);
    // Magenta for numbers: \033[35m
    assert(strstr(buf, "\033[35m1\033[0m") != NULL);
    // Dim gray for REM: \033[90m
    assert(strstr(buf, "\033[90mREM Test Program\033[0m") != NULL);

    // 5. LLIST with range arguments is rejected
    runtime_process_input(&rt, "LLIST 10", true);
    assert(rt.last_error.code == ERR_NONSENSE);

    // 6. Plain LIST remains unchanged (no ANSI, plain format "%u %s\n")
    FILE *plain_out = tmpfile();
    assert(plain_out != NULL);
    rt.out = plain_out;
    runtime_process_input(&rt, "LIST", true);
    fflush(plain_out);
    fseek(plain_out, 0, SEEK_SET);
    char pbuf[2048];
    size_t pn = fread(pbuf, 1, sizeof(pbuf) - 1, plain_out);
    pbuf[pn] = '\0';
    assert(strstr(pbuf, "\033[") == NULL); // Absolutely no ANSI escape codes in plain LIST!
    assert(strstr(pbuf, "10 REM Test Program\n") != NULL);
    fclose(plain_out);

    fclose(out);
    runtime_free(&rt);
    printf("test_llist_pretty_printer: PASS\n");
}

static void test_normalization_and_syntax(void) {
    // Normalization test
    char *n1 = normalize_keywords("def fn my_func(x, y) = x * y");
    assert(strcmp(n1, "DEF FN my_func(x, y) = x * y") == 0);
    free(n1);

    char *n2 = normalize_keywords("print fn my_func(3, 4)");
    assert(strcmp(n2, "PRINT FN my_func(3, 4)") == 0);
    free(n2);

    char *n3 = normalize_keywords("llist");
    assert(strcmp(n3, "LLIST") == 0);
    free(n3);

    // Syntax validation
    size_t err_col = 0;
    assert(syntax_validate_line("DEF FN f(x) = x + 1", &err_col));
    assert(syntax_validate_line("DEF FN f() = 42", &err_col));
    assert(syntax_validate_line("DEF FN f = 42", &err_col));
    assert(syntax_validate_line("DEF FN pad$(s$, n) = s$", &err_col));
    assert(syntax_validate_line("PRINT FN f(10)", &err_col));
    assert(syntax_validate_line("LLIST", &err_col));

    // Rejections
    assert(!syntax_validate_line("DEF f(x) = 1", &err_col)); // missing FN
    assert(!syntax_validate_line("DEF FN (x) = 1", &err_col)); // missing fname
    assert(!syntax_validate_line("LLIST 10", &err_col)); // LLIST has no args

    printf("test_normalization_and_syntax: PASS\n");
}

int main(void) {
    test_def_fn_basic_numeric();
    test_def_fn_strings();
    test_def_fn_scoping_and_shadowing();
    test_def_fn_forward_reference();
    test_def_fn_composition();
    test_def_fn_errors();
    test_llist_pretty_printer();
    test_normalization_and_syntax();
    printf("All user function and pretty printer tests passed successfully!\n");
    return 0;
}
