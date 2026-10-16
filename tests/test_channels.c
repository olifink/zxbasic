#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <sys/stat.h>

#include "../src/runtime.h"
#include "../src/normalize.h"

static void test_open_mode_disambiguation(void) {
    Runtime rt;
    runtime_init(&rt);
    FILE *out = tmpfile();
    assert(out != NULL);
    rt.out = out;

    const char *test_file = "test_chan_mode.txt";
    unlink(test_file);

    // 1. File does not exist -> OPEN creates in WRITE mode
    runtime_process_input(&rt, "OPEN #4, \"test_chan_mode.txt\"", true);
    assert(rt.last_error.code == ERR_OK);
    assert(rt.channels[4].is_open);
    assert(rt.channels[4].mode == CHANNEL_MODE_WRITE);

    // Write to channel
    runtime_process_input(&rt, "PRINT #4, \"Hello World\"", true);
    assert(rt.last_error.code == ERR_OK);

    // Close channel
    runtime_process_input(&rt, "CLOSE #4", true);
    assert(rt.last_error.code == ERR_OK);
    assert(!rt.channels[4].is_open);

    // 2. File now exists -> OPEN opens in READ mode
    assert(access(test_file, F_OK) == 0);
    runtime_process_input(&rt, "OPEN #4, \"test_chan_mode.txt\"", true);
    assert(rt.last_error.code == ERR_OK);
    assert(rt.channels[4].is_open);
    assert(rt.channels[4].mode == CHANNEL_MODE_READ);

    // Read back string
    runtime_process_input(&rt, "INPUT #4, A$", true);
    const char *str_val = NULL;
    size_t str_len = 0;
    assert(symtab_get_str(&rt.symtab, "A$", &str_val, &str_len));
    assert(strcmp(str_val, "Hello World") == 0);

    // Attempting to PRINT to read-only channel raises ERR_NONSENSE
    runtime_process_input(&rt, "PRINT #4, \"Should Fail\"", true);
    assert(rt.last_error.code == ERR_NONSENSE);

    // Close channel
    runtime_process_input(&rt, "CLOSE #4", true);
    unlink(test_file);

    fclose(out);
    runtime_free(&rt);
    printf("test_open_mode_disambiguation: PASS\n");
}

static void test_close_multiple_channels(void) {
    Runtime rt;
    runtime_init(&rt);
    FILE *out = tmpfile();
    assert(out != NULL);
    rt.out = out;

    unlink("t_c4.txt");
    unlink("t_c5.txt");
    unlink("t_c6.txt");

    runtime_process_input(&rt, "OPEN #4, \"t_c4.txt\"", true);
    runtime_process_input(&rt, "OPEN #5, \"t_c5.txt\"", true);
    runtime_process_input(&rt, "OPEN #6, \"t_c6.txt\"", true);
    assert(rt.channels[4].is_open);
    assert(rt.channels[5].is_open);
    assert(rt.channels[6].is_open);

    // Close all 3 at once
    runtime_process_input(&rt, "CLOSE #4, #5, #6", true);
    assert(rt.last_error.code == ERR_OK);
    assert(!rt.channels[4].is_open);
    assert(!rt.channels[5].is_open);
    assert(!rt.channels[6].is_open);

    // Closing already closed user channel is safe no-op
    runtime_process_input(&rt, "CLOSE #4", true);
    assert(rt.last_error.code == ERR_OK);

    // Closing system channel raises ERR_NONSENSE
    runtime_process_input(&rt, "CLOSE #0", true);
    assert(rt.last_error.code == ERR_NONSENSE);

    unlink("t_c4.txt");
    unlink("t_c5.txt");
    unlink("t_c6.txt");

    fclose(out);
    runtime_free(&rt);
    printf("test_close_multiple_channels: PASS\n");
}

