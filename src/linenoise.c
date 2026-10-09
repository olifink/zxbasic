#define _POSIX_C_SOURCE 200809L

#include "linenoise.h"
#include <termios.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <stdbool.h>

#define LINENOISE_MAX_LINE 4096
#define LINENOISE_HISTORY_MAX 100

static struct termios orig_termios;
static bool rawmode = false;
static int atexit_registered = 0;
static int history_len = 0;
static char **history = NULL;
static char *preload_buffer = NULL;

static void disable_raw_mode(int fd) {
    if (rawmode) {
        tcsetattr(fd, TCSAFLUSH, &orig_termios);
        rawmode = false;
    }
}

static void linenoise_atexit(void) {
    disable_raw_mode(STDIN_FILENO);
    linenoiseHistoryFree();
    free(preload_buffer);
    preload_buffer = NULL;
}

static int enable_raw_mode(int fd) {
    if (!isatty(STDIN_FILENO)) return -1;
    if (!atexit_registered) {
        atexit(linenoise_atexit);
        atexit_registered = 1;
    }

    if (tcgetattr(fd, &orig_termios) == -1) return -1;

    struct termios raw = orig_termios;
    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    raw.c_oflag &= ~(OPOST);
    raw.c_cflag |= (CS8);
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;

    if (tcsetattr(fd, TCSAFLUSH, &raw) < 0) return -1;
    rawmode = true;
    return 0;
}

void linenoisePreloadBuffer(const char *buf) {
    free(preload_buffer);
    preload_buffer = buf ? strdup(buf) : NULL;
}

int linenoiseHistoryAdd(const char *line) {
    if (!line || line[0] == '\0') return 0;

    // Do not add duplicate of previous entry
    if (history_len > 0 && strcmp(history[history_len - 1], line) == 0) {
        return 0;
    }

    char *linecopy = strdup(line);
    if (!linecopy) return 0;

    if (history_len == LINENOISE_HISTORY_MAX) {
        free(history[0]);
        memmove(history, history + 1, sizeof(char *) * (LINENOISE_HISTORY_MAX - 1));
        history_len--;
    }

    char **new_hist = realloc(history, sizeof(char *) * (history_len + 1));
    if (!new_hist) {
        free(linecopy);
        return 0;
    }
    history = new_hist;
    history[history_len++] = linecopy;
    return 1;
}

void linenoiseHistoryFree(void) {
    if (history) {
        for (int j = 0; j < history_len; j++) {
            free(history[j]);
        }
        free(history);
        history = NULL;
        history_len = 0;
    }
}

static void refresh_line(int fd, const char *prompt, const char *buf, size_t len, size_t pos) {
    char seq[64];
    // Cursor to left edge
    if (write(fd, "\r", 1) == -1) return;
    // Write prompt
    if (write(fd, prompt, strlen(prompt)) == -1) return;
    // Write buffer
    if (write(fd, buf, len) == -1) return;
    // Erase to right
    if (write(fd, "\x1b[0K", 4) == -1) return;
    // Move cursor to original pos
    snprintf(seq, sizeof(seq), "\r\x1b[%dC", (int)(strlen(prompt) + pos));
    if (write(fd, seq, strlen(seq)) == -1) return;
}

