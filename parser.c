// ============================================================================
// parser.c -- Recursive-Descent Parser Implementation
//
// Grammar (in order of precedence, lowest first):
//
//   program        → declaration* EOF
//   declaration    → var_decl | fun_decl | statement
//   var_decl       → "var" IDENTIFIER ("=" expression)? ";"
//   fun_decl       → "fn" IDENTIFIER "(" [params] ")" block
//   statement      → if_stmt | while_stmt | for_stmt | return_stmt
//                  | block | expr_stmt
//   if_stmt        → "if" "(" expression ")" statement ("else" statement)?
//   while_stmt     → "while" "(" expression ")" statement
//   for_stmt       → "for" "(" [init] ";" [cond] ";" [incr] ")" statement
//   return_stmt    → "return" expression? ";"
//   block          → "{" declaration* "}"
//   expr_stmt      → expression ";"
//   expression     → assignment
//   assignment     → logical_or ("=" assignment)?
//   logical_or     → logical_and ("||" logical_and)*
//   logical_and    → bitwise_or ("&&" bitwise_or)*
//   bitwise_or     → bitwise_xor ("|" bitwise_xor)*
//   bitwise_xor    → bitwise_and ("^" bitwise_and)*
//   bitwise_and    → equality ("&" equality)*
//   equality       → comparison (("==" | "!=") comparison)*
//   comparison     → shift (("<" | "<=" | ">" | ">=") shift)*
//   shift          → term (("<<" | ">>") term)*
//   term           → factor (("+" | "-") factor)*
//   factor         → unary (("*" | "/" | "%") unary)*
//   unary          → ("!" | "~" | "-") unary | call
//   call           → primary ("(" [args] ")")*
//   primary        → "true" | "false" | "nil" | INT | DOUBLE
//                  | IDENTIFIER | "#" INT | "#" "(" INT "," INT "," INT ")"
//                  | "(" expression ")"
//
// Database reference syntax (RealScript-specific extension):
//   #N         → flat-index database access
//   #(L, D, R) → link-device-register database access
// ============================================================================

#include "parser.h"
#include "lexer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ---------------------------------------------------------------------------
// ParserState -- tracks where we are in the token stream and error status
//
//   current   : the token we are about to process
//   previous  : the token we just processed (peekable for match/consume)
//   hadError  : set when any error occurs; causes parse() to return NULL
//   panicMode : after an error, skip tokens until a statement boundary
// ---------------------------------------------------------------------------
typedef struct {
    Token current;         // The token currently being looked at
    Token previous;        // The last consumed token
    bool hadError;         // True if any error has occurred
    bool panicMode;        // True while in error-recovery mode
} ParserState;

static ParserState parser;

// Forward declarations (recursive descent requires mutual recursion)
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

// ============================================================================
// Error handling
// ============================================================================

// error_at: report an error at a specific token, then enter panic mode
static void error_at(Token* token, const char* message) {
    if (parser.panicMode) return;              // Already recovering
    parser.panicMode = true;
    fprintf(stderr, "[Line %d] Error", token->line);

    if (token->type == TOKEN_EOF) {
        fprintf(stderr, " at end");
    } else if (token->type == TOKEN_ERROR) {
        // The lexer already printed the message; just print the error prefix
    } else {
        fprintf(stderr, " at '%.*s'", token->length, token->start);
    }

    fprintf(stderr, ": %s\n", message);
    parser.hadError = true;
}

// error: report at the previous token
static void error(const char* message) {
    error_at(&parser.previous, message);
}

// error_at_current: report at the current token
static void error_at_current(const char* message) {
    error_at(&parser.current, message);
}

// ============================================================================
// Token stream management
// ============================================================================

// advance: consume the current token and fetch the next one from the lexer
static void advance(void) {
    parser.previous = parser.current;

    // Skip over any error tokens produced by the lexer
    for (;;) {
        parser.current = next_token();
        if (parser.current.type != TOKEN_ERROR) break;
        error_at_current(parser.current.start);
    }
}

// check: see if the current token has a given type (without consuming)
static bool check(TokenType type) {
    return parser.current.type == type;
}

// match_token: if current token matches, consume and return true
static bool match_token(TokenType type) {
    if (!check(type)) return false;
    advance();
    return true;
}

// consume: expect a token type; if found, advance; otherwise error
static void consume(TokenType type, const char* message) {
    if (parser.current.type == type) {
        advance();
        return;
    }
    error_at_current(message);
}

