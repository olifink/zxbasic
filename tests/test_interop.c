#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <sys/stat.h>

#include "../src/runtime.h"
#include "../src/normalize.h"

static void test_shebang_ingestion(void) {
    Runtime rt;
    runtime_init(&rt);
    FILE *out = tmpfile();
    rt.out = out;

    const char *test_script = "test_shebang.bas";
    unlink(test_script);

    // Create file with shebang line at offset 0
    FILE *f = fopen(test_script, "w");
    assert(f != NULL);
    fprintf(f, "#!/usr/bin/env zxbasic\n");
    fprintf(f, "10 LET X = 42\n");
    fprintf(f, "20 PRINT X\n");
    fclose(f);

    // Load file: shebang line should be completely ignored
    bool ok = runtime_load(&rt, test_script);
    assert(ok);
    assert(rt.last_error.code == ERR_OK);

    // Program should only have 2 lines (10 and 20), no shebang
    assert(rt.program.count == 2);
    assert(rt.program.lines[0].line_no == 10);
    assert(rt.program.lines[1].line_no == 20);

    // Run the loaded program
    runtime_run(&rt, 0);
    assert(rt.last_error.code == ERR_OK);

    rewind(out);
    char buf[256];
    assert(fgets(buf, sizeof(buf), out) != NULL);
    assert(strstr(buf, "42") != NULL);

    // Test MERGE with a shebang file
    const char *merge_script = "test_shebang_merge.bas";
    unlink(merge_script);
    f = fopen(merge_script, "w");
    assert(f != NULL);
    fprintf(f, "#!/bin/bash\n");
    fprintf(f, "30 PRINT \"MERGED\"\n");
    fclose(f);

    ok = runtime_merge(&rt, merge_script);
    assert(ok);
    assert(rt.program.count == 3);
    assert(rt.program.lines[2].line_no == 30);

    unlink(test_script);
    unlink(merge_script);
    fclose(out);
    runtime_free(&rt);
    printf("test_shebang_ingestion: PASS\n");
}

static void test_save_code_export(void) {
    Runtime rt;
    runtime_init(&rt);
    FILE *out = tmpfile();
    rt.out = out;

    const char *exec_script = "test_generated_script.bas";
    unlink(exec_script);

    runtime_process_input(&rt, "10 PRINT \"EXECUTABLE HELLO\"", true);
    runtime_process_input(&rt, "20 PRINT 100 * 2", true);

    // SAVE "file" CODE
    runtime_process_input(&rt, "SAVE \"test_generated_script.bas\" CODE", true);
    assert(rt.last_error.code == ERR_OK);

    // Verify file exists
    assert(access(exec_script, F_OK) == 0);

    // Verify executable permissions
    struct stat st;
    assert(stat(exec_script, &st) == 0);
    assert(st.st_mode & S_IXUSR);

    // Verify content: line 1 must be shebang
    FILE *f = fopen(exec_script, "r");
    assert(f != NULL);
    char line1[128];
    assert(fgets(line1, sizeof(line1), f) != NULL);
    assert(strcmp(line1, "#!/usr/bin/env zxbasic\n") == 0);

    char line2[128];
    assert(fgets(line2, sizeof(line2), f) != NULL);
    assert(strstr(line2, "10 PRINT \"EXECUTABLE HELLO\"") != NULL);
    fclose(f);

    // Test SAVE "file"CODE (no whitespace between quote and CODE)
    const char *exec_script2 = "test_generated_script2.bas";
    unlink(exec_script2);
    runtime_process_input(&rt, "SAVE \"test_generated_script2.bas\"CODE", true);
    assert(rt.last_error.code == ERR_OK);
    assert(stat(exec_script2, &st) == 0);
    assert(st.st_mode & S_IXUSR);

    unlink(exec_script);
    unlink(exec_script2);
    fclose(out);
    runtime_free(&rt);
    printf("test_save_code_export: PASS\n");
}

