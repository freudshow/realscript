// ============================================================================
// lexer.c -- Tokenizer / Lexer Implementation
//
// Scans the source string character-by-character and emits tokens one at a
// time (in a pull-based manner -- the parser calls next_token() when it
// needs the next token).
//
// Features:
//   - Skips whitespace (spaces, tabs, newlines, carriage returns)
//   - Handles // line comments and /* block comments */
//   - Recognises all keywords (e.g. 'var', 'fn', 'if', 'while', 'for')
//   - Numeric literals: integers (42) and doubles (3.14)
//   - Multi-character operators: &&, ||, ==, !=, <=, >=, <<, >>
// ============================================================================

#include "lexer.h"
#include <string.h>
#include <stdbool.h>
#include <stdio.h>

// ---------------------------------------------------------------------------
// Lexer internal state
//
//   start   : points to the beginning of the current lexeme being scanned
//   current : points to the character currently being examined
//   line    : current source line number (1-indexed, incremented on '\n')
// ---------------------------------------------------------------------------
typedef struct {
    const char* start;    // Start of the current lexeme in the source
    const char* current;  // Current scan position within the source
    int line;             // Current line number
} Lexer;

static Lexer lexer;       // Singleton lexer state (one source at a time)

// ---------------------------------------------------------------------------
// init_lexer -- (re)initialise the lexer with a new source string
// ---------------------------------------------------------------------------
void init_lexer(const char* source) {
    lexer.start = source;
    lexer.current = source;
    lexer.line = 1;
}

// ---------------------------------------------------------------------------
// Low-level scanning helpers
// ---------------------------------------------------------------------------

// is_at_end: have we consumed the entire source?
static bool is_at_end(void) {
    return *lexer.current == '\0';
}

// advance: consume and return the next character
static char advance(void) {
    lexer.current++;
    return lexer.current[-1];
}

// peek: look at the current character without consuming it
static char peek(void) {
    return *lexer.current;
}

// peek_next: look one character ahead (or '\0' at end)
static char peek_next(void) {
    if (is_at_end()) return '\0';
    return lexer.current[1];
}

// match: if the current character equals 'expected', consume it and return true
static bool match(char expected) {
    if (is_at_end()) return false;
    if (*lexer.current != expected) return false;
    lexer.current++;
    return true;
}

// ---------------------------------------------------------------------------
// Token construction helpers
// ---------------------------------------------------------------------------

// make_token: create a Token spanning from lexer.start to lexer.current
static Token make_token(TokenType type) {
    Token token;
    token.type = type;
    token.start = lexer.start;
    token.length = (int)(lexer.current - lexer.start);
    token.line = lexer.line;
    return token;
}

// error_token: create a synthetic error token whose .start is the message
static Token error_token(const char* message) {
    Token token;
    token.type = TOKEN_ERROR;
    token.start = message;        // Overload .start to point at error string
    token.length = (int)strlen(message);
    token.line = lexer.line;
    return token;
}

// ---------------------------------------------------------------------------
// skip_whitespace -- advance past spaces, tabs, newlines, and comments
//
// Comments supported:
//   - //  → line comment (until '\n' or EOF)
//   - /*  → block comment (nesting NOT supported)
// ---------------------------------------------------------------------------
static void skip_whitespace(void) {
    for (;;) {
        char c = peek();
        switch (c) {
            case ' ':
            case '\r':
            case '\t':
                advance();
                break;
            case '\n':
                lexer.line++;     // Track lines for error messages
                advance();
                break;
            case '/':
                if (peek_next() == '/') {
                    // Line comment: skip until newline or EOF
                    while (peek() != '\n' && !is_at_end()) advance();
                } else if (peek_next() == '*') {
                    // Block comment: skip until */
                    advance(); // consume '/'
                    advance(); // consume '*'
                    while (!(peek() == '*' && peek_next() == '/') && !is_at_end()) {
                        if (peek() == '\n') lexer.line++;
                        advance();
                    }
                    if (!is_at_end()) {
                        advance(); // consume '*'
                        advance(); // consume '/'
                    }
                } else {
                    return;       // '/' is actually a division operator
                }
                break;
            default:
                return;           // Non-whitespace/comment character
        }
    }
}

// ---------------------------------------------------------------------------
// Character classification
// ---------------------------------------------------------------------------
static bool is_alpha(char c) {
    return (c >= 'a' && c <= 'z') ||
           (c >= 'A' && c <= 'Z') ||
           c == '_';
}

static bool is_digit(char c) {
    return c >= '0' && c <= '9';
}

