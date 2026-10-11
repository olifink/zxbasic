#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <sys/stat.h>
#include "../src/runtime.h"
#include "../src/program.h"
#include "../src/normalize.h"

static void test_merge_feature(void) {
    Runtime rt;
    runtime_init(&rt);
    FILE *out = tmpfile();
    assert(out != NULL);
    rt.out = out;

    // Initial program
    // 10 LET A = 100
    // 20 PRINT "ORIG 20"
    // 30 PRINT "ORIG 30"
    runtime_process_input(&rt, "10 LET A = 100", true);
    runtime_process_input(&rt, "20 PRINT \"ORIG 20\"", true);
    runtime_process_input(&rt, "30 PRINT \"ORIG 30\"", true);

    // Run program to populate variable A
    runtime_process_input(&rt, "RUN", true);
    double val_a = 0;
    assert(symtab_get_num(&rt.symtab, "A", &val_a) && val_a == 100.0);

    // Create file to merge:
    // Overwrites line 20, adds line 40, and has an auto-run RUN 40 at the end
    const char *merge_file = "test_merge_temp.bas";
    FILE *f = fopen(merge_file, "w");
    assert(f != NULL);
    fprintf(f, "20 PRINT \"MERGED 20\"\n");
    fprintf(f, "40 PRINT \"MERGED 40\"\n");
    fprintf(f, "RUN 40\n"); // Trailing direct command must be ignored by MERGE
    fclose(f);

    // Merge the file
    runtime_process_input(&rt, "MERGE \"test_merge_temp.bas\"", true);
    assert(rt.last_error.code == ERR_OK);

    // Verify program store lines: 10, 20, 30, 40
    assert(rt.program.count == 4);
    assert(rt.program.lines[0].line_no == 10);
    assert(rt.program.lines[1].line_no == 20);
    assert(strcmp(rt.program.lines[1].source, "PRINT \"MERGED 20\"") == 0);
    assert(rt.program.lines[2].line_no == 30);
    assert(strcmp(rt.program.lines[2].source, "PRINT \"ORIG 30\"") == 0);
    assert(rt.program.lines[3].line_no == 40);
    assert(strcmp(rt.program.lines[3].source, "PRINT \"MERGED 40\"") == 0);

    // Verify variables in symtab were NOT cleared by MERGE
    val_a = 0;
    assert(symtab_get_num(&rt.symtab, "A", &val_a) && val_a == 100.0);

    // MERGE with non-existent file reports error
    runtime_process_input(&rt, "MERGE \"nonexistent_file_xyz.bas\"", true);
    assert(rt.last_error.code == ERR_NONSENSE);

    unlink(merge_file);
    fclose(out);
    runtime_free(&rt);
    printf("test_merge_feature: PASS\n");
}

static void test_save_line_autorun(void) {
    Runtime rt;
    runtime_init(&rt);
    FILE *out = tmpfile();
    assert(out != NULL);
    rt.out = out;

    // Create a program
    runtime_process_input(&rt, "10 PRINT \"LINE 10\"", true);
    runtime_process_input(&rt, "20 PRINT \"LINE 20\"", true);
    runtime_process_input(&rt, "30 PRINT \"LINE 30\"", true);

    const char *auto_file = "test_auto_temp.bas";

    // SAVE with LINE 20
    runtime_process_input(&rt, "SAVE \"test_auto_temp.bas\" LINE 20", true);
    assert(rt.last_error.code == ERR_OK);

    // Check file content on disk has trailing RUN 20
    FILE *f = fopen(auto_file, "r");
    assert(f != NULL);
    char buf[512];
    bool found_run_20 = false;
    while (fgets(buf, sizeof(buf), f)) {
        if (strstr(buf, "RUN 20") != NULL) {
            found_run_20 = true;
        }
    }
    fclose(f);
    assert(found_run_20);

    // Now clear runtime and LOAD the auto-run file: it should execute line 20 automatically!
    rewind(out);
    ftruncate(fileno(out), 0);

    runtime_process_input(&rt, "LOAD \"test_auto_temp.bas\"", true);
    assert(rt.last_error.code == ERR_OK);

    rewind(out);
    char out_buf[512];
    size_t n = fread(out_buf, 1, sizeof(out_buf) - 1, out);
    out_buf[n] = '\0';
    // Output should contain LINE 20 and LINE 30, but NOT LINE 10 (since started at 20)
    assert(strstr(out_buf, "LINE 20") != NULL);
    assert(strstr(out_buf, "LINE 30") != NULL);
    assert(strstr(out_buf, "LINE 10") == NULL);

    // SAVE with LINE (without line number) -> appends RUN
    const char *auto_file2 = "test_auto2_temp.bas";
    runtime_process_input(&rt, "SAVE \"test_auto2_temp.bas\" LINE", true);
    assert(rt.last_error.code == ERR_OK);

    f = fopen(auto_file2, "r");
    assert(f != NULL);
    bool found_run = false;
    while (fgets(buf, sizeof(buf), f)) {
        if (strcmp(buf, "RUN\n") == 0) {
            found_run = true;
        }
    }
    fclose(f);
    assert(found_run);

    // SAVE with invalid LINE number
    runtime_process_input(&rt, "SAVE \"invalid.bas\" LINE 10000", true);
    assert(rt.last_error.code == ERR_INTEGER_RANGE);

    unlink(auto_file);
    unlink(auto_file2);
    fclose(out);
    runtime_free(&rt);
    printf("test_save_line_autorun: PASS\n");
}

