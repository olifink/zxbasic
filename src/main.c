#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "runtime.h"

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

    char line_buf[2048];
    while (true) {
        if (is_interactive) {
            printf("> ");
            fflush(stdout);
        }

        if (!fgets(line_buf, sizeof(line_buf), stdin)) {
            break; // EOF
        }

        runtime_process_input(&rt, line_buf, is_interactive);
    }

    runtime_free(&rt);
    return 0;
}