// ---------------------------------------------------------------------------
// Keyword detection
//
// check_keyword checks whether the characters from lexer.start+start onwards
// match the given 'rest' string.  If they do, returns the keyword TokenType;
// otherwise returns TOKEN_IDENTIFIER.
// ---------------------------------------------------------------------------
static TokenType check_keyword(int start, int length, const char* rest, TokenType type) {
    if (lexer.current - lexer.start == start + length &&
        memcmp(lexer.start + start, rest, length) == 0) {
        return type;
    }
    return TOKEN_IDENTIFIER;
}

// identifier_type: check whether the current identifier is actually a keyword
static TokenType identifier_type(void) {
    switch (lexer.start[0]) {
        case 'e': return check_keyword(1, 3, "lse", TOKEN_ELSE);
        case 'f':
            if (lexer.current - lexer.start > 1) {
                switch (lexer.start[1]) {
                    case 'n': return check_keyword(2, 0, "", TOKEN_FN);
                    case 'o': return check_keyword(2, 1, "r", TOKEN_FOR);
                    case 'a': return check_keyword(2, 3, "lse", TOKEN_FALSE);
                }
            }
            break;
        case 'i': return check_keyword(1, 1, "f", TOKEN_IF);
        case 'n': return check_keyword(1, 2, "il", TOKEN_NIL);
        case 'r': return check_keyword(1, 5, "eturn", TOKEN_RETURN);
        case 't': return check_keyword(1, 3, "rue", TOKEN_TRUE);
        case 'v': return check_keyword(1, 2, "ar", TOKEN_VAR);
        case 'w': return check_keyword(1, 4, "hile", TOKEN_WHILE);
    }
    return TOKEN_IDENTIFIER;   // No keyword matched
}

// identifier: scan a whole identifier/keyword and determine its type
static Token identifier(void) {
    while (is_alpha(peek()) || is_digit(peek())) advance();
    return make_token(identifier_type());
}

// number: scan an integer or floating-point literal
static Token number(void) {
    bool is_double = false;
    // Consume the integer part
    while (is_digit(peek())) advance();

    // If followed by '.' and another digit, it's a double literal
    if (peek() == '.' && is_digit(peek_next())) {
        is_double = true;
        advance(); // consume '.'
        while (is_digit(peek())) advance();  // consume fractional part
    }
    return make_token(is_double ? TOKEN_DOUBLE : TOKEN_INT);
}

// ============================================================================
// next_token -- the main entry point: return the next token from the source
//
// Called repeatedly by the parser to obtain the token stream.
// ============================================================================
Token next_token(void) {
    skip_whitespace();                     // Skip any preceding whitespace/comments
    lexer.start = lexer.current;           // Mark the start of the new lexeme

    if (is_at_end()) return make_token(TOKEN_EOF);

    char c = advance();                    // Consume first character

    // Identifier or keyword
    if (is_alpha(c)) return identifier();
    // Numeric literal
    if (is_digit(c)) return number();

    // Single- and multi-character operators / punctuation
    switch (c) {
        case '(': return make_token(TOKEN_LEFT_PAREN);
        case ')': return make_token(TOKEN_RIGHT_PAREN);
        case '{': return make_token(TOKEN_LEFT_BRACE);
        case '}': return make_token(TOKEN_RIGHT_BRACE);
        case ',': return make_token(TOKEN_COMMA);
        case ';': return make_token(TOKEN_SEMICOLON);
        case '#': return make_token(TOKEN_HASH);
        case '+': return make_token(TOKEN_PLUS);
        case '-': return make_token(TOKEN_MINUS);
        case '*': return make_token(TOKEN_STAR);
        case '/': return make_token(TOKEN_SLASH);
        case '%': return make_token(TOKEN_PERCENT);
        case '^': return make_token(TOKEN_CARET);
        case '~': return make_token(TOKEN_TILDE);

        // Two-character operators starting with &
        case '&':
            return make_token(match('&') ? TOKEN_AMPERSAND_AMPERSAND : TOKEN_AMPERSAND);
        // Two-character operators starting with |
        case '|':
            return make_token(match('|') ? TOKEN_PIPE_PIPE : TOKEN_PIPE);

        // Two-character operators starting with !
        case '!':
            return make_token(match('=') ? TOKEN_BANG_EQUAL : TOKEN_BANG);
        // Two-character operators starting with =
        case '=':
            return make_token(match('=') ? TOKEN_EQUAL_EQUAL : TOKEN_EQUAL);

        // Operators starting with <  (<<, <=, <)
        case '<':
            if (match('<')) return make_token(TOKEN_LESS_LESS);
            if (match('=')) return make_token(TOKEN_LESS_EQUAL);
            return make_token(TOKEN_LESS);

        // Operators starting with >  (>>, >=, >)
        case '>':
            if (match('>')) return make_token(TOKEN_GREATER_GREATER);
            if (match('=')) return make_token(TOKEN_GREATER_EQUAL);
            return make_token(TOKEN_GREATER);
    }

    // If we get here, the character is not recognised at all
    return error_token("Unexpected character.");
}
