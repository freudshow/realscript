#include "lexer.h"
#include <string.h>
#include <stdbool.h>
#include <stdio.h>

typedef struct {
    const char* start;
    const char* current;
    int line;
} Lexer;

static Lexer lexer;

void init_lexer(const char* source) {
    lexer.start = source;
    lexer.current = source;
    lexer.line = 1;
}

static bool is_at_end(void) {
    return *lexer.current == '\0';
}

static char advance(void) {
    lexer.current++;
    return lexer.current[-1];
}

static char peek(void) {
    return *lexer.current;
}

static char peek_next(void) {
    if (is_at_end()) return '\0';
    return lexer.current[1];
}

static bool match(char expected) {
    if (is_at_end()) return false;
    if (*lexer.current != expected) return false;
    lexer.current++;
    return true;
}

static Token make_token(TokenType type) {
    Token token;
    token.type = type;
    token.start = lexer.start;
    token.length = (int)(lexer.current - lexer.start);
    token.line = lexer.line;
    return token;
}

static Token error_token(const char* message) {
    Token token;
    token.type = TOKEN_ERROR;
    token.start = message;
    token.length = (int)strlen(message);
    token.line = lexer.line;
    return token;
}

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
                lexer.line++;
                advance();
                break;
            case '/':
                if (peek_next() == '/') {
                    while (peek() != '\n' && !is_at_end()) advance();
                } else if (peek_next() == '*') {
                    advance(); // '/'
                    advance(); // '*'
                    while (!(peek() == '*' && peek_next() == '/') && !is_at_end()) {
                        if (peek() == '\n') lexer.line++;
                        advance();
                    }
                    if (!is_at_end()) {
                        advance(); // '*'
                        advance(); // '/'
                    }
                } else {
                    return;
                }
                break;
            default:
                return;
        }
    }
}

static bool is_alpha(char c) {
    return (c >= 'a' && c <= 'z') ||
           (c >= 'A' && c <= 'Z') ||
           c == '_';
}

static bool is_digit(char c) {
    return c >= '0' && c <= '9';
}

static TokenType check_keyword(int start, int length, const char* rest, TokenType type) {
    if (lexer.current - lexer.start == start + length &&
        memcmp(lexer.start + start, rest, length) == 0) {
        return type;
    }
    return TOKEN_IDENTIFIER;
}

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
    return TOKEN_IDENTIFIER;
}

static Token identifier(void) {
    while (is_alpha(peek()) || is_digit(peek())) advance();
    return make_token(identifier_type());
}

static Token number(void) {
    bool is_double = false;
    while (is_digit(peek())) advance();

    if (peek() == '.' && is_digit(peek_next())) {
        is_double = true;
        advance(); // consume '.'
        while (is_digit(peek())) advance();
    }
    return make_token(is_double ? TOKEN_DOUBLE : TOKEN_INT);
}

Token next_token(void) {
    skip_whitespace();
    lexer.start = lexer.current;

    if (is_at_end()) return make_token(TOKEN_EOF);

    char c = advance();

    if (is_alpha(c)) return identifier();
    if (is_digit(c)) return number();

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

        case '&':
            return make_token(match('&') ? TOKEN_AMPERSAND_AMPERSAND : TOKEN_AMPERSAND);
        case '|':
            return make_token(match('|') ? TOKEN_PIPE_PIPE : TOKEN_PIPE);

        case '!':
            return make_token(match('=') ? TOKEN_BANG_EQUAL : TOKEN_BANG);
        case '=':
            return make_token(match('=') ? TOKEN_EQUAL_EQUAL : TOKEN_EQUAL);

        case '<':
            if (match('<')) return make_token(TOKEN_LESS_LESS);
            if (match('=')) return make_token(TOKEN_LESS_EQUAL);
            return make_token(TOKEN_LESS);

        case '>':
            if (match('>')) return make_token(TOKEN_GREATER_GREATER);
            if (match('=')) return make_token(TOKEN_GREATER_EQUAL);
            return make_token(TOKEN_GREATER);
    }

    return error_token("Unexpected character.");
}