static void test_move_persistence(void) {
    Runtime rt;
    runtime_init(&rt);
    FILE *out = tmpfile();
    assert(out != NULL);
    rt.out = out;

    const char *src_file = "test_move_src.txt";
    const char *dest_file = "test_move_dest.txt";
    unlink(src_file);
    unlink(dest_file);

    // Write source file
    runtime_process_input(&rt, "OPEN #4, \"test_move_src.txt\"", true);
    runtime_process_input(&rt, "PRINT #4, \"First Line\"", true);
    runtime_process_input(&rt, "PRINT #4, \"Second Line\"", true);
    runtime_process_input(&rt, "CLOSE #4", true);

    // Open source for read, destination for write
    runtime_process_input(&rt, "OPEN #4, \"test_move_src.txt\"", true);
    runtime_process_input(&rt, "OPEN #5, \"test_move_dest.txt\"", true);
    assert(rt.channels[4].mode == CHANNEL_MODE_READ);
    assert(rt.channels[5].mode == CHANNEL_MODE_WRITE);

    // MOVE #4 TO #5
    runtime_process_input(&rt, "MOVE #4 TO #5", true);
    assert(rt.last_error.code == ERR_OK);

    // Verify channels remain OPEN!
    assert(rt.channels[4].is_open);
    assert(rt.channels[5].is_open);

    // Append extra line to destination
    runtime_process_input(&rt, "PRINT #5, \"Third Line\"", true);
    assert(rt.last_error.code == ERR_OK);

    runtime_process_input(&rt, "CLOSE #4, #5", true);

    // Read back destination to verify all 3 lines are present
    runtime_process_input(&rt, "OPEN #6, \"test_move_dest.txt\"", true);
    runtime_process_input(&rt, "INPUT #6, L1$", true);
    runtime_process_input(&rt, "INPUT #6, L2$", true);
    runtime_process_input(&rt, "INPUT #6, L3$", true);
    runtime_process_input(&rt, "CLOSE #6", true);

    const char *v1 = NULL, *v2 = NULL, *v3 = NULL;
    size_t len1 = 0, len2 = 0, len3 = 0;
    assert(symtab_get_str(&rt.symtab, "L1$", &v1, &len1) && strcmp(v1, "First Line") == 0);
    assert(symtab_get_str(&rt.symtab, "L2$", &v2, &len2) && strcmp(v2, "Second Line") == 0);
    assert(symtab_get_str(&rt.symtab, "L3$", &v3, &len3) && strcmp(v3, "Third Line") == 0);

    unlink(src_file);
    unlink(dest_file);

    fclose(out);
    runtime_free(&rt);
    printf("test_move_persistence: PASS\n");
}

static void test_format_and_erase_recursive(void) {
    Runtime rt;
    runtime_init(&rt);
    FILE *out = tmpfile();
    assert(out != NULL);
    rt.out = out;

    // Get initial working directory
    char orig_cwd[1024];
    assert(getcwd(orig_cwd, sizeof(orig_cwd)) != NULL);

    // Test FORMAT directory creation and chdir
    runtime_process_input(&rt, "FORMAT \"temp_chan_dir/sub1/sub2\"", true);
    assert(rt.last_error.code == ERR_OK);

    // Check we changed directory
    char new_cwd[1024];
    assert(getcwd(new_cwd, sizeof(new_cwd)) != NULL);
    assert(strstr(new_cwd, "sub2") != NULL);

    // Create a file in current (nested) directory
    runtime_process_input(&rt, "OPEN #4, \"nested_data.txt\"", true);
    runtime_process_input(&rt, "PRINT #4, \"nested payload\"", true);
    runtime_process_input(&rt, "CLOSE #4", true);

    // Return to original cwd
    assert(chdir(orig_cwd) == 0);

    // Verify file exists at relative path
    assert(access("temp_chan_dir/sub1/sub2/nested_data.txt", F_OK) == 0);

    // Test ERASE recursive directory deletion
    runtime_process_input(&rt, "ERASE \"temp_chan_dir\"", true);
    assert(rt.last_error.code == ERR_OK);

    // Verify entire directory tree was removed
    assert(access("temp_chan_dir", F_OK) != 0);

    fclose(out);
    runtime_free(&rt);
    printf("test_format_and_erase_recursive: PASS\n");
}