static int edit_line(int fd, const char *prompt, char *buf, size_t maxlen) {
    size_t len = 0;
    size_t pos = 0;

    buf[0] = '\0';

    if (preload_buffer) {
        size_t plen = strlen(preload_buffer);
        if (plen >= maxlen) plen = maxlen - 1;
        memcpy(buf, preload_buffer, plen);
        buf[plen] = '\0';
        len = plen;
        pos = plen;
        free(preload_buffer);
        preload_buffer = NULL;
    }

    int history_index = history_len;

    refresh_line(fd, prompt, buf, len, pos);

    while (true) {
        char c;
        ssize_t nread = read(fd, &c, 1);
        if (nread <= 0) {
            if (nread < 0 && (errno == EINTR || errno == EAGAIN)) {
                errno = EAGAIN;
                return -1;
            }
            if (len == 0) {
                errno = 0;
                return -1;
            }
            return (int)len;
        }

        if (c == '\r' || c == '\n') {
            return (int)len;
        } else if (c == 3) { // Ctrl+C
            errno = EAGAIN;
            return -1;
        } else if (c == 4) { // Ctrl+D
            if (len == 0) {
                errno = 0;
                return -1;
            }
            // Delete character under cursor
            if (pos < len) {
                memmove(buf + pos, buf + pos + 1, len - pos);
                len--;
                refresh_line(fd, prompt, buf, len, pos);
            }
        } else if (c == 127 || c == 8) { // Backspace
            if (pos > 0 && len > 0) {
                memmove(buf + pos - 1, buf + pos, len - pos);
                pos--;
                len--;
                buf[len] = '\0';
                refresh_line(fd, prompt, buf, len, pos);
            }
        } else if (c == 27) { // Escape sequence
            char seq[3];
            if (read(fd, seq, 1) <= 0) continue;
            if (read(fd, seq + 1, 1) <= 0) continue;

            if (seq[0] == '[') {
                if (seq[1] >= '0' && seq[1] <= '9') {
                    // Extended escape, read trailing byte
                    char trail;
                    if (read(fd, &trail, 1) <= 0) continue;
                    if (seq[1] == '3' && trail == '~') { // Delete
                        if (pos < len) {
                            memmove(buf + pos, buf + pos + 1, len - pos);
                            len--;
                            buf[len] = '\0';
                            refresh_line(fd, prompt, buf, len, pos);
                        }
                    } else if (seq[1] == '1' || seq[1] == '7') { // Home
                        pos = 0;
                        refresh_line(fd, prompt, buf, len, pos);
                    } else if (seq[1] == '4' || seq[1] == '8') { // End
                        pos = len;
                        refresh_line(fd, prompt, buf, len, pos);
                    }
                } else {
                    switch (seq[1]) {
                        case 'A': // Up arrow (history prev)
                            if (history_len > 0 && history_index > 0) {
                                history_index--;
                                strncpy(buf, history[history_index], maxlen - 1);
                                buf[maxlen - 1] = '\0';
                                len = pos = strlen(buf);
                                refresh_line(fd, prompt, buf, len, pos);
                            }
                            break;
                        case 'B': // Down arrow (history next)
                            if (history_index < history_len) {
                                history_index++;
                                if (history_index < history_len) {
                                    strncpy(buf, history[history_index], maxlen - 1);
                                    buf[maxlen - 1] = '\0';
                                } else {
                                    buf[0] = '\0';
                                }
                                len = pos = strlen(buf);
                                refresh_line(fd, prompt, buf, len, pos);
                            }
                            break;
                        case 'C': // Right arrow
                            if (pos < len) {
                                pos++;
                                refresh_line(fd, prompt, buf, len, pos);
                            }
                            break;
                        case 'D': // Left arrow
                            if (pos > 0) {
                                pos--;
                                refresh_line(fd, prompt, buf, len, pos);
                            }
                            break;
                        case 'H': // Home
                            pos = 0;
                            refresh_line(fd, prompt, buf, len, pos);
                            break;
                        case 'F': // End
                            pos = len;
                            refresh_line(fd, prompt, buf, len, pos);
                            break;
                    }
                }
            }
        } else if (c >= 32 && (unsigned char)c < 127) {
            // Printable character
            if (len < maxlen - 1) {
                if (pos < len) {
                    memmove(buf + pos + 1, buf + pos, len - pos);
                }
                buf[pos] = c;
                pos++;
                len++;
                buf[len] = '\0';
                refresh_line(fd, prompt, buf, len, pos);
            }
        }
    }
}

char *linenoise(const char *prompt) {
    errno = 0;
    if (!isatty(STDIN_FILENO)) {
        char buf[LINENOISE_MAX_LINE];
        if (!fgets(buf, sizeof(buf), stdin)) return NULL;
        size_t len = strlen(buf);
        while (len > 0 && (buf[len - 1] == '\r' || buf[len - 1] == '\n')) {
            buf[--len] = '\0';
        }
        return strdup(buf);
    }

    if (enable_raw_mode(STDIN_FILENO) == -1) {
        char buf[LINENOISE_MAX_LINE];
        if (!fgets(buf, sizeof(buf), stdin)) return NULL;
        return strdup(buf);
    }

    char buf[LINENOISE_MAX_LINE];
    int count = edit_line(STDIN_FILENO, prompt, buf, sizeof(buf));
    disable_raw_mode(STDIN_FILENO);
    printf("\n");

    if (count == -1) return NULL;
    return strdup(buf);
}
