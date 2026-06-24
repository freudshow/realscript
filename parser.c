#include "parser.h"
#include "lexer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    Token current;
    Token previous;
    bool hadError;
    bool panicMode;
} ParserState;

static ParserState parser;

// Forward declarations of CFG functions
static AstNode* declaration(void);
static AstNode* statement(void);
static AstNode* var_decl(void);
static AstNode* fun_decl(void);
static AstNode* if_stmt(void);
static AstNode* while_stmt(void);
static AstNode* for_stmt(void);
static AstNode* return_stmt(void);
static AstNode* block(void);
static AstNode* expr_stmt(void);

static AstNode* expression(void);
static AstNode* assignment(void);
static AstNode* logical_or(void);
static AstNode* logical_and(void);
static AstNode* bitwise_or(void);
static AstNode* bitwise_xor(void);
static AstNode* bitwise_and(void);
static AstNode* equality(void);
static AstNode* comparison(void);
static AstNode* shift(void);
static AstNode* term(void);
static AstNode* factor(void);
static AstNode* unary(void);
static AstNode* call(void);
static AstNode* primary(void);

static void error_at(Token* token, const char* message) {
    if (parser.panicMode) return;
    parser.panicMode = true;
    fprintf(stderr, "[Line %d] Error", token->line);

    if (token->type == TOKEN_EOF) {
        fprintf(stderr, " at end");
    } else if (token->type == TOKEN_ERROR) {
        // Nothing
    } else {
        fprintf(stderr, " at '%.*s'", token->length, token->start);
    }

    fprintf(stderr, ": %s\n", message);
    parser.hadError = true;
}

static void error(const char* message) {
    error_at(&parser.previous, message);
}

static void error_at_current(const char* message) {
    error_at(&parser.current, message);
}

static void advance(void) {
    parser.previous = parser.current;

    for (;;) {
        parser.current = next_token();
        if (parser.current.type != TOKEN_ERROR) break;

        error_at_current(parser.current.start);
    }
}

static bool check(TokenType type) {
    return parser.current.type == type;
}

static bool match_token(TokenType type) {
    if (!check(type)) return false;
    advance();
    return true;
}

static void consume(TokenType type, const char* message) {
    if (parser.current.type == type) {
        advance();
        return;
    }
    error_at_current(message);
}

static void synchronize(void) {
    parser.panicMode = false;

    while (parser.current.type != TOKEN_EOF) {
        if (parser.previous.type == TOKEN_SEMICOLON) return;

        switch (parser.current.type) {
            case TOKEN_FN:
            case TOKEN_VAR:
            case TOKEN_IF:
            case TOKEN_WHILE:
            case TOKEN_FOR:
            case TOKEN_RETURN:
                return;
            default:
                // Do nothing.
                break;
        }

        advance();
    }
}

static int atoi_from_token(Token token) {
    char buf[64];
    int len = token.length > 63 ? 63 : token.length;
    memcpy(buf, token.start, len);
    buf[len] = '\0';
    return atoi(buf);
}

AstNodeList* parse(const char* source) {
    init_lexer(source);
    parser.hadError = false;
    parser.panicMode = false;

    advance(); // Load the first token

    AstNodeList* head = NULL;
    AstNodeList* tail = NULL;

    while (!check(TOKEN_EOF)) {
        AstNode* node = declaration();
        if (node != NULL) {
            AstNodeList* listNode = malloc(sizeof(AstNodeList));
            listNode->node = node;
            listNode->next = NULL;
            if (head == NULL) {
                head = listNode;
                tail = listNode;
            } else {
                tail->next = listNode;
                tail = listNode;
            }
        }
    }

    if (parser.hadError) {
        free_ast_list(head);
        return NULL;
    }

    return head;
}

static AstNode* declaration(void) {
    AstNode* decl = NULL;
    if (match_token(TOKEN_VAR)) {
        decl = var_decl();
    } else if (match_token(TOKEN_FN)) {
        decl = fun_decl();
    } else {
        decl = statement();
    }

    if (parser.panicMode) synchronize();
    return decl;
}

static AstNode* var_decl(void) {
    consume(TOKEN_IDENTIFIER, "Expect variable name.");
    Token nameToken = parser.previous;

    AstNode* initializer = NULL;
    if (match_token(TOKEN_EQUAL)) {
        initializer = expression();
    }

    consume(TOKEN_SEMICOLON, "Expect ';' after variable declaration.");
    return make_var_decl(nameToken.start, nameToken.length, initializer, nameToken.line);
}

