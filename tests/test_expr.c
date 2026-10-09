#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <math.h>
#include "../src/lexer.h"
#include "../src/value.h"
#include "../src/symtab.h"
#include "../src/expr.h"

static Value eval_str(const char *expr_str, SymTab *st, BasicError *err) {
    Lexer l;
    lexer_init(&l, expr_str);
    *err = (BasicError){ .code = ERR_OK, .custom_msg = NULL, .line_no = -1, .stmt_index = 0 };
    Value v = expr_eval(&l, st, err);
    token_free(&l.current);
    return v;
}

static void test_arithmetic(void) {
    SymTab st;
    symtab_init(&st);
    BasicError err;

    Value v1 = eval_str("2 + 3 * 4", &st, &err);
    assert(err.code == ERR_OK && value_is_num(&v1) && v1.as.num == 14.0);

    Value v2 = eval_str("(2 + 3) * 4", &st, &err);
    assert(err.code == ERR_OK && value_is_num(&v2) && v2.as.num == 20.0);

    Value v3 = eval_str("2 ^ 3 ^ 2", &st, &err); // Right associative: 2 ^ 9 = 512
    assert(err.code == ERR_OK && value_is_num(&v3) && v3.as.num == 512.0);

    Value v4 = eval_str("-3 ^ 2", &st, &err); // Sinclair precedence: -(3^2) = -9
    assert(err.code == ERR_OK && value_is_num(&v4) && v4.as.num == -9.0);

    symtab_free(&st);
    printf("test_arithmetic: PASS\n");
}

static void test_relational_and_logic(void) {
    SymTab st;
    symtab_init(&st);
    BasicError err;

    Value v1 = eval_str("5 > 3", &st, &err);
    assert(err.code == ERR_OK && v1.as.num == 1.0);

    Value v2 = eval_str("5 = 3", &st, &err);
    assert(err.code == ERR_OK && v2.as.num == 0.0);

    Value v3 = eval_str("5 <> 3", &st, &err);
    assert(err.code == ERR_OK && v3.as.num == 1.0);

    // NOT binds looser than relational: NOT 5 = 3 -> NOT (5 = 3) -> NOT 0 -> 1
    Value v4 = eval_str("NOT 5 = 3", &st, &err);
    assert(err.code == ERR_OK && v4.as.num == 1.0);

    // Sinclair AND shortcut
    Value v5 = eval_str("10 AND 1", &st, &err);
    assert(err.code == ERR_OK && v5.as.num == 10.0);

    Value v6 = eval_str("10 AND 0", &st, &err);
    assert(err.code == ERR_OK && v6.as.num == 0.0);

    Value v7 = eval_str("\"YES\" AND 1", &st, &err);
    assert(err.code == ERR_OK && value_is_str(&v7) && strcmp(v7.as.str.chars, "YES") == 0);
    value_free(&v7);

    Value v8 = eval_str("\"YES\" AND 0", &st, &err);
    assert(err.code == ERR_OK && value_is_str(&v8) && strcmp(v8.as.str.chars, "") == 0);
    value_free(&v8);

    // Sinclair OR shortcut
    Value v9 = eval_str("0 OR 42", &st, &err);
    assert(err.code == ERR_OK && v9.as.num == 42.0);

    Value v10 = eval_str("12 OR 42", &st, &err);
    assert(err.code == ERR_OK && v10.as.num == 12.0);

    symtab_free(&st);
    printf("test_relational_and_logic: PASS\n");
}