static void test_structured_data_io(void) {
    Runtime rt;
    runtime_init(&rt);
    FILE *out = tmpfile();
    assert(out != NULL);
    rt.out = out;

    const char *data_file = "test_scores.dat";
    unlink(data_file);

    // Write structured fields
    runtime_process_input(&rt, "OPEN #4, \"test_scores.dat\"", true);
    runtime_process_input(&rt, "PRINT #4, \"Alice\", 9500", true);
    runtime_process_input(&rt, "PRINT #4, \"Bob\", 8200", true);
    runtime_process_input(&rt, "CLOSE #4", true);

    // Read back comma/tab separated fields
    runtime_process_input(&rt, "OPEN #4, \"test_scores.dat\"", true);
    runtime_process_input(&rt, "INPUT #4, N1$, S1", true);
    assert(rt.last_error.code == ERR_OK);
    runtime_process_input(&rt, "INPUT #4, N2$, S2", true);
    assert(rt.last_error.code == ERR_OK);
    runtime_process_input(&rt, "CLOSE #4", true);

    const char *n1_str = NULL, *n2_str = NULL;
    size_t n1_len = 0, n2_len = 0;
    double s1_val = 0, s2_val = 0;

    assert(symtab_get_str(&rt.symtab, "N1$", &n1_str, &n1_len) && strcmp(n1_str, "Alice") == 0);
    assert(symtab_get_num(&rt.symtab, "S1", &s1_val) && s1_val == 9500);
    assert(symtab_get_str(&rt.symtab, "N2$", &n2_str, &n2_len) && strcmp(n2_str, "Bob") == 0);
    assert(symtab_get_num(&rt.symtab, "S2", &s2_val) && s2_val == 8200);

    unlink(data_file);

    fclose(out);
    runtime_free(&rt);
    printf("test_structured_data_io: PASS\n");
}

static void test_clear_closes_channels(void) {
    Runtime rt;
    runtime_init(&rt);
    FILE *out = tmpfile();
    assert(out != NULL);
    rt.out = out;

    unlink("t_clr4.txt");
    unlink("t_clr5.txt");

    // Open channels
    runtime_process_input(&rt, "OPEN #4, \"t_clr4.txt\"", true);
    runtime_process_input(&rt, "OPEN #5, \"t_clr5.txt\"", true);
    assert(rt.channels[4].is_open);
    assert(rt.channels[5].is_open);

    // Run CLEAR
    runtime_process_input(&rt, "CLEAR", true);
    assert(rt.last_error.code == ERR_OK);

    // Assert channels are now closed
    assert(!rt.channels[4].is_open);
    assert(!rt.channels[5].is_open);

    unlink("t_clr4.txt");
    unlink("t_clr5.txt");

    fclose(out);
    runtime_free(&rt);
    printf("test_clear_closes_channels: PASS\n");
}