static AstNode* fun_decl(void) {
    consume(TOKEN_IDENTIFIER, "Expect function name.");
    Token nameToken = parser.previous;

    consume(TOKEN_LEFT_PAREN, "Expect '(' after function name.");

    ParamList* head = NULL;
    ParamList* tail = NULL;

    if (!check(TOKEN_RIGHT_PAREN)) {
        do {
            consume(TOKEN_IDENTIFIER, "Expect parameter name.");
            ParamList* param = malloc(sizeof(ParamList));
            param->name = malloc(parser.previous.length + 1);
            memcpy(param->name, parser.previous.start, parser.previous.length);
            param->name[parser.previous.length] = '\0';
            param->next = NULL;

            if (head == NULL) {
                head = param;
                tail = param;
            } else {
                tail->next = param;
                tail = param;
            }
        } while (match_token(TOKEN_COMMA));
    }

    consume(TOKEN_RIGHT_PAREN, "Expect ')' after parameters.");
    consume(TOKEN_LEFT_BRACE, "Expect '{' before function body.");

    AstNode* body = block();

    return make_fun_decl(nameToken.start, nameToken.length, head, body, nameToken.line);
}

static AstNode* statement(void) {
    if (match_token(TOKEN_IF)) {
        return if_stmt();
    }
    if (match_token(TOKEN_WHILE)) {
        return while_stmt();
    }
    if (match_token(TOKEN_FOR)) {
        return for_stmt();
    }
    if (match_token(TOKEN_RETURN)) {
        return return_stmt();
    }
    if (match_token(TOKEN_LEFT_BRACE)) {
        return block();
    }
    return expr_stmt();
}

static AstNode* if_stmt(void) {
    int line = parser.previous.line;
    consume(TOKEN_LEFT_PAREN, "Expect '(' after 'if'.");
    AstNode* condition = expression();
    consume(TOKEN_RIGHT_PAREN, "Expect ')' after condition.");

    AstNode* thenBranch = statement();
    AstNode* elseBranch = NULL;
    if (match_token(TOKEN_ELSE)) {
        elseBranch = statement();
    }

    return make_if(condition, thenBranch, elseBranch, line);
}

static AstNode* while_stmt(void) {
    int line = parser.previous.line;
    consume(TOKEN_LEFT_PAREN, "Expect '(' after 'while'.");
    AstNode* condition = expression();
    consume(TOKEN_RIGHT_PAREN, "Expect ')' after condition.");

    AstNode* body = statement();
    return make_while(condition, body, line);
}

static AstNode* for_stmt(void) {
    int line = parser.previous.line;
    consume(TOKEN_LEFT_PAREN, "Expect '(' after 'for'.");

    AstNode* initializer = NULL;
    if (match_token(TOKEN_SEMICOLON)) {
        initializer = NULL;
    } else if (match_token(TOKEN_VAR)) {
        initializer = var_decl(); // var_decl parses internal semicolon
    } else {
        initializer = expr_stmt(); // expr_stmt parses internal semicolon
    }

    AstNode* condition = NULL;
    if (!match_token(TOKEN_SEMICOLON)) {
        condition = expression();
        consume(TOKEN_SEMICOLON, "Expect ';' after loop condition.");
    }

    AstNode* increment = NULL;
    if (!check(TOKEN_RIGHT_PAREN)) {
        increment = expression();
    }
    consume(TOKEN_RIGHT_PAREN, "Expect ')' after for clauses.");

    AstNode* body = statement();

    return make_for(initializer, condition, increment, body, line);
}

static AstNode* return_stmt(void) {
    int line = parser.previous.line;
    AstNode* expr = NULL;
    if (!check(TOKEN_SEMICOLON)) {
        expr = expression();
    }
    consume(TOKEN_SEMICOLON, "Expect ';' after return value.");
    return make_return(expr, line);
}

static AstNode* block(void) {
    int line = parser.previous.line;
    AstNodeList* head = NULL;
    AstNodeList* tail = NULL;

    while (!check(TOKEN_RIGHT_BRACE) && !check(TOKEN_EOF)) {
        AstNode* node = declaration();
        if (node != NULL) {
            AstNodeList* listNode = malloc(sizeof(AstNodeList));
            listNode->node = node;
            listNode->next = NULL;
            if (head == NULL) {
                head = listNode;
                tail = listNode;
            } else {
                tail->next = listNode;
                tail = listNode;
            }
        }
    }

    consume(TOKEN_RIGHT_BRACE, "Expect '}' after block.");
    return make_block(head, line);
}

