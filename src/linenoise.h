#ifndef ZXBASIC_LINENOISE_H
#define ZXBASIC_LINENOISE_H

#ifdef __cplusplus
extern "C" {
#endif

// Reads a line from the terminal interactively with line editing and history.
// Returns a dynamically allocated string (caller must free), or NULL on EOF / Ctrl+C.
char *linenoise(const char *prompt);

// Pre-populates the input buffer for the next linenoise() prompt.
void linenoisePreloadBuffer(const char *buf);

// History functions
int linenoiseHistoryAdd(const char *line);
void linenoiseHistoryFree(void);

#ifdef __cplusplus
}
#endif

#endif // ZXBASIC_LINENOISE_H