static void test_run_with_file_and_line(void) {
    Runtime rt;
    runtime_init(&rt);
    FILE *out = tmpfile();
    assert(out != NULL);
    rt.out = out;

    // Create a standalone file without auto-run
    const char *prog_file = "test_run_file_temp.bas";
    FILE *f = fopen(prog_file, "w");
    assert(f != NULL);
    fprintf(f, "10 PRINT \"PART 1\"\n");
    fprintf(f, "20 PRINT \"PART 2\"\n");
    fprintf(f, "30 PRINT \"PART 3\"\n");
    fclose(f);

    // RUN "file"
    rewind(out);
    ftruncate(fileno(out), 0);
    runtime_process_input(&rt, "RUN \"test_run_file_temp.bas\"", true);
    assert(rt.last_error.code == ERR_OK);

    rewind(out);
    char buf[512];
    size_t n = fread(buf, 1, sizeof(buf) - 1, out);
    buf[n] = '\0';
    assert(strstr(buf, "PART 1") != NULL);
    assert(strstr(buf, "PART 2") != NULL);
    assert(strstr(buf, "PART 3") != NULL);

    // RUN "file", line
    rewind(out);
    ftruncate(fileno(out), 0);
    runtime_process_input(&rt, "RUN \"test_run_file_temp.bas\", 20", true);
    assert(rt.last_error.code == ERR_OK);

    rewind(out);
    n = fread(buf, 1, sizeof(buf) - 1, out);
    buf[n] = '\0';
    assert(strstr(buf, "PART 1") == NULL);
    assert(strstr(buf, "PART 2") != NULL);
    assert(strstr(buf, "PART 3") != NULL);

    // RUN "file" line (without comma)
    rewind(out);
    ftruncate(fileno(out), 0);
    runtime_process_input(&rt, "RUN \"test_run_file_temp.bas\" 30", true);
    assert(rt.last_error.code == ERR_OK);

    rewind(out);
    n = fread(buf, 1, sizeof(buf) - 1, out);
    buf[n] = '\0';
    assert(strstr(buf, "PART 1") == NULL);
    assert(strstr(buf, "PART 2") == NULL);
    assert(strstr(buf, "PART 3") != NULL);

    // RUN non-existent file
    runtime_process_input(&rt, "RUN \"missing_file_xyz.bas\"", true);
    assert(rt.last_error.code == ERR_NONSENSE);

    // In-program chain loading
    const char *chain1 = "test_chain1.bas";
    const char *chain2 = "test_chain2.bas";
    FILE *f1 = fopen(chain1, "w");
    fprintf(f1, "10 PRINT \"CHAIN 1\"\n20 RUN \"test_chain2.bas\"\n");
    fclose(f1);

    FILE *f2 = fopen(chain2, "w");
    fprintf(f2, "10 PRINT \"CHAIN 2 SUCCESS\"\n");
    fclose(f2);

    rewind(out);
    ftruncate(fileno(out), 0);
    runtime_process_input(&rt, "RUN \"test_chain1.bas\"", true);

    rewind(out);
    n = fread(buf, 1, sizeof(buf) - 1, out);
    buf[n] = '\0';
    assert(strstr(buf, "CHAIN 1") != NULL);
    assert(strstr(buf, "CHAIN 2 SUCCESS") != NULL);

    unlink(prog_file);
    unlink(chain1);
    unlink(chain2);
    fclose(out);
    runtime_free(&rt);
    printf("test_run_with_file_and_line: PASS\n");
}

