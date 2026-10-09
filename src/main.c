#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include "runtime.h"
#include "linenoise.h"

int main(int argc, char **argv) {
    Runtime rt;
    runtime_init(&rt);

    if (argc > 1) {
        // Execute file passed as argument
        const char *filepath = argv[1];
        if (!runtime_load(&rt, filepath)) {
            fprintf(stderr, "Error loading file: %s\n", filepath);
            runtime_free(&rt);
            return 1;
        }
        runtime_run(&rt, 0);
        int exit_code = (rt.last_error.code == ERR_OK || rt.last_error.code == ERR_STOP) ? 0 : 1;
        runtime_free(&rt);
        return exit_code;
    }

    bool is_interactive = isatty(STDIN_FILENO);

    if (is_interactive) {
        printf("zxbasic (Modern Sinclair BASIC for POSIX)\n");
        printf("Type commands or enter numbered lines. RUN to execute.\n\n");
    }

    while (true) {
        char prompt[64];
        if (rt.auto_mode) {
            bool exists = (program_find_index(&rt.program, rt.auto_current_line) >= 0);
            snprintf(prompt, sizeof(prompt), "%u%s ", (unsigned int)rt.auto_current_line, exists ? "*" : "");
        } else {
            snprintf(prompt, sizeof(prompt), "> ");
        }

        char *line = NULL;
        if (is_interactive) {
            if (rt.edit_prefill_buffer) {
                linenoisePreloadBuffer(rt.edit_prefill_buffer);
                free(rt.edit_prefill_buffer);
                rt.edit_prefill_buffer = NULL;
            }
            line = linenoise(prompt);
            if (!line) break; // EOF or Ctrl+C
            if (line[0] != '\0') {
                linenoiseHistoryAdd(line);
            }
        } else {
            char buf[4096];
            if (!fgets(buf, sizeof(buf), stdin)) {
                break;
            }
            size_t blen = strlen(buf);
            while (blen > 0 && (buf[blen - 1] == '\r' || buf[blen - 1] == '\n')) {
                buf[--blen] = '\0';
            }
            line = strdup(buf);
        }

        if (rt.auto_mode) {
            const char *p = line;
            while (*p && isspace((unsigned char)*p)) p++;
            if (*p == '\0') {
                rt.auto_mode = false;
                free(line);
                continue;
            }

            char full_line[4200];
            snprintf(full_line, sizeof(full_line), "%u %s", (unsigned int)rt.auto_current_line, p);
            runtime_process_input(&rt, full_line, is_interactive);
            if ((uint64_t)rt.auto_current_line + rt.auto_step > MAX_LINE_NO) {
                rt.auto_mode = false;
            } else {
                rt.auto_current_line += rt.auto_step;
            }
        } else {
            runtime_process_input(&rt, line, is_interactive);
        }

        free(line);
    }

    runtime_free(&rt);
    return 0;
}