static void test_string_slicing(void) {
    SymTab st;
    symtab_init(&st);
    BasicError err;

    Value v1 = eval_str("\"ABCDE\"(2 TO 4)", &st, &err);
    assert(err.code == ERR_OK && strcmp(v1.as.str.chars, "BCD") == 0);
    value_free(&v1);

    Value v2 = eval_str("\"ABCDE\"(3 TO)", &st, &err);
    assert(err.code == ERR_OK && strcmp(v2.as.str.chars, "CDE") == 0);
    value_free(&v2);

    Value v3 = eval_str("\"ABCDE\"(TO 2)", &st, &err);
    assert(err.code == ERR_OK && strcmp(v3.as.str.chars, "AB") == 0);
    value_free(&v3);

    Value v4 = eval_str("\"ABCDE\"(3)", &st, &err);
    assert(err.code == ERR_OK && strcmp(v4.as.str.chars, "C") == 0);
    value_free(&v4);

    Value v5 = eval_str("\"ABCDE\"(4 TO 2)", &st, &err); // start > end -> ""
    assert(err.code == ERR_OK && strcmp(v5.as.str.chars, "") == 0);
    value_free(&v5);

    symtab_free(&st);
    printf("test_string_slicing: PASS\n");
}

static void test_functions(void) {
    SymTab st;
    symtab_init(&st);
    BasicError err;

    Value v1 = eval_str("ABS(-5.5)", &st, &err);
    assert(err.code == ERR_OK && v1.as.num == 5.5);

    Value v2 = eval_str("INT(3.7)", &st, &err);
    assert(err.code == ERR_OK && v2.as.num == 3.0);

    Value v3 = eval_str("INT(-3.7)", &st, &err);
    assert(err.code == ERR_OK && v3.as.num == -4.0);

    Value v4 = eval_str("SQR(16)", &st, &err);
    assert(err.code == ERR_OK && v4.as.num == 4.0);

    Value v5 = eval_str("LEN(\"HELLO\")", &st, &err);
    assert(err.code == ERR_OK && v5.as.num == 5.0);

    Value v6 = eval_str("STR$(123)", &st, &err);
    assert(err.code == ERR_OK && strcmp(v6.as.str.chars, "123") == 0);
    value_free(&v6);

    Value v7 = eval_str("CHR$(65)", &st, &err);
    assert(err.code == ERR_OK && strcmp(v7.as.str.chars, "A") == 0);
    value_free(&v7);

    Value v8 = eval_str("CODE(\"A\")", &st, &err);
    assert(err.code == ERR_OK && v8.as.num == 65.0);

    Value v9 = eval_str("VAL(\"10 + 20\")", &st, &err);
    assert(err.code == ERR_OK && v9.as.num == 30.0);

    symtab_free(&st);
    printf("test_functions: PASS\n");
}

static void test_variables_and_arrays(void) {
    SymTab st;
    symtab_init(&st);
    BasicError err;

    // Numeric variable
    symtab_set_num(&st, "x", 10.0);
    symtab_set_num(&st, "y", 20.0);
    Value v1 = eval_str("x + y", &st, &err);
    assert(err.code == ERR_OK && v1.as.num == 30.0);

    // Case sensitivity: 'a' vs 'A'
    symtab_set_num(&st, "a", 1.0);
    symtab_set_num(&st, "A", 2.0);
    Value va = eval_str("a", &st, &err);
    Value vA = eval_str("A", &st, &err);
    assert(va.as.num == 1.0 && vA.as.num == 2.0);

    // String variable and slicing
    symtab_set_str(&st, "A$", "ZX Spectrum", 11);
    Value vs = eval_str("A$(4 TO 11)", &st, &err);
    assert(err.code == ERR_OK && strcmp(vs.as.str.chars, "Spectrum") == 0);
    value_free(&vs);

    // Numeric Array
    size_t dims[2] = { 3, 3 };
    assert(symtab_dim_num_array(&st, "M", 2, dims, &err));
    size_t idx[2] = { 2, 2 };
    assert(symtab_set_num_array(&st, "M", 2, idx, 42.0, &err));
    Value vm = eval_str("M(2, 2)", &st, &err);
    assert(err.code == ERR_OK && vm.as.num == 42.0);

    symtab_free(&st);
    printf("test_variables_and_arrays: PASS\n");
}

int main(void) {
    test_arithmetic();
    test_relational_and_logic();
    test_string_slicing();
    test_functions();
    test_variables_and_arrays();
    return 0;
}
