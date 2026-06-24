#include "ast.h"
#include <stdlib.h>
#include <string.h>

static char* duplicate_string(const char* start, int length) {
    char* copy = malloc(length + 1);
    if (!copy) return NULL;
    memcpy(copy, start, length);
    copy[length] = '\0';
    return copy;
}

AstNode* make_literal(Value val, int line) {
    AstNode* node = malloc(sizeof(AstNode));
    if (!node) return NULL;
    node->type = AST_LITERAL;
    node->line = line;
    node->as.literal = val;
    return node;
}

AstNode* make_var_ref(const char* name, int length, int line) {
    AstNode* node = malloc(sizeof(AstNode));
    if (!node) return NULL;
    node->type = AST_VAR_REF;
    node->line = line;
    node->as.var.name = duplicate_string(name, length);
    node->as.var.initializer = NULL;
    return node;
}

AstNode* make_assign_var(const char* name, int length, AstNode* value, int line) {
    AstNode* node = malloc(sizeof(AstNode));
    if (!node) return NULL;
    node->type = AST_ASSIGN_VAR;
    node->line = line;
    node->as.assign_var.name = duplicate_string(name, length);
    node->as.assign_var.value = value;
    return node;
}

AstNode* make_db_ref_id(int realNo, int line) {
    AstNode* node = malloc(sizeof(AstNode));
    if (!node) return NULL;
    node->type = AST_DB_REF_ID;
    node->line = line;
    node->as.db_ref_id.realNo = realNo;
    return node;
}

AstNode* make_assign_db_ref_id(int realNo, AstNode* value, int line) {
    AstNode* node = malloc(sizeof(AstNode));
    if (!node) return NULL;
    node->type = AST_ASSIGN_DB_REF_ID;
    node->line = line;
    node->as.assign_db_id.realNo = realNo;
    node->as.assign_db_id.value = value;
    return node;
}

AstNode* make_db_ref_link(int linkNo, int devNo, int regNo, int line) {
    AstNode* node = malloc(sizeof(AstNode));
    if (!node) return NULL;
    node->type = AST_DB_REF_LINK;
    node->line = line;
    node->as.db_ref_link.linkNo = linkNo;
    node->as.db_ref_link.devNo = devNo;
    node->as.db_ref_link.regNo = regNo;
    return node;
}

AstNode* make_assign_db_ref_link(int linkNo, int devNo, int regNo, AstNode* value, int line) {
    AstNode* node = malloc(sizeof(AstNode));
    if (!node) return NULL;
    node->type = AST_ASSIGN_DB_REF_LINK;
    node->line = line;
    node->as.assign_db_link.linkNo = linkNo;
    node->as.assign_db_link.devNo = devNo;
    node->as.assign_db_link.regNo = regNo;
    node->as.assign_db_link.value = value;
    return node;
}

AstNode* make_binary(TokenType op, AstNode* left, AstNode* right, int line) {
    AstNode* node = malloc(sizeof(AstNode));
    if (!node) return NULL;
    node->type = AST_BINARY;
    node->line = line;
    node->as.binary.op = op;
    node->as.binary.left = left;
    node->as.binary.right = right;
    return node;
}

AstNode* make_unary(TokenType op, AstNode* operand, int line) {
    AstNode* node = malloc(sizeof(AstNode));
    if (!node) return NULL;
    node->type = AST_UNARY;
    node->line = line;
    node->as.unary.op = op;
    node->as.unary.operand = operand;
    return node;
}

AstNode* make_call(const char* callee, int length, AstNodeList* arguments, int line) {
    AstNode* node = malloc(sizeof(AstNode));
    if (!node) return NULL;
    node->type = AST_CALL;
    node->line = line;
    node->as.call.callee = duplicate_string(callee, length);
    node->as.call.arguments = arguments;
    return node;
}

AstNode* make_expr_stmt(AstNode* expr, int line) {
    AstNode* node = malloc(sizeof(AstNode));
    if (!node) return NULL;
    node->type = AST_EXPR_STMT;
    node->line = line;
    node->as.expr_stmt.expr = expr;
    return node;
}

AstNode* make_var_decl(const char* name, int length, AstNode* initializer, int line) {
    AstNode* node = malloc(sizeof(AstNode));
    if (!node) return NULL;
    node->type = AST_VAR_DECL;
    node->line = line;
    node->as.var.name = duplicate_string(name, length);
    node->as.var.initializer = initializer;
    return node;
}