// ============================================================================
// synchronize -- panic-mode error recovery
//
// Discard tokens until we hit a statement boundary (semicolon or a keyword
// that starts a new statement/declaration).  This prevents a single error
// from producing cascading nonsense messages.
// ============================================================================
static void synchronize(void) {
    parser.panicMode = false;

    while (parser.current.type != TOKEN_EOF) {
        // If the previous token was a semicolon, we're at a statement boundary
        if (parser.previous.type == TOKEN_SEMICOLON) return;

        switch (parser.current.type) {
            case TOKEN_FN:
            case TOKEN_VAR:
            case TOKEN_IF:
            case TOKEN_WHILE:
            case TOKEN_FOR:
            case TOKEN_RETURN:
                return;   // These start a new top-level construct
            default:
                break;
        }

        advance();
    }
}

// ---------------------------------------------------------------------------
// atoi_from_token: convert a Token's text to int (for database reference #N)
// ---------------------------------------------------------------------------
static int atoi_from_token(Token token) {
    char buf[64];
    int len = token.length > 63 ? 63 : token.length;
    memcpy(buf, token.start, len);
    buf[len] = '\0';
    return atoi(buf);
}

// ============================================================================
// parse -- entry point
//
// Initialises the lexer and parser state, then loops over declarations
// until EOF, collecting them into an AstNodeList.
// Returns NULL on any parse error (with messages printed to stderr).
// ============================================================================
AstNodeList* parse(const char* source) {
    init_lexer(source);
    parser.hadError = false;
    parser.panicMode = false;

    advance(); // Load the first token from the lexer

    // Build the top-level statement list
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

// ============================================================================
// declaration  → var_decl | fun_decl | statement
// ============================================================================
static AstNode* declaration(void) {
    AstNode* decl = NULL;
    if (match_token(TOKEN_VAR)) {
        decl = var_decl();
    } else if (match_token(TOKEN_FN)) {
        decl = fun_decl();
    } else {
        decl = statement();
    }

    if (parser.panicMode) synchronize();  // Error recovery after failed parse
    return decl;
}

// ============================================================================
// var_decl  → "var" IDENTIFIER ("=" expression)? ";"
// ============================================================================
static AstNode* var_decl(void) {
    consume(TOKEN_IDENTIFIER, "Expect variable name.");
    Token nameToken = parser.previous;

    AstNode* initializer = NULL;
    if (match_token(TOKEN_EQUAL)) {
        initializer = expression();       // Optional initialiser
    }

    consume(TOKEN_SEMICOLON, "Expect ';' after variable declaration.");
    return make_var_decl(nameToken.start, nameToken.length, initializer, nameToken.line);
}

// ============================================================================
// fun_decl  → "fn" IDENTIFIER "(" [params] ")" block
//   params  → IDENTIFIER ("," IDENTIFIER)*
// ============================================================================
static AstNode* fun_decl(void) {
    consume(TOKEN_IDENTIFIER, "Expect function name.");
    Token nameToken = parser.previous;

    consume(TOKEN_LEFT_PAREN, "Expect '(' after function name.");

    // Parse comma-separated parameter list
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

    AstNode* body = block();  // Parse the function body (which consumes '}')

    return make_fun_decl(nameToken.start, nameToken.length, head, body, nameToken.line);
}

// ============================================================================
// statement  → if_stmt | while_stmt | for_stmt | return_stmt | block | expr_stmt
// ============================================================================
static AstNode* statement(void) {
    if (match_token(TOKEN_IF))    return if_stmt();
    if (match_token(TOKEN_WHILE)) return while_stmt();
    if (match_token(TOKEN_FOR))   return for_stmt();
    if (match_token(TOKEN_RETURN)) return return_stmt();
    if (match_token(TOKEN_LEFT_BRACE)) return block();
    return expr_stmt();
}

// ============================================================================
// if_stmt  → "if" "(" expression ")" statement ("else" statement)?
// ============================================================================
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

// ============================================================================
// while_stmt  → "while" "(" expression ")" statement
// ============================================================================
static AstNode* while_stmt(void) {
    int line = parser.previous.line;
    consume(TOKEN_LEFT_PAREN, "Expect '(' after 'while'.");
    AstNode* condition = expression();
    consume(TOKEN_RIGHT_PAREN, "Expect ')' after condition.");

    AstNode* body = statement();
    return make_while(condition, body, line);
}

// ============================================================================
// for_stmt  → "for" "(" [init] ";" [cond] ";" [incr] ")" statement
//
// Each of init, cond, incr may be omitted (represented as NULL in the AST).
// init may be a var_decl, an expr_stmt, or empty (just ";").
// ============================================================================
static AstNode* for_stmt(void) {
    int line = parser.previous.line;
    consume(TOKEN_LEFT_PAREN, "Expect '(' after 'for'.");

    // --- Initialiser clause ---
    AstNode* initializer = NULL;
    if (match_token(TOKEN_SEMICOLON)) {
        initializer = NULL;                     // Empty init
    } else if (match_token(TOKEN_VAR)) {
        initializer = var_decl();               // var_decl consumes its ';'
    } else {
        initializer = expr_stmt();              // expr_stmt consumes its ';'
    }

    // --- Condition clause ---
    AstNode* condition = NULL;
    if (!match_token(TOKEN_SEMICOLON)) {
        condition = expression();
        consume(TOKEN_SEMICOLON, "Expect ';' after loop condition.");
    }

    // --- Increment clause ---
    AstNode* increment = NULL;
    if (!check(TOKEN_RIGHT_PAREN)) {
        increment = expression();
    }
    consume(TOKEN_RIGHT_PAREN, "Expect ')' after for clauses.");

    AstNode* body = statement();

    return make_for(initializer, condition, increment, body, line);
}

// ============================================================================
// return_stmt  → "return" expression? ";"
// ============================================================================
static AstNode* return_stmt(void) {
    int line = parser.previous.line;
    AstNode* expr = NULL;
    if (!check(TOKEN_SEMICOLON)) {
        expr = expression();
    }
    consume(TOKEN_SEMICOLON, "Expect ';' after return value.");
    return make_return(expr, line);
}

// ============================================================================
// block  → "{" declaration* "}"
// ============================================================================
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

// ============================================================================
// expr_stmt  → expression ";"
// ============================================================================
static AstNode* expr_stmt(void) {
    int line = parser.current.line;
    AstNode* expr = expression();
    consume(TOKEN_SEMICOLON, "Expect ';' after expression.");
    return make_expr_stmt(expr, line);
}

// ============================================================================
// expression  → assignment
// ============================================================================
static AstNode* expression(void) {
    return assignment();
}

// ============================================================================
// assignment  → logical_or ("=" assignment)?
//
// The left-hand side must be a valid lvalue (variable ref or db ref).
// We detect this by checking the type of the already-parsed left-hand expr.
// ============================================================================
static AstNode* assignment(void) {
    AstNode* expr = logical_or();

    if (match_token(TOKEN_EQUAL)) {
        Token equals = parser.previous;
        AstNode* value = assignment();  // Right-associative

        // Convert the var_ref / db_ref node into an assignment node
        if (expr->type == AST_VAR_REF) {
            char* name = expr->as.var.name;
            AstNode* assignNode = make_assign_var(name, (int)strlen(name), value, equals.line);
            free(expr);  // Free the ref shell; the name pointer is reused
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

// ============================================================================
// Operator precedence:  each level is a Pratt-like left-associative loop
// ============================================================================

// logical_or  → logical_and ("||" logical_and)*
static AstNode* logical_or(void) {
    AstNode* expr = logical_and();
    while (match_token(TOKEN_PIPE_PIPE)) {
        TokenType op = parser.previous.type;
        AstNode* right = logical_and();
        expr = make_binary(op, expr, right, parser.previous.line);
    }
    return expr;
}

// logical_and  → bitwise_or ("&&" bitwise_or)*
static AstNode* logical_and(void) {
    AstNode* expr = bitwise_or();
    while (match_token(TOKEN_AMPERSAND_AMPERSAND)) {
        TokenType op = parser.previous.type;
        AstNode* right = bitwise_or();
        expr = make_binary(op, expr, right, parser.previous.line);
    }
    return expr;
}

// bitwise_or  → bitwise_xor ("|" bitwise_xor)*
static AstNode* bitwise_or(void) {
    AstNode* expr = bitwise_xor();
    while (match_token(TOKEN_PIPE)) {
        TokenType op = parser.previous.type;
        AstNode* right = bitwise_xor();
        expr = make_binary(op, expr, right, parser.previous.line);
    }
    return expr;
}

// bitwise_xor  → bitwise_and ("^" bitwise_and)*
static AstNode* bitwise_xor(void) {
    AstNode* expr = bitwise_and();
    while (match_token(TOKEN_CARET)) {
        TokenType op = parser.previous.type;
        AstNode* right = bitwise_and();
        expr = make_binary(op, expr, right, parser.previous.line);
    }
    return expr;
}

// bitwise_and  → equality ("&" equality)*
static AstNode* bitwise_and(void) {
    AstNode* expr = equality();
    while (match_token(TOKEN_AMPERSAND)) {
        TokenType op = parser.previous.type;
        AstNode* right = equality();
        expr = make_binary(op, expr, right, parser.previous.line);
    }
    return expr;
}

// equality  → comparison (("==" | "!=") comparison)*
static AstNode* equality(void) {
    AstNode* expr = comparison();
    while (match_token(TOKEN_EQUAL_EQUAL) || match_token(TOKEN_BANG_EQUAL)) {
        TokenType op = parser.previous.type;
        AstNode* right = comparison();
        expr = make_binary(op, expr, right, parser.previous.line);
    }
    return expr;
}

// comparison  → shift (("<" | "<=" | ">" | ">=") shift)*
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

// shift  → term (("<<" | ">>") term)*
static AstNode* shift(void) {
    AstNode* expr = term();
    while (match_token(TOKEN_LESS_LESS) || match_token(TOKEN_GREATER_GREATER)) {
        TokenType op = parser.previous.type;
        AstNode* right = term();
        expr = make_binary(op, expr, right, parser.previous.line);
    }
    return expr;
}

// term  → factor (("+" | "-") factor)*
static AstNode* term(void) {
    AstNode* expr = factor();
    while (match_token(TOKEN_PLUS) || match_token(TOKEN_MINUS)) {
        TokenType op = parser.previous.type;
        AstNode* right = factor();
        expr = make_binary(op, expr, right, parser.previous.line);
    }
    return expr;
}

// factor  → unary (("*" | "/" | "%") unary)*
static AstNode* factor(void) {
    AstNode* expr = unary();
    while (match_token(TOKEN_STAR) || match_token(TOKEN_SLASH) || match_token(TOKEN_PERCENT)) {
        TokenType op = parser.previous.type;
        AstNode* right = unary();
        expr = make_binary(op, expr, right, parser.previous.line);
    }
    return expr;
}

// ============================================================================
// unary  → ("!" | "~" | "-") unary | call
// ============================================================================
static AstNode* unary(void) {
    if (match_token(TOKEN_BANG) || match_token(TOKEN_TILDE) || match_token(TOKEN_MINUS)) {
        TokenType op = parser.previous.type;
        AstNode* operand = unary();   // Right-associative nesting
        return make_unary(op, operand, parser.previous.line);
    }
    return call();
}

// ============================================================================
// call  → primary ("(" [expression ("," expression)*] ")")*
//
// Handles function calls like  foo()  foo(a)  foo(a, b, c)
// Can be chained (though the language currently only supports direct calls
// by name, not method calls on arbitrary expressions).
// ============================================================================
static AstNode* call(void) {
    AstNode* expr = primary();

    for (;;) {
        if (match_token(TOKEN_LEFT_PAREN)) {
            // Only allow calls on variable references (not on literals or
            // arbitrary expressions)
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
            free(expr);  // Free the var_ref shell (name pointer is reused)
            expr = callNode;
        } else {
            break;
        }
    }

    return expr;
}

// ============================================================================
// primary  → "true" | "false" | "nil"
//          | INT | DOUBLE | IDENTIFIER
//          | "#" INT | "#" "(" INT "," INT "," INT ")"
//          | "(" expression ")"
// ============================================================================
static AstNode* primary(void) {
    // Keyword literals
    if (match_token(TOKEN_FALSE)) return make_literal(bool_val(false), parser.previous.line);
    if (match_token(TOKEN_TRUE))  return make_literal(bool_val(true), parser.previous.line);
    if (match_token(TOKEN_NIL))   return make_literal(nil_val(), parser.previous.line);

    // Integer literal (e.g. 42)
    if (match_token(TOKEN_INT)) {
        char buf[128];
        int len = parser.previous.length > 127 ? 127 : parser.previous.length;
        memcpy(buf, parser.previous.start, len);
        buf[len] = '\0';
        int64_t val = strtoll(buf, NULL, 10);
        return make_literal(int_val(val), parser.previous.line);
    }

    // Double literal (e.g. 3.14)
    if (match_token(TOKEN_DOUBLE)) {
        char buf[128];
        int len = parser.previous.length > 127 ? 127 : parser.previous.length;
        memcpy(buf, parser.previous.start, len);
        buf[len] = '\0';
        double val = strtod(buf, NULL);
        return make_literal(double_val(val), parser.previous.line);
    }

    // Variable reference
    if (match_token(TOKEN_IDENTIFIER)) {
        return make_var_ref(parser.previous.start, parser.previous.length, parser.previous.line);
    }

    // Database references:  #N  or  #(L, D, R)
    if (match_token(TOKEN_HASH)) {
        if (match_token(TOKEN_INT)) {
            // Flat-index form: #32
            int realNo = atoi_from_token(parser.previous);
            return make_db_ref_id(realNo, parser.previous.line);
        } else if (match_token(TOKEN_LEFT_PAREN)) {
            // Tuple form: #(3, 45, 12)
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

    // Parenthesised expression (grouping)
    if (match_token(TOKEN_LEFT_PAREN)) {
        AstNode* expr = expression();
        consume(TOKEN_RIGHT_PAREN, "Expect ')' after expression.");
        return expr;
    }

    // Nothing matched — syntax error
    error("Expect expression.");
    return NULL;
}
