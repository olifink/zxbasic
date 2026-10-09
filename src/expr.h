#ifndef ZXBASIC_EXPR_H
#define ZXBASIC_EXPR_H

#include "lexer.h"
#include "value.h"
#include "symtab.h"
#include "error.h"

// Parse and evaluate an expression from current lexer position
Value expr_eval(Lexer *l, SymTab *st, BasicError *err);

#endif // ZXBASIC_EXPR_H