static AstNode* expr_stmt(void) {
    int line = parser.current.line;
    AstNode* expr = expression();
    consume(TOKEN_SEMICOLON, "Expect ';' after expression.");
    return make_expr_stmt(expr, line);
}

static AstNode* expression(void) {
    return assignment();
}

static AstNode* assignment(void) {
    AstNode* expr = logical_or();

    if (match_token(TOKEN_EQUAL)) {
        Token equals = parser.previous;
        AstNode* value = assignment();

        if (expr->type == AST_VAR_REF) {
            char* name = expr->as.var.name;
            AstNode* assignNode = make_assign_var(name, (int)strlen(name), value, equals.line);
            // Free the temporary var ref node (only the shell struct; name is reused by assignNode)
            free(expr);
            return assignNode;
        } else if (expr->type == AST_DB_REF_ID) {
            int realNo = expr->as.db_ref_id.realNo;
            AstNode* assignNode = make_assign_db_ref_id(realNo, value, equals.line);
            free(expr);
            return assignNode;
        } else if (expr->type == AST_DB_REF_LINK) {
            int l = expr->as.db_ref_link.linkNo;
            int d = expr->as.db_ref_link.devNo;
            int r = expr->as.db_ref_link.regNo;
            AstNode* assignNode = make_assign_db_ref_link(l, d, r, value, equals.line);
            free(expr);
            return assignNode;
        }

        error("Invalid assignment target.");
    }

    return expr;
}

static AstNode* logical_or(void) {
    AstNode* expr = logical_and();
    while (match_token(TOKEN_PIPE_PIPE)) {
        TokenType op = parser.previous.type;
        AstNode* right = logical_and();
        expr = make_binary(op, expr, right, parser.previous.line);
    }
    return expr;
}

static AstNode* logical_and(void) {
    AstNode* expr = bitwise_or();
    while (match_token(TOKEN_AMPERSAND_AMPERSAND)) {
        TokenType op = parser.previous.type;
        AstNode* right = bitwise_or();
        expr = make_binary(op, expr, right, parser.previous.line);
    }
    return expr;
}

static AstNode* bitwise_or(void) {
    AstNode* expr = bitwise_xor();
    while (match_token(TOKEN_PIPE)) {
        TokenType op = parser.previous.type;
        AstNode* right = bitwise_xor();
        expr = make_binary(op, expr, right, parser.previous.line);
    }
    return expr;
}

static AstNode* bitwise_xor(void) {
    AstNode* expr = bitwise_and();
    while (match_token(TOKEN_CARET)) {
        TokenType op = parser.previous.type;
        AstNode* right = bitwise_and();
        expr = make_binary(op, expr, right, parser.previous.line);
    }
    return expr;
}

static AstNode* bitwise_and(void) {
    AstNode* expr = equality();
    while (match_token(TOKEN_AMPERSAND)) {
        TokenType op = parser.previous.type;
        AstNode* right = equality();
        expr = make_binary(op, expr, right, parser.previous.line);
    }
    return expr;
}

static AstNode* equality(void) {
    AstNode* expr = comparison();
    while (match_token(TOKEN_EQUAL_EQUAL) || match_token(TOKEN_BANG_EQUAL)) {
        TokenType op = parser.previous.type;
        AstNode* right = comparison();
        expr = make_binary(op, expr, right, parser.previous.line);
    }
    return expr;
}

static AstNode* comparison(void) {
    AstNode* expr = shift();
    while (match_token(TOKEN_LESS) || match_token(TOKEN_LESS_EQUAL) ||
           match_token(TOKEN_GREATER) || match_token(TOKEN_GREATER_EQUAL)) {
        TokenType op = parser.previous.type;
        AstNode* right = shift();
        expr = make_binary(op, expr, right, parser.previous.line);
    }
    return expr;
}

static AstNode* shift(void) {
    AstNode* expr = term();
    while (match_token(TOKEN_LESS_LESS) || match_token(TOKEN_GREATER_GREATER)) {
        TokenType op = parser.previous.type;
        AstNode* right = term();
        expr = make_binary(op, expr, right, parser.previous.line);
    }
    return expr;
}

static AstNode* term(void) {
    AstNode* expr = factor();
    while (match_token(TOKEN_PLUS) || match_token(TOKEN_MINUS)) {
        TokenType op = parser.previous.type;
        AstNode* right = factor();
        expr = make_binary(op, expr, right, parser.previous.line);
    }
    return expr;
}