static void test_move_file_by_name(void) {
    Runtime rt;
    runtime_init(&rt);
    FILE *out = tmpfile();
    assert(out != NULL);
    rt.out = out;

    const char *f1 = "t_mv1.txt";
    const char *f2 = "t_mv2.txt";
    const char *f3 = "t_mv3.txt";
    unlink(f1);
    unlink(f2);
    unlink(f3);

    // Create f1
    runtime_process_input(&rt, "OPEN #4, \"t_mv1.txt\"", true);
    runtime_process_input(&rt, "PRINT #4, \"Rename Test Payload\"", true);
    runtime_process_input(&rt, "CLOSE #4", true);
    assert(access(f1, F_OK) == 0);
    assert(access(f2, F_OK) != 0);

    // MOVE f1 TO f2
    runtime_process_input(&rt, "MOVE \"t_mv1.txt\" TO \"t_mv2.txt\"", true);
    assert(rt.last_error.code == ERR_OK);
    assert(access(f1, F_OK) != 0);
    assert(access(f2, F_OK) == 0);

    // Read back from f2 to verify contents
    runtime_process_input(&rt, "OPEN #4, \"t_mv2.txt\"", true);
    runtime_process_input(&rt, "INPUT #4, A$", true);
    const char *str_val = NULL;
    size_t str_len = 0;
    assert(symtab_get_str(&rt.symtab, "A$", &str_val, &str_len));
    assert(strcmp(str_val, "Rename Test Payload") == 0);

    // Attempting to move an open file should fail with ERR_NONSENSE
    runtime_process_input(&rt, "MOVE \"t_mv2.txt\" TO \"t_mv3.txt\"", true);
    assert(rt.last_error.code == ERR_NONSENSE);
    assert(access(f2, F_OK) == 0);
    assert(access(f3, F_OK) != 0);

    // Close channel #4
    runtime_process_input(&rt, "CLOSE #4", true);

    // Attempting to move non-existent file should fail with ERR_NONSENSE
    runtime_process_input(&rt, "MOVE \"non_existent.txt\" TO \"t_mv3.txt\"", true);
    assert(rt.last_error.code == ERR_NONSENSE);

    // MOVE with string variables
    runtime_process_input(&rt, "LET S$ = \"t_mv2.txt\"", true);
    runtime_process_input(&rt, "LET D$ = \"t_mv3.txt\"", true);
    runtime_process_input(&rt, "MOVE S$ TO D$", true);
    assert(rt.last_error.code == ERR_OK);
    assert(access(f2, F_OK) != 0);
    assert(access(f3, F_OK) == 0);

    unlink(f1);
    unlink(f2);
    unlink(f3);

    fclose(out);
    runtime_free(&rt);
    printf("test_move_file_by_name: PASS\n");
}

static void test_copy_file_by_name(void) {
    Runtime rt;
    runtime_init(&rt);
    FILE *out = tmpfile();
    assert(out != NULL);
    rt.out = out;

    const char *f1 = "t_cp1.txt";
    const char *f2 = "t_cp2.txt";
    const char *f3 = "t_cp3.txt";
    const char *dir = "t_cp_dir";
    unlink(f1);
    unlink(f2);
    unlink(f3);
    unlink("t_cp_dir/t_cp1.txt");
    rmdir(dir);

    // Create f1
    runtime_process_input(&rt, "OPEN #4, \"t_cp1.txt\"", true);
    runtime_process_input(&rt, "PRINT #4, \"Copy Test Payload 12345\"", true);
    runtime_process_input(&rt, "CLOSE #4", true);
    assert(access(f1, F_OK) == 0);
    assert(access(f2, F_OK) != 0);

    // COPY f1 TO f2
    runtime_process_input(&rt, "COPY \"t_cp1.txt\" TO \"t_cp2.txt\"", true);
    assert(rt.last_error.code == ERR_OK);
    // Source still exists!
    assert(access(f1, F_OK) == 0);
    // Destination exists!
    assert(access(f2, F_OK) == 0);

    // Read back from f2 to verify contents
    runtime_process_input(&rt, "OPEN #4, \"t_cp2.txt\"", true);
    runtime_process_input(&rt, "INPUT #4, A$", true);
    const char *str_val = NULL;
    size_t str_len = 0;
    assert(symtab_get_str(&rt.symtab, "A$", &str_val, &str_len));
    assert(strcmp(str_val, "Copy Test Payload 12345") == 0);

    // Attempting to copy an open file should fail with ERR_NONSENSE
    runtime_process_input(&rt, "COPY \"t_cp2.txt\" TO \"t_cp3.txt\"", true);
    assert(rt.last_error.code == ERR_NONSENSE);
    assert(access(f3, F_OK) != 0);

    // Close channel #4
    runtime_process_input(&rt, "CLOSE #4", true);

    // Attempting to copy non-existent file should fail with ERR_NONSENSE
    runtime_process_input(&rt, "COPY \"non_existent.txt\" TO \"t_cp3.txt\"", true);
    assert(rt.last_error.code == ERR_NONSENSE);

    // Copying into an existing directory
    assert(mkdir(dir, 0755) == 0);
    runtime_process_input(&rt, "COPY \"t_cp1.txt\" TO \"t_cp_dir\"", true);
    assert(rt.last_error.code == ERR_OK);
    assert(access("t_cp_dir/t_cp1.txt", F_OK) == 0);

    // COPY with string variables
    runtime_process_input(&rt, "LET S$ = \"t_cp1.txt\"", true);
    runtime_process_input(&rt, "LET D$ = \"t_cp3.txt\"", true);
    runtime_process_input(&rt, "COPY S$ TO D$", true);
    assert(rt.last_error.code == ERR_OK);
    assert(access(f1, F_OK) == 0);
    assert(access(f3, F_OK) == 0);

    unlink("t_cp_dir/t_cp1.txt");
    rmdir(dir);
    unlink(f1);
    unlink(f2);
    unlink(f3);

    fclose(out);
    runtime_free(&rt);
    printf("test_copy_file_by_name: PASS\n");
}