static void test_cat_feature(void) {
    Runtime rt;
    runtime_init(&rt);
    FILE *out = tmpfile();
    assert(out != NULL);
    rt.out = out;

    // CAT current directory
    runtime_process_input(&rt, "CAT", true);
    assert(rt.last_error.code == ERR_OK);

    rewind(out);
    char buf[4096];
    size_t n = fread(buf, 1, sizeof(buf) - 1, out);
    buf[n] = '\0';
    assert(strstr(buf, "Directory of .:") != NULL);
    assert(strstr(buf, "[DIR]  src/") != NULL);
    assert(strstr(buf, "[DIR]  tests/") != NULL);

    // CAT sub-path "tests"
    rewind(out);
    ftruncate(fileno(out), 0);
    runtime_process_input(&rt, "CAT \"tests\"", true);
    assert(rt.last_error.code == ERR_OK);

    rewind(out);
    n = fread(buf, 1, sizeof(buf) - 1, out);
    buf[n] = '\0';
    assert(strstr(buf, "Directory of tests:") != NULL);
    assert(strstr(buf, "test_expr.c") != NULL);

    // CAT non-existent directory
    runtime_process_input(&rt, "CAT \"nonexistent_dir_xyz123\"", true);
    assert(rt.last_error.code == ERR_NONSENSE);

    fclose(out);
    runtime_free(&rt);
    printf("test_cat_feature: PASS\n");
}

static void test_delete_file_feature(void) {
    Runtime rt;
    runtime_init(&rt);
    FILE *out = tmpfile();
    assert(out != NULL);
    rt.out = out;

    const char *temp_file = "test_del_temp.bas";
    FILE *f = fopen(temp_file, "w");
    assert(f != NULL);
    fprintf(f, "10 PRINT \"DELETE ME\"\n");
    fclose(f);

    // Verify file exists
    assert(access(temp_file, F_OK) == 0);

    // DELETE "test_del_temp.bas"
    runtime_process_input(&rt, "DELETE \"test_del_temp.bas\"", true);
    assert(rt.last_error.code == ERR_OK);

    // Verify file was deleted
    assert(access(temp_file, F_OK) != 0);

    // DELETE non-existent file reports error
    runtime_process_input(&rt, "DELETE \"nonexistent_del_file.bas\"", true);
    assert(rt.last_error.code == ERR_NONSENSE);

    // DELETE with string variable expression
    f = fopen(temp_file, "w");
    fprintf(f, "10 REM\n");
    fclose(f);
    assert(access(temp_file, F_OK) == 0);

    runtime_process_input(&rt, "LET F$ = \"test_del_temp.bas\"", true);
    runtime_process_input(&rt, "DELETE F$", true);
    assert(rt.last_error.code == ERR_OK);
    assert(access(temp_file, F_OK) != 0);

    // Programmatic DELETE file inside a running program is allowed
    f = fopen(temp_file, "w");
    fprintf(f, "10 REM\n");
    fclose(f);
    assert(access(temp_file, F_OK) == 0);

    runtime_process_input(&rt, "10 DELETE \"test_del_temp.bas\"", true);
    runtime_process_input(&rt, "RUN", true);
    assert(rt.last_error.code == ERR_OK);
    assert(access(temp_file, F_OK) != 0);

    fclose(out);
    runtime_free(&rt);
    printf("test_delete_file_feature: PASS\n");
}