static void test_stdio_channel_mappings(void) {
    Runtime rt;
    runtime_init(&rt);
    FILE *out = tmpfile();
    FILE *err = tmpfile();
    FILE *in = tmpfile();
    rt.out = out;
    rt.err = err;
    rt.in = in;

    // 1. Unadorned PRINT emits to channel 1 (stdout / rt.out)
    runtime_process_input(&rt, "PRINT \"Stdout Default\"", false);
    assert(rt.last_error.code == ERR_OK);

    rewind(out);
    char buf[512];
    assert(fgets(buf, sizeof(buf), out) != NULL);
    assert(strstr(buf, "Stdout Default") != NULL);

    // Non-interactive execution generates no status report on stderr
    rewind(err);
    assert(fgets(buf, sizeof(buf), err) == NULL);

    // In interactive mode, status reports go to channel 2 (stderr)
    rewind(err);
    ftruncate(fileno(err), 0);
    runtime_process_input(&rt, "PRINT \"Interactive Run\"", true);
    rewind(err);
    assert(fgets(buf, sizeof(buf), err) != NULL);
    assert(strstr(buf, "0 OK") != NULL);

    // 2. Explicit PRINT #1 emits to stdout (rt.out)
    fclose(out);
    out = tmpfile();
    rt.out = out;
    runtime_process_input(&rt, "PRINT #1; \"Explicit Stdout\"", false);
    assert(rt.last_error.code == ERR_OK);

    rewind(out);
    assert(fgets(buf, sizeof(buf), out) != NULL);
    assert(strstr(buf, "Explicit Stdout") != NULL);

    // 3. Explicit PRINT #2 emits to stderr (rt.err)
    fclose(err);
    err = tmpfile();
    rt.err = err;
    fclose(out);
    out = tmpfile();
    rt.out = out;
    runtime_process_input(&rt, "PRINT #2; \"Explicit Stderr Log\"", false);
    assert(rt.last_error.code == ERR_OK);

    rewind(err);
    assert(fgets(buf, sizeof(buf), err) != NULL);
    assert(strstr(buf, "Explicit Stderr Log") != NULL);

    // Ensure stdout received nothing from PRINT #2
    rewind(out);
    assert(fgets(buf, sizeof(buf), out) == NULL);

    // 4. Reports go to channel 2 (stderr)
    fclose(err);
    err = tmpfile();
    rt.err = err;
    runtime_process_input(&rt, "10 STOP", false);
    runtime_run(&rt, 0);

    rewind(err);
    buf[0] = '\0';
    fread(buf, 1, sizeof(buf) - 1, err);
    assert(strstr(buf, "9 STOP statement") != NULL);

    // 5. INPUT defaults to channel 0 (stdin)
    fputs("Antigravity\n", in);
    rewind(in);

    runtime_process_input(&rt, "INPUT A$", true);
    assert(rt.last_error.code == ERR_OK);
    const char *str_val = NULL;
    size_t str_len = 0;
    assert(symtab_get_str(&rt.symtab, "A$", &str_val, &str_len));
    assert(strcmp(str_val, "Antigravity") == 0);

    // 6. Explicit INPUT #0 reads from stdin
    fclose(in);
    in = tmpfile();
    rt.in = in;
    fputs("StreamZero\n", in);
    rewind(in);

    runtime_process_input(&rt, "INPUT #0, B$", true);
    assert(rt.last_error.code == ERR_OK);
    assert(symtab_get_str(&rt.symtab, "B$", &str_val, &str_len));
    assert(strcmp(str_val, "StreamZero") == 0);

    // 7. Mode constraints & error conditions
    // Cannot PRINT to channel 0 (read-only stdin)
    runtime_process_input(&rt, "PRINT #0; \"Bad\"", true);
    assert(rt.last_error.code == ERR_NONSENSE);

    // Cannot INPUT from channel 1 (write-only stdout)
    runtime_process_input(&rt, "INPUT #1, C$", true);
    assert(rt.last_error.code == ERR_NONSENSE);

    // Cannot INPUT from channel 2 (write-only stderr)
    runtime_process_input(&rt, "INPUT #2, D$", true);
    assert(rt.last_error.code == ERR_NONSENSE);

    // Cannot CLOSE channels 0, 1, or 2
    runtime_process_input(&rt, "CLOSE #0", true);
    assert(rt.last_error.code == ERR_NONSENSE);
    runtime_process_input(&rt, "CLOSE #1", true);
    assert(rt.last_error.code == ERR_NONSENSE);
    runtime_process_input(&rt, "CLOSE #2", true);
    assert(rt.last_error.code == ERR_NONSENSE);

    // Cannot OPEN channels 0, 1, or 2
    runtime_process_input(&rt, "OPEN #0, \"f0.txt\"", true);
    assert(rt.last_error.code == ERR_INTEGER_RANGE);
    runtime_process_input(&rt, "OPEN #1, \"f1.txt\"", true);
    assert(rt.last_error.code == ERR_INTEGER_RANGE);
    runtime_process_input(&rt, "OPEN #2, \"f2.txt\"", true);
    assert(rt.last_error.code == ERR_INTEGER_RANGE);

    // CLEAR closes user channels but leaves 0, 1, 2 open
    runtime_process_input(&rt, "CLEAR", true);
    assert(rt.channels[0].is_open);
    assert(rt.channels[1].is_open);
    assert(rt.channels[2].is_open);

    fclose(out);
    fclose(err);
    fclose(in);
    runtime_free(&rt);
    printf("test_stdio_channel_mappings: PASS\n");
}