static void test_normalization_and_syntax(void) {
    // Normalization test
    char *n1 = normalize_keywords("open #4, \"f\": close #4, #5: move #4 to #5: format \"d\"");
    assert(strcmp(n1, "OPEN #4, \"f\": CLOSE #4, #5: MOVE #4 TO #5: FORMAT \"d\"") == 0);
    free(n1);

    char *n2 = normalize_keywords("print #4, \"test\": input #4, a$, b");
    assert(strcmp(n2, "PRINT #4, \"test\": INPUT #4, a$, b") == 0);
    free(n2);

    char *n3 = normalize_keywords("move \"file1\" to \"file2\"");
    assert(strcmp(n3, "MOVE \"file1\" TO \"file2\"") == 0);
    free(n3);

    char *n4 = normalize_keywords("copy \"file1\" to \"file2\"");
    assert(strcmp(n4, "COPY \"file1\" TO \"file2\"") == 0);
    free(n4);

    // Syntax validation
    size_t err_col = 0;
    assert(syntax_validate_line("OPEN #4, \"test.dat\"", &err_col));
    assert(syntax_validate_line("CLOSE #4", &err_col));
    assert(syntax_validate_line("CLOSE #4, #5, #6", &err_col));
    assert(syntax_validate_line("PRINT #4, \"hello\", 123", &err_col));
    assert(syntax_validate_line("INPUT #4, A$, B", &err_col));
    assert(syntax_validate_line("MOVE #4 TO #5", &err_col));
    assert(syntax_validate_line("MOVE \"file1.txt\" TO \"file2.txt\"", &err_col));
    assert(syntax_validate_line("MOVE A$ TO B$", &err_col));
    assert(syntax_validate_line("COPY #4 TO #5", &err_col));
    assert(syntax_validate_line("COPY \"file1.txt\" TO \"file2.txt\"", &err_col));
    assert(syntax_validate_line("COPY A$ TO B$", &err_col));
    assert(syntax_validate_line("FORMAT \"dir/path\"", &err_col));
    assert(syntax_validate_line("ERASE \"dir/path\"", &err_col));

    // Rejection of malformed syntax
    assert(!syntax_validate_line("OPEN 4, \"file\"", &err_col));
    assert(!syntax_validate_line("MOVE #4", &err_col));
    assert(!syntax_validate_line("MOVE #4 TO 5", &err_col));
    assert(!syntax_validate_line("MOVE \"file1.txt\"", &err_col));
    assert(!syntax_validate_line("COPY #4", &err_col));
    assert(!syntax_validate_line("COPY #4 TO 5", &err_col));
    assert(!syntax_validate_line("COPY \"file1.txt\"", &err_col));
    assert(!syntax_validate_line("CLOSE", &err_col));

    printf("test_normalization_and_syntax: PASS\n");
}

int main(void) {
    test_open_mode_disambiguation();
    test_close_multiple_channels();
    test_clear_closes_channels();
    test_move_persistence();
    test_move_file_by_name();
    test_copy_file_by_name();
    test_format_and_erase_recursive();
    test_structured_data_io();
    test_normalization_and_syntax();
    printf("All channel and stream tests passed successfully!\n");
    return 0;
}

