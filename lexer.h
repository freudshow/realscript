// ============================================================================
// lexer.h -- Tokenizer / Lexer Header
//
// The lexer converts a raw source string into a stream of Tokens that the
// parser can consume one at a time.  This is the first phase of the
// compilation pipeline:  source → lexer → tokens → parser → AST
//
// Token types cover:
//   - Punctuation:  ( ) { } , ; #
//   - Arithmetic:   + - * / % & | ^ ~ << >>
//   - Logical:      ! == != = < > <= >= && ||
//   - Literals:     identifiers, integers, doubles
//   - Keywords:     var fn if else while for return true false nil
//   - Sentinel:     EOF and ERROR
// ============================================================================

#ifndef LEXER_H
#define LEXER_H

// ---------------------------------------------------------------------------
// TokenType enum -- every category of lexical token
// ---------------------------------------------------------------------------
typedef enum {
    // ---- Single-character punctuation ----
    TOKEN_LEFT_PAREN, TOKEN_RIGHT_PAREN,
    TOKEN_LEFT_BRACE, TOKEN_RIGHT_BRACE,
    TOKEN_COMMA, TOKEN_SEMICOLON,
    TOKEN_HASH,                // #  -- prefix for database references

    // ---- Arithmetic / Bitwise operators ----
    TOKEN_PLUS, TOKEN_MINUS, TOKEN_STAR, TOKEN_SLASH, TOKEN_PERCENT,
    TOKEN_AMPERSAND, TOKEN_PIPE, TOKEN_CARET, TOKEN_TILDE,
    TOKEN_LESS_LESS, TOKEN_GREATER_GREATER,

    // ---- Logical / Comparison / Assignment ----
    TOKEN_BANG, TOKEN_BANG_EQUAL,
    TOKEN_EQUAL, TOKEN_EQUAL_EQUAL,
    TOKEN_GREATER, TOKEN_GREATER_EQUAL,
    TOKEN_LESS, TOKEN_LESS_EQUAL,
    TOKEN_AMPERSAND_AMPERSAND, TOKEN_PIPE_PIPE,

    // ---- Literals ----
    TOKEN_IDENTIFIER,   // User-defined names (variable, function names)
    TOKEN_INT,          // Integer literal  (e.g. 42)
    TOKEN_DOUBLE,       // Double literal   (e.g. 3.14)

    // ---- Keywords ----
    TOKEN_VAR, TOKEN_FN, TOKEN_IF, TOKEN_ELSE, TOKEN_WHILE, TOKEN_FOR,
    TOKEN_RETURN, TOKEN_TRUE, TOKEN_FALSE, TOKEN_NIL,

    TOKEN_EOF,          // End of source (no more tokens)
    TOKEN_ERROR         // Lexical error (invalid character etc.)
} TokenType;

// ---------------------------------------------------------------------------
// Token -- a single lexeme produced by the lexer
//
//   start   : pointer into the original source string (NOT null-terminated)
//   length  : number of characters in the lexeme
//   line    : source line number (1-based) for error messages
// ---------------------------------------------------------------------------
typedef struct {
    TokenType type;      // What kind of token this is
    const char* start;   // First character of the lexeme in source
    int length;          // Number of characters in the lexeme
    int line;            // Source line number (for error reporting)
} Token;

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------
void init_lexer(const char* source);   // Reset lexer state with new source
Token next_token(void);                 // Return the next token from the source

#endif // LEXER_H
