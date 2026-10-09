#define _POSIX_C_SOURCE 200809L

#include "lexer.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdbool.h>

void token_free(Token *t) {
    if (!t) return;
    if (t->str_val) {
        free(t->str_val);
        t->str_val = NULL;
    }
    t->str_len = 0;
}

const char *token_type_name(TokenType t) {
    switch (t) {
        case TOKEN_EOF: return "EOF";
        case TOKEN_COLON: return ":";
        case TOKEN_LPAREN: return "(";
        case TOKEN_RPAREN: return ")";
        case TOKEN_COMMA: return ",";
        case TOKEN_SEMICOLON: return ";";
        case TOKEN_APOSTROPHE: return "'";
        case TOKEN_PLUS: return "+";
        case TOKEN_MINUS: return "-";
        case TOKEN_STAR: return "*";
        case TOKEN_SLASH: return "/";
        case TOKEN_CARET: return "^";
        case TOKEN_EQUAL: return "=";
        case TOKEN_NOT_EQUAL: return "<>";
        case TOKEN_LESS: return "<";
        case TOKEN_GREATER: return ">";
        case TOKEN_LESS_EQUAL: return "<=";
        case TOKEN_GREATER_EQUAL: return ">=";
        case TOKEN_AND: return "AND";
        case TOKEN_OR: return "OR";
        case TOKEN_NOT: return "NOT";
        case TOKEN_NUMBER: return "NUMBER";
        case TOKEN_STRING: return "STRING";
        case TOKEN_IDENT: return "IDENT";
        case TOKEN_LET: return "LET";
        case TOKEN_PRINT: return "PRINT";
        case TOKEN_INPUT: return "INPUT";
        case TOKEN_GOTO: return "GOTO";
        case TOKEN_GOSUB: return "GOSUB";
        case TOKEN_RETURN: return "RETURN";
        case TOKEN_IF: return "IF";
        case TOKEN_THEN: return "THEN";
        case TOKEN_FOR: return "FOR";
        case TOKEN_TO: return "TO";
        case TOKEN_STEP: return "STEP";
        case TOKEN_NEXT: return "NEXT";
        case TOKEN_DATA: return "DATA";
        case TOKEN_READ: return "READ";
        case TOKEN_RESTORE: return "RESTORE";
        case TOKEN_DIM: return "DIM";
        case TOKEN_STOP: return "STOP";
        case TOKEN_REM: return "REM";
        case TOKEN_RUN: return "RUN";
        case TOKEN_LIST: return "LIST";
        case TOKEN_NEW: return "NEW";
        case TOKEN_CLEAR: return "CLEAR";
        case TOKEN_CLS: return "CLS";
        case TOKEN_SAVE: return "SAVE";
        case TOKEN_LOAD: return "LOAD";
        case TOKEN_ABS: return "ABS";
        case TOKEN_ACS: return "ACS";
        case TOKEN_ASN: return "ASN";
        case TOKEN_ATN: return "ATN";
        case TOKEN_COS: return "COS";
        case TOKEN_EXP: return "EXP";
        case TOKEN_INT: return "INT";
        case TOKEN_LN: return "LN";
        case TOKEN_RND: return "RND";
        case TOKEN_SGN: return "SGN";
        case TOKEN_SIN: return "SIN";
        case TOKEN_SQR: return "SQR";
        case TOKEN_TAN: return "TAN";
        case TOKEN_CHR_STR: return "CHR$";
        case TOKEN_CODE: return "CODE";
        case TOKEN_LEN: return "LEN";
        case TOKEN_STR_STR: return "STR$";
        case TOKEN_VAL: return "VAL";
        case TOKEN_INKEY_STR: return "INKEY$";
        default: return "UNKNOWN";
    }
}

void lexer_init(Lexer *l, const char *source) {
    l->source = source ? source : "";
    l->cursor = 0;
    memset(&l->current, 0, sizeof(Token));
    lexer_next(l);
}

static char peek(const Lexer *l) {
    return l->source[l->cursor];
}

static char peek_next(const Lexer *l) {
    if (l->source[l->cursor] == '\0') return '\0';
    return l->source[l->cursor + 1];
}

static char advance(Lexer *l) {
    char c = l->source[l->cursor];
    if (c != '\0') {
        l->cursor++;
    }
    return c;
}

