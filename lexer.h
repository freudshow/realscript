#ifndef LEXER_H
#define LEXER_H

typedef enum {
    // Single-character tokens
    TOKEN_LEFT_PAREN, TOKEN_RIGHT_PAREN,
    TOKEN_LEFT_BRACE, TOKEN_RIGHT_BRACE,
    TOKEN_COMMA, TOKEN_SEMICOLON,
    TOKEN_HASH,

    // Operators (Arithmetic / Bitwise)
    TOKEN_PLUS, TOKEN_MINUS, TOKEN_STAR, TOKEN_SLASH, TOKEN_PERCENT,
    TOKEN_AMPERSAND, TOKEN_PIPE, TOKEN_CARET, TOKEN_TILDE,
    TOKEN_LESS_LESS, TOKEN_GREATER_GREATER,

    // Operators (Logical / Comparison / Assignment)
    TOKEN_BANG, TOKEN_BANG_EQUAL,
    TOKEN_EQUAL, TOKEN_EQUAL_EQUAL,
    TOKEN_GREATER, TOKEN_GREATER_EQUAL,
    TOKEN_LESS, TOKEN_LESS_EQUAL,
    TOKEN_AMPERSAND_AMPERSAND, TOKEN_PIPE_PIPE,

    // Literals
    TOKEN_IDENTIFIER, TOKEN_INT, TOKEN_DOUBLE,

    // Keywords
    TOKEN_VAR, TOKEN_FN, TOKEN_IF, TOKEN_ELSE, TOKEN_WHILE, TOKEN_FOR,
    TOKEN_RETURN, TOKEN_TRUE, TOKEN_FALSE, TOKEN_NIL,

    TOKEN_EOF,
    TOKEN_ERROR
} TokenType;

typedef struct {
    TokenType type;
    const char* start;
    int length;
    int line;
} Token;

void init_lexer(const char* source);
Token next_token(void);

#endif // LEXER_H
