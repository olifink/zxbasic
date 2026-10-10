#ifndef ZXBASIC_LEXER_H
#define ZXBASIC_LEXER_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

typedef enum {
    TOKEN_EOF = 0,
    TOKEN_COLON,
    
    // Delimiters
    TOKEN_LPAREN,
    TOKEN_RPAREN,
    TOKEN_COMMA,
    TOKEN_SEMICOLON,
    TOKEN_APOSTROPHE,
    
    // Operators
    TOKEN_PLUS,
    TOKEN_MINUS,
    TOKEN_STAR,
    TOKEN_SLASH,
    TOKEN_CARET,
    TOKEN_EQUAL,
    TOKEN_NOT_EQUAL,
    TOKEN_LESS,
    TOKEN_GREATER,
    TOKEN_LESS_EQUAL,
    TOKEN_GREATER_EQUAL,
    TOKEN_AND,
    TOKEN_OR,
    TOKEN_NOT,
    
    // Literals & Identifiers
    TOKEN_NUMBER,
    TOKEN_STRING,
    TOKEN_IDENT,
    
    // Statement Keywords
    TOKEN_LET,
    TOKEN_PRINT,
    TOKEN_INPUT,
    TOKEN_GOTO,
    TOKEN_GOSUB,
    TOKEN_RETURN,
    TOKEN_IF,
    TOKEN_THEN,
    TOKEN_FOR,
    TOKEN_TO,
    TOKEN_STEP,
    TOKEN_NEXT,
    TOKEN_DATA,
    TOKEN_READ,
    TOKEN_RESTORE,
    TOKEN_DIM,
    TOKEN_STOP,
    TOKEN_REM,
    TOKEN_RUN,
    TOKEN_LIST,
    TOKEN_NEW,
    TOKEN_CLEAR,
    TOKEN_CLS,
    TOKEN_SAVE,
    TOKEN_LOAD,
    TOKEN_EDIT,
    TOKEN_AUTO,
    TOKEN_EXIT,
    TOKEN_RENUM,
    TOKEN_INK,
    TOKEN_PAPER,
    TOKEN_BRIGHT,
    TOKEN_INVERSE,
    TOKEN_AT,

    // Built-in functions
    TOKEN_ABS,
    TOKEN_ACS,
    TOKEN_ASN,
    TOKEN_ATN,
    TOKEN_COS,
    TOKEN_EXP,
    TOKEN_INT,
    TOKEN_LN,
    TOKEN_RND,
    TOKEN_SGN,
    TOKEN_SIN,
    TOKEN_SQR,
    TOKEN_TAN,
    TOKEN_CHR_STR,
    TOKEN_CODE,
    TOKEN_LEN,
    TOKEN_STR_STR,
    TOKEN_VAL,
    TOKEN_INKEY_STR,
    
    TOKEN_UNKNOWN
} TokenType;

typedef struct {
    TokenType type;
    double num_val;
    char *str_val;     // for string literals or identifier names
    size_t str_len;
    size_t col;        // 1-based character position
} Token;

typedef struct {
    const char *source;
    size_t cursor;
    Token current;
} Lexer;

void lexer_init(Lexer *l, const char *source);
void lexer_next(Lexer *l);
void token_free(Token *t);
const char *token_type_name(TokenType t);

#endif // ZXBASIC_LEXER_H