static void skip_whitespace(Lexer *l) {
    while (true) {
        char c = peek(l);
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            advance(l);
        } else {
            break;
        }
    }
}

static TokenType match_keyword(const char *upper) {
    if (strcmp(upper, "LET") == 0) return TOKEN_LET;
    if (strcmp(upper, "PRINT") == 0) return TOKEN_PRINT;
    if (strcmp(upper, "INPUT") == 0) return TOKEN_INPUT;
    if (strcmp(upper, "GOTO") == 0) return TOKEN_GOTO;
    if (strcmp(upper, "GOSUB") == 0) return TOKEN_GOSUB;
    if (strcmp(upper, "RETURN") == 0) return TOKEN_RETURN;
    if (strcmp(upper, "IF") == 0) return TOKEN_IF;
    if (strcmp(upper, "THEN") == 0) return TOKEN_THEN;
    if (strcmp(upper, "FOR") == 0) return TOKEN_FOR;
    if (strcmp(upper, "TO") == 0) return TOKEN_TO;
    if (strcmp(upper, "STEP") == 0) return TOKEN_STEP;
    if (strcmp(upper, "NEXT") == 0) return TOKEN_NEXT;
    if (strcmp(upper, "DATA") == 0) return TOKEN_DATA;
    if (strcmp(upper, "READ") == 0) return TOKEN_READ;
    if (strcmp(upper, "RESTORE") == 0) return TOKEN_RESTORE;
    if (strcmp(upper, "DIM") == 0) return TOKEN_DIM;
    if (strcmp(upper, "STOP") == 0) return TOKEN_STOP;
    if (strcmp(upper, "REM") == 0) return TOKEN_REM;
    if (strcmp(upper, "RUN") == 0) return TOKEN_RUN;
    if (strcmp(upper, "LIST") == 0) return TOKEN_LIST;
    if (strcmp(upper, "NEW") == 0) return TOKEN_NEW;
    if (strcmp(upper, "CLEAR") == 0) return TOKEN_CLEAR;
    if (strcmp(upper, "CLS") == 0) return TOKEN_CLS;
    if (strcmp(upper, "SAVE") == 0) return TOKEN_SAVE;
    if (strcmp(upper, "LOAD") == 0) return TOKEN_LOAD;

    if (strcmp(upper, "AND") == 0) return TOKEN_AND;
    if (strcmp(upper, "OR") == 0) return TOKEN_OR;
    if (strcmp(upper, "NOT") == 0) return TOKEN_NOT;

    if (strcmp(upper, "ABS") == 0) return TOKEN_ABS;
    if (strcmp(upper, "ACS") == 0) return TOKEN_ACS;
    if (strcmp(upper, "ASN") == 0) return TOKEN_ASN;
    if (strcmp(upper, "ATN") == 0) return TOKEN_ATN;
    if (strcmp(upper, "COS") == 0) return TOKEN_COS;
    if (strcmp(upper, "EXP") == 0) return TOKEN_EXP;
    if (strcmp(upper, "INT") == 0) return TOKEN_INT;
    if (strcmp(upper, "LN") == 0) return TOKEN_LN;
    if (strcmp(upper, "RND") == 0) return TOKEN_RND;
    if (strcmp(upper, "SGN") == 0) return TOKEN_SGN;
    if (strcmp(upper, "SIN") == 0) return TOKEN_SIN;
    if (strcmp(upper, "SQR") == 0) return TOKEN_SQR;
    if (strcmp(upper, "TAN") == 0) return TOKEN_TAN;

    if (strcmp(upper, "CHR$") == 0) return TOKEN_CHR_STR;
    if (strcmp(upper, "CODE") == 0) return TOKEN_CODE;
    if (strcmp(upper, "LEN") == 0) return TOKEN_LEN;
    if (strcmp(upper, "STR$") == 0) return TOKEN_STR_STR;
    if (strcmp(upper, "VAL") == 0) return TOKEN_VAL;
    if (strcmp(upper, "INKEY$") == 0) return TOKEN_INKEY_STR;

    return TOKEN_UNKNOWN;
}