AstNode* make_fun_decl(const char* name, int length, ParamList* parameters, AstNode* body, int line) {
    AstNode* node = malloc(sizeof(AstNode));
    if (!node) return NULL;
    node->type = AST_FUN_DECL;
    node->line = line;
    node->as.fun_decl.name = duplicate_string(name, length);
    node->as.fun_decl.parameters = parameters;
    node->as.fun_decl.body = body;
    return node;
}

AstNode* make_if(AstNode* condition, AstNode* then_branch, AstNode* else_branch, int line) {
    AstNode* node = malloc(sizeof(AstNode));
    if (!node) return NULL;
    node->type = AST_IF;
    node->line = line;
    node->as.if_stmt.condition = condition;
    node->as.if_stmt.then_branch = then_branch;
    node->as.if_stmt.else_branch = else_branch;
    return node;
}

AstNode* make_while(AstNode* condition, AstNode* body, int line) {
    AstNode* node = malloc(sizeof(AstNode));
    if (!node) return NULL;
    node->type = AST_WHILE;
    node->line = line;
    node->as.while_stmt.condition = condition;
    node->as.while_stmt.body = body;
    return node;
}

AstNode* make_for(AstNode* initializer, AstNode* condition, AstNode* increment, AstNode* body, int line) {
    AstNode* node = malloc(sizeof(AstNode));
    if (!node) return NULL;
    node->type = AST_FOR;
    node->line = line;
    node->as.for_stmt.initializer = initializer;
    node->as.for_stmt.condition = condition;
    node->as.for_stmt.increment = increment;
    node->as.for_stmt.body = body;
    return node;
}

AstNode* make_block(AstNodeList* statements, int line) {
    AstNode* node = malloc(sizeof(AstNode));
    if (!node) return NULL;
    node->type = AST_BLOCK;
    node->line = line;
    node->as.block.statements = statements;
    return node;
}

AstNode* make_return(AstNode* expr, int line) {
    AstNode* node = malloc(sizeof(AstNode));
    if (!node) return NULL;
    node->type = AST_RETURN;
    node->line = line;
    node->as.expr_stmt.expr = expr;
    return node;
}

void free_ast(AstNode* node) {
    if (!node) return;
    switch (node->type) {
        case AST_LITERAL:
            // Value does not dynamic alloc.
            break;
        case AST_VAR_REF:
            free(node->as.var.name);
            break;
        case AST_ASSIGN_VAR:
            free(node->as.assign_var.name);
            free_ast(node->as.assign_var.value);
            break;
        case AST_DB_REF_ID:
            break;
        case AST_ASSIGN_DB_REF_ID:
            free_ast(node->as.assign_db_id.value);
            break;
        case AST_DB_REF_LINK:
            break;
        case AST_ASSIGN_DB_REF_LINK:
            free_ast(node->as.assign_db_link.value);
            break;
        case AST_BINARY:
            free_ast(node->as.binary.left);
            free_ast(node->as.binary.right);
            break;
        case AST_UNARY:
            free_ast(node->as.unary.operand);
            break;
        case AST_CALL:
            free(node->as.call.callee);
            free_ast_list(node->as.call.arguments);
            break;
        case AST_EXPR_STMT:
        case AST_RETURN:
            free_ast(node->as.expr_stmt.expr);
            break;
        case AST_VAR_DECL:
            free(node->as.var.name);
            free_ast(node->as.var.initializer);
            break;
        case AST_FUN_DECL:
            free(node->as.fun_decl.name);
            free_param_list(node->as.fun_decl.parameters);
            free_ast(node->as.fun_decl.body);
            break;
        case AST_IF:
            free_ast(node->as.if_stmt.condition);
            free_ast(node->as.if_stmt.then_branch);
            free_ast(node->as.if_stmt.else_branch);
            break;
        case AST_WHILE:
            free_ast(node->as.while_stmt.condition);
            free_ast(node->as.while_stmt.body);
            break;
        case AST_FOR:
            free_ast(node->as.for_stmt.initializer);
            free_ast(node->as.for_stmt.condition);
            free_ast(node->as.for_stmt.increment);
            free_ast(node->as.for_stmt.body);
            break;
        case AST_BLOCK:
            free_ast_list(node->as.block.statements);
            break;
    }
    free(node);
}

void free_ast_list(AstNodeList* list) {
    while (list != NULL) {
        AstNodeList* next = list->next;
        free_ast(list->node);
        free(list);
        list = next;
    }
}

void free_param_list(ParamList* list) {
    while (list != NULL) {
        ParamList* next = list->next;
        free(list->name);
        free(list);
        list = next;
    }
}