static void test_run_subprocess_execution(void) {
    Runtime rt;
    runtime_init(&rt);
    FILE *out = tmpfile();
    FILE *err = tmpfile();
    rt.out = out;
    rt.err = err;

    // 1. Spawning host executable directly: echo into channel
    const char *out_file = "test_subp_out.txt";
    unlink(out_file);

    runtime_process_input(&rt, "OPEN #5, \"test_subp_out.txt\"", true);
    assert(rt.channels[5].is_open);

    // RUN "echo hello" TO , #5
    runtime_process_input(&rt, "RUN \"echo HostProcessSuccess\" TO , #5", true);
    assert(rt.last_error.code == ERR_OK);

    // Check exit status sysvar
    int64_t status_val = -1;
    assert(sysvar_get_int(&rt.sysvars, "STATUS", &status_val));
    assert(status_val == 0);

    // Close channel and verify file content
    runtime_process_input(&rt, "CLOSE #5", true);
    FILE *rf = fopen(out_file, "r");
    assert(rf != NULL);
    char buf[256];
    assert(fgets(buf, sizeof(buf), rf) != NULL);
    assert(strstr(buf, "HostProcessSuccess") != NULL);
    fclose(rf);
    unlink(out_file);

    // 2. Capture non-zero exit status
    runtime_process_input(&rt, "RUN \"/bin/sh -c 'exit 15'\"", true);
    assert(rt.last_error.code == ERR_OK);
    assert(sysvar_get_int(&rt.sysvars, "STATUS", &status_val));
    assert(status_val == 15);

    // 3. Verify in-memory program & variables are preserved after subprocess execution
    runtime_process_input(&rt, "10 PRINT \"In Program\"", true);
    runtime_process_input(&rt, "LET MYVAR = 99", true);
    runtime_process_input(&rt, "RUN \"echo Preserved\"", true);
    assert(rt.program.count == 1);
    assert(rt.program.lines[0].line_no == 10);
    double myvar = 0;
    assert(symtab_get_num(&rt.symtab, "MYVAR", &myvar) && myvar == 99.0);

    // 4. Pipeline redirection: feed input from channel 4, capture stdout in channel 5
    const char *sort_in = "test_sort_in.txt";
    const char *sort_out = "test_sort_out.txt";
    unlink(sort_in);
    unlink(sort_out);

    FILE *sin = fopen(sort_in, "w");
    assert(sin != NULL);
    fprintf(sin, "zebra\napple\nmango\n");
    fclose(sin);

    runtime_process_input(&rt, "OPEN #4, \"test_sort_in.txt\"", true);
    runtime_process_input(&rt, "OPEN #5, \"test_sort_out.txt\"", true);
    assert(rt.channels[4].mode == CHANNEL_MODE_READ);
    assert(rt.channels[5].mode == CHANNEL_MODE_WRITE);

    runtime_process_input(&rt, "RUN \"sort\" TO #4, #5", true);
    assert(rt.last_error.code == ERR_OK);
    assert(sysvar_get_int(&rt.sysvars, "STATUS", &status_val) && status_val == 0);

    runtime_process_input(&rt, "CLOSE #4, #5", true);

    FILE *sout = fopen(sort_out, "r");
    assert(sout != NULL);
    char l1[64], l2[64], l3[64];
    assert(fgets(l1, sizeof(l1), sout) != NULL);
    assert(fgets(l2, sizeof(l2), sout) != NULL);
    assert(fgets(l3, sizeof(l3), sout) != NULL);
    assert(strncmp(l1, "apple", 5) == 0);
    assert(strncmp(l2, "mango", 5) == 0);
    assert(strncmp(l3, "zebra", 5) == 0);
    fclose(sout);

    unlink(sort_in);
    unlink(sort_out);

    // 5. Error checking: referencing closed channel raises ERR_NONSENSE
    runtime_process_input(&rt, "RUN \"echo Fail\" TO #4", true);
    assert(rt.last_error.code == ERR_NONSENSE);

    // Referencing write-only channel as stdin raises ERR_NONSENSE
    runtime_process_input(&rt, "RUN \"echo Fail\" TO #1", true);
    assert(rt.last_error.code == ERR_NONSENSE);

    // 6. Local executable without ./ prefix executes without 'not found' error
    const char *local_exec = "test_local_runner.sh";
    unlink(local_exec);
    FILE *lex = fopen(local_exec, "w");
    assert(lex != NULL);
    fprintf(lex, "#!/bin/sh\necho \"LOCAL_OK\"\n");
    fclose(lex);
    chmod(local_exec, 0755);

    runtime_process_input(&rt, "OPEN #5, \"test_local_out.txt\"", true);
    runtime_process_input(&rt, "RUN \"test_local_runner.sh\" TO , #5", true);
    assert(rt.last_error.code == ERR_OK);
    runtime_process_input(&rt, "CLOSE #5", true);

    FILE *lout = fopen("test_local_out.txt", "r");
    assert(lout != NULL);
    assert(fgets(buf, sizeof(buf), lout) != NULL);
    assert(strstr(buf, "LOCAL_OK") != NULL);
    fclose(lout);
    unlink(local_exec);
    unlink("test_local_out.txt");

    fclose(out);
    fclose(err);
    runtime_free(&rt);
    printf("test_run_subprocess_execution: PASS\n");
}