void lexer_next(Lexer *l) {
    token_free(&l->current);
    skip_whitespace(l);

    l->current.col = l->cursor + 1;
    l->current.num_val = 0.0;
    l->current.str_val = NULL;
    l->current.str_len = 0;

    char c = peek(l);
    if (c == '\0') {
        l->current.type = TOKEN_EOF;
        return;
    }

    // Number literal (e.g. 123, 3.14, .5)
    if (isdigit((unsigned char)c) || (c == '.' && isdigit((unsigned char)peek_next(l)))) {
        const char *start = &l->source[l->cursor];
        char *endptr = NULL;
        double val = strtod(start, &endptr);
        l->cursor += (endptr - start);
        l->current.type = TOKEN_NUMBER;
        l->current.num_val = val;
        return;
    }

    // String literal
    if (c == '"') {
        advance(l); // skip initial "
        size_t cap = 32;
        size_t len = 0;
        char *buf = malloc(cap);

        while (true) {
            char sc = advance(l);
            if (sc == '\0') {
                // Unterminated string literal
                free(buf);
                l->current.type = TOKEN_UNKNOWN;
                return;
            }
            if (sc == '"') {
                if (peek(l) == '"') {
                    // Escaped double quote ("")
                    advance(l);
                    if (len + 1 >= cap) {
                        cap *= 2;
                        buf = realloc(buf, cap);
                    }
                    buf[len++] = '"';
                } else {
                    // End of string literal
                    break;
                }
            } else {
                if (len + 1 >= cap) {
                    cap *= 2;
                    buf = realloc(buf, cap);
                }
                buf[len++] = sc;
            }
        }
        buf[len] = '\0';
        l->current.type = TOKEN_STRING;
        l->current.str_val = buf;
        l->current.str_len = len;
        return;
    }

    // Identifiers and Keywords
    if (isalpha((unsigned char)c)) {
        size_t start = l->cursor;
        while (isalnum((unsigned char)peek(l))) {
            advance(l);
        }
        if (peek(l) == '$') {
            advance(l);
        }
        size_t len = l->cursor - start;
        char *raw = malloc(len + 1);
        char *upper = malloc(len + 1);
        memcpy(raw, &l->source[start], len);
        raw[len] = '\0';
        for (size_t i = 0; i < len; i++) {
            upper[i] = (char)toupper((unsigned char)raw[i]);
        }
        upper[len] = '\0';

        TokenType kw = match_keyword(upper);
        free(upper);

        if (kw != TOKEN_UNKNOWN) {
            free(raw);
            if (kw == TOKEN_REM) {
                // Ignore the rest of the physical line!
                while (peek(l) != '\0' && peek(l) != '\n') {
                    advance(l);
                }
                l->current.type = TOKEN_EOF;
                return;
            }
            l->current.type = kw;
            return;
        }

        l->current.type = TOKEN_IDENT;
        l->current.str_val = raw;
        l->current.str_len = len;
        return;
    }

    // Operators and Delimiters
    advance(l);
    switch (c) {
        case ':': l->current.type = TOKEN_COLON; return;
        case ';': l->current.type = TOKEN_SEMICOLON; return;
        case ',': l->current.type = TOKEN_COMMA; return;
        case '\'': l->current.type = TOKEN_APOSTROPHE; return;
        case '(': l->current.type = TOKEN_LPAREN; return;
        case ')': l->current.type = TOKEN_RPAREN; return;
        case '+': l->current.type = TOKEN_PLUS; return;
        case '-': l->current.type = TOKEN_MINUS; return;
        case '*': l->current.type = TOKEN_STAR; return;
        case '/': l->current.type = TOKEN_SLASH; return;
        case '^': l->current.type = TOKEN_CARET; return;
        case '=': l->current.type = TOKEN_EQUAL; return;
        case '<':
            if (peek(l) == '>') {
                advance(l);
                l->current.type = TOKEN_NOT_EQUAL;
                return;
            }
            if (peek(l) == '=') {
                advance(l);
                l->current.type = TOKEN_LESS_EQUAL;
                return;
            }
            l->current.type = TOKEN_LESS;
            return;
        case '>':
            if (peek(l) == '=') {
                advance(l);
                l->current.type = TOKEN_GREATER_EQUAL;
                return;
            }
            l->current.type = TOKEN_GREATER;
            return;
        default:
            l->current.type = TOKEN_UNKNOWN;
            return;
    }
}