static AstNode* factor(void) {
    AstNode* expr = unary();
    while (match_token(TOKEN_STAR) || match_token(TOKEN_SLASH) || match_token(TOKEN_PERCENT)) {
        TokenType op = parser.previous.type;
        AstNode* right = unary();
        expr = make_binary(op, expr, right, parser.previous.line);
    }
    return expr;
}

static AstNode* unary(void) {
    if (match_token(TOKEN_BANG) || match_token(TOKEN_TILDE) || match_token(TOKEN_MINUS)) {
        TokenType op = parser.previous.type;
        AstNode* operand = unary();
        return make_unary(op, operand, parser.previous.line);
    }
    return call();
}

static AstNode* call(void) {
    AstNode* expr = primary();

    for (;;) {
        if (match_token(TOKEN_LEFT_PAREN)) {
            if (expr->type != AST_VAR_REF) {
                error("Can only call functions directly by name.");
                free_ast(expr);
                return NULL;
            }

            Token nameToken = parser.previous;
            AstNodeList* head = NULL;
            AstNodeList* tail = NULL;

            if (!check(TOKEN_RIGHT_PAREN)) {
                do {
                    AstNode* arg = expression();
                    AstNodeList* argNode = malloc(sizeof(AstNodeList));
                    argNode->node = arg;
                    argNode->next = NULL;

                    if (head == NULL) {
                        head = argNode;
                        tail = argNode;
                    } else {
                        tail->next = argNode;
                        tail = argNode;
                    }
                } while (match_token(TOKEN_COMMA));
            }

            consume(TOKEN_RIGHT_PAREN, "Expect ')' after arguments.");

            char* calleeName = expr->as.var.name;
            AstNode* callNode = make_call(calleeName, (int)strlen(calleeName), head, nameToken.line);
            free(expr); // Free the shell var_ref node
            expr = callNode;
        } else {
            break;
        }
    }

    return expr;
}

static AstNode* primary(void) {
    if (match_token(TOKEN_FALSE)) return make_literal(bool_val(false), parser.previous.line);
    if (match_token(TOKEN_TRUE)) return make_literal(bool_val(true), parser.previous.line);
    if (match_token(TOKEN_NIL)) return make_literal(nil_val(), parser.previous.line);

    if (match_token(TOKEN_INT)) {
        char buf[128];
        int len = parser.previous.length > 127 ? 127 : parser.previous.length;
        memcpy(buf, parser.previous.start, len);
        buf[len] = '\0';
        int64_t val = strtoll(buf, NULL, 10);
        return make_literal(int_val(val), parser.previous.line);
    }

    if (match_token(TOKEN_DOUBLE)) {
        char buf[128];
        int len = parser.previous.length > 127 ? 127 : parser.previous.length;
        memcpy(buf, parser.previous.start, len);
        buf[len] = '\0';
        double val = strtod(buf, NULL);
        return make_literal(double_val(val), parser.previous.line);
    }

    if (match_token(TOKEN_IDENTIFIER)) {
        return make_var_ref(parser.previous.start, parser.previous.length, parser.previous.line);
    }

    if (match_token(TOKEN_HASH)) {
        // Database Reference: #32 or #(3, 45, 12)
        if (match_token(TOKEN_INT)) {
            int realNo = atoi_from_token(parser.previous);
            return make_db_ref_id(realNo, parser.previous.line);
        } else if (match_token(TOKEN_LEFT_PAREN)) {
            consume(TOKEN_INT, "Expect integer linkNo in database reference.");
            int linkNo = atoi_from_token(parser.previous);
            consume(TOKEN_COMMA, "Expect ',' after linkNo.");
            consume(TOKEN_INT, "Expect integer devNo in database reference.");
            int devNo = atoi_from_token(parser.previous);
            consume(TOKEN_COMMA, "Expect ',' after devNo.");
            consume(TOKEN_INT, "Expect integer regNo in database reference.");
            int regNo = atoi_from_token(parser.previous);
            consume(TOKEN_RIGHT_PAREN, "Expect ')' to close database reference.");
            return make_db_ref_link(linkNo, devNo, regNo, parser.previous.line);
        } else {
            error("Expect integer or '(' after '#'.");
            return NULL;
        }
    }

    if (match_token(TOKEN_LEFT_PAREN)) {
        AstNode* expr = expression();
        consume(TOKEN_RIGHT_PAREN, "Expect ')' after expression.");
        return expr;
    }

    error("Expect expression.");
    return NULL;
}