static void test_run_basic_redirection(void) {
    Runtime rt;
    runtime_init(&rt);
    FILE *out = tmpfile();
    FILE *err = tmpfile();
    rt.out = out;
    rt.err = err;

    // Create a non-executable BASIC program
    const char *sub_bas = "test_subprogram.bas";
    unlink(sub_bas);

    FILE *f = fopen(sub_bas, "w");
    assert(f != NULL);
    fprintf(f, "10 INPUT S$\n");
    fprintf(f, "20 PRINT \"CHILD_PROCESSED: \"; S$\n");
    fclose(f);

    // Ensure it is NOT executable
    chmod(sub_bas, 0644);

    const char *data_in = "test_data_in.txt";
    const char *data_out = "test_data_out.txt";
    unlink(data_in);
    unlink(data_out);

    FILE *di = fopen(data_in, "w");
    assert(di != NULL);
    fprintf(di, "AntigravityCore\n");
    fclose(di);

    runtime_process_input(&rt, "OPEN #4, \"test_data_in.txt\"", true);
    runtime_process_input(&rt, "OPEN #5, \"test_data_out.txt\"", true);

    // Chain-load BASIC program with redirected channels
    runtime_process_input(&rt, "RUN \"test_subprogram.bas\" TO #4, #5", true);
    assert(rt.last_error.code == ERR_OK);

    runtime_process_input(&rt, "CLOSE #4, #5", true);

    FILE *dout = fopen(data_out, "r");
    assert(dout != NULL);
    char buf[256];
    assert(fgets(buf, sizeof(buf), dout) != NULL);
    assert(strstr(buf, "CHILD_PROCESSED: AntigravityCore") != NULL);
    fclose(dout);

    unlink(sub_bas);
    unlink(data_in);
    unlink(data_out);
    fclose(out);
    fclose(err);
    runtime_free(&rt);
    printf("test_run_basic_redirection: PASS\n");
}

static void test_interop_normalization_and_syntax(void) {
    size_t col = 0;

    // SAVE CODE normalization & syntax
    char *n1 = normalize_keywords("save \"script.bas\" code");
    assert(strcmp(n1, "SAVE \"script.bas\" CODE") == 0);
    assert(syntax_validate_line(n1, &col));
    free(n1);

    // RUN TO normalization & syntax
    char *n2 = normalize_keywords("run \"sort\" to #4, #5");
    assert(strcmp(n2, "RUN \"sort\" TO #4, #5") == 0);
    assert(syntax_validate_line(n2, &col));
    free(n2);

    char *n3 = normalize_keywords("run \"sort\" to #4, #5, #2");
    assert(strcmp(n3, "RUN \"sort\" TO #4, #5, #2") == 0);
    assert(syntax_validate_line(n3, &col));
    free(n3);

    char *n4 = normalize_keywords("run \"sort\" to , #5");
    assert(strcmp(n4, "RUN \"sort\" TO , #5") == 0);
    assert(syntax_validate_line(n4, &col));
    free(n4);

    char *n5 = normalize_keywords("run \"sort\" to 4, 5");
    assert(strcmp(n5, "RUN \"sort\" TO 4, 5") == 0);
    assert(syntax_validate_line(n5, &col));
    free(n5);

    // PRINT and INPUT stream channels
    char *n6 = normalize_keywords("print #1; \"hello\"");
    assert(strcmp(n6, "PRINT #1; \"hello\"") == 0);
    assert(syntax_validate_line(n6, &col));
    free(n6);

    char *n7 = normalize_keywords("print #2; \"err\"");
    assert(strcmp(n7, "PRINT #2; \"err\"") == 0);
    assert(syntax_validate_line(n7, &col));
    free(n7);

    char *n8 = normalize_keywords("input #0, a$");
    assert(strcmp(n8, "INPUT #0, a$") == 0);
    assert(syntax_validate_line(n8, &col));
    free(n8);

    printf("test_interop_normalization_and_syntax: PASS\n");
}

int main(void) {
    test_shebang_ingestion();
    test_save_code_export();
    test_stdio_channel_mappings();
    test_run_subprocess_execution();
    test_run_basic_redirection();
    test_interop_normalization_and_syntax();
    printf("All POSIX interoperability tests passed successfully!\n");
    return 0;
}