static void test_delete_program_lines(void) {
    Runtime rt;
    runtime_init(&rt);
    FILE *out = tmpfile();
    assert(out != NULL);
    rt.out = out;

    // Helper setup
    #define SETUP_LINES() do { \
        program_clear(&rt.program); \
        runtime_process_input(&rt, "10 PRINT \"L10\"", true); \
        runtime_process_input(&rt, "20 PRINT \"L20\"", true); \
        runtime_process_input(&rt, "30 PRINT \"L30\"", true); \
        runtime_process_input(&rt, "40 PRINT \"L40\"", true); \
        runtime_process_input(&rt, "50 PRINT \"L50\"", true); \
        assert(rt.program.count == 5); \
    } while (0)

    // 1. DELETE 20 TO 40 -> leaves 10, 50
    SETUP_LINES();
    runtime_process_input(&rt, "DELETE 20 TO 40", true);
    assert(rt.last_error.code == ERR_OK);
    assert(rt.program.count == 2);
    assert(rt.program.lines[0].line_no == 10);
    assert(rt.program.lines[1].line_no == 50);

    // 2. DELETE TO 30 -> leaves 40, 50
    SETUP_LINES();
    runtime_process_input(&rt, "DELETE TO 30", true);
    assert(rt.last_error.code == ERR_OK);
    assert(rt.program.count == 2);
    assert(rt.program.lines[0].line_no == 40);
    assert(rt.program.lines[1].line_no == 50);

    // 3. DELETE 30 TO -> leaves 10, 20
    SETUP_LINES();
    runtime_process_input(&rt, "DELETE 30 TO", true);
    assert(rt.last_error.code == ERR_OK);
    assert(rt.program.count == 2);
    assert(rt.program.lines[0].line_no == 10);
    assert(rt.program.lines[1].line_no == 20);

    // 4. DELETE TO -> leaves 0 lines
    SETUP_LINES();
    runtime_process_input(&rt, "DELETE TO", true);
    assert(rt.last_error.code == ERR_OK);
    assert(rt.program.count == 0);

    // 5. DELETE 30 (single line shorthand) -> deletes only line 30
    SETUP_LINES();
    runtime_process_input(&rt, "DELETE 30", true);
    assert(rt.last_error.code == ERR_OK);
    assert(rt.program.count == 4);
    assert(rt.program.lines[0].line_no == 10);
    assert(rt.program.lines[1].line_no == 20);
    assert(rt.program.lines[2].line_no == 40);
    assert(rt.program.lines[3].line_no == 50);

    // 6. DELETE lines inside running program is forbidden (raises ERR_NONSENSE)
    SETUP_LINES();
    runtime_process_input(&rt, "60 DELETE 10 TO 20", true);
    runtime_process_input(&rt, "RUN 60", true);
    assert(rt.last_error.code == ERR_NONSENSE);

    #undef SETUP_LINES

    fclose(out);
    runtime_free(&rt);
    printf("test_delete_program_lines: PASS\n");
}

static void test_normalization_and_syntax(void) {
    // Keywords auto-capitalization
    char *n1 = normalize_keywords("merge \"test.bas\": cat \"dir\": delete 10 to 50");
    assert(strcmp(n1, "MERGE \"test.bas\": CAT \"dir\": DELETE 10 TO 50") == 0);
    free(n1);

    char *n2 = normalize_keywords("save \"backup.bas\" line 100");
    assert(strcmp(n2, "SAVE \"backup.bas\" LINE 100") == 0);
    free(n2);

    // Syntax validation
    size_t err_col = 0;
    assert(syntax_validate_line("MERGE \"test.bas\"", &err_col));
    assert(syntax_validate_line("CAT", &err_col));
    assert(syntax_validate_line("CAT \"tests\"", &err_col));
    assert(syntax_validate_line("SAVE \"test.bas\" LINE 20", &err_col));
    assert(syntax_validate_line("SAVE \"test.bas\" LINE", &err_col));
    assert(syntax_validate_line("RUN \"test.bas\", 10", &err_col));
    assert(syntax_validate_line("RUN \"test.bas\"", &err_col));
    assert(syntax_validate_line("DELETE \"file.bas\"", &err_col));
    assert(syntax_validate_line("DELETE 10 TO 50", &err_col));
    assert(syntax_validate_line("DELETE TO 50", &err_col));
    assert(syntax_validate_line("DELETE 10 TO", &err_col));

    printf("test_normalization_and_syntax: PASS\n");
}

int main(void) {
    test_merge_feature();
    test_save_line_autorun();
    test_run_with_file_and_line();
    test_cat_feature();
    test_delete_file_feature();
    test_delete_program_lines();
    test_normalization_and_syntax();
    printf("All file management tests passed successfully!\n");
    return 0;
}
