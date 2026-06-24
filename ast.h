#ifndef AST_H
#define AST_H

#include "lexer.h"
#include "value.h"

typedef enum {
    AST_LITERAL,
    AST_VAR_REF,
    AST_ASSIGN_VAR,
    AST_DB_REF_ID,
    AST_ASSIGN_DB_REF_ID,
    AST_DB_REF_LINK,
    AST_ASSIGN_DB_REF_LINK,
    AST_BINARY,
    AST_UNARY,
    AST_CALL,

    AST_EXPR_STMT,
    AST_VAR_DECL,
    AST_FUN_DECL,
    AST_IF,
    AST_WHILE,
    AST_FOR,
    AST_BLOCK,
    AST_RETURN
} AstNodeType;

typedef struct AstNode AstNode;

typedef struct AstNodeList {
    AstNode* node;
    struct AstNodeList* next;
} AstNodeList;

typedef struct ParamList {
    char* name;
    struct ParamList* next;
} ParamList;

struct AstNode {
    AstNodeType type;
    int line;
    union {
        // AST_LITERAL
        Value literal;

        // AST_VAR_REF / AST_VAR_DECL
        struct {
            char* name;
            AstNode* initializer; // for AST_VAR_DECL
        } var;

        // AST_ASSIGN_VAR
        struct {
            char* name;
            AstNode* value;
        } assign_var;

        // AST_DB_REF_ID
        struct {
            int realNo;
        } db_ref_id;

        // AST_ASSIGN_DB_REF_ID
        struct {
            int realNo;
            AstNode* value;
        } assign_db_id;

        // AST_DB_REF_LINK
        struct {
            int linkNo;
            int devNo;
            int regNo;
        } db_ref_link;

        // AST_ASSIGN_DB_REF_LINK
        struct {
            int linkNo;
            int devNo;
            int regNo;
            AstNode* value;
        } assign_db_link;

        // AST_BINARY
        struct {
            TokenType op;
            AstNode* left;
            AstNode* right;
        } binary;

        // AST_UNARY
        struct {
            TokenType op;
            AstNode* operand;
        } unary;

        // AST_CALL
        struct {
            char* callee;
            AstNodeList* arguments;
        } call;

        // AST_EXPR_STMT / AST_RETURN
        struct {
            AstNode* expr;
        } expr_stmt;

        // AST_FUN_DECL
        struct {
            char* name;
            ParamList* parameters;
            AstNode* body; // AST_BLOCK
        } fun_decl;

        // AST_IF
        struct {
            AstNode* condition;
            AstNode* then_branch;
            AstNode* else_branch;
        } if_stmt;

        // AST_WHILE
        struct {
            AstNode* condition;
            AstNode* body;
        } while_stmt;

        // AST_FOR
        struct {
            AstNode* initializer;
            AstNode* condition;
            AstNode* increment;
            AstNode* body;
        } for_stmt;

        // AST_BLOCK
        struct {
            AstNodeList* statements;
        } block;
    } as;
};

AstNode* make_literal(Value val, int line);
AstNode* make_var_ref(const char* name, int length, int line);
AstNode* make_assign_var(const char* name, int length, AstNode* value, int line);
AstNode* make_db_ref_id(int realNo, int line);
AstNode* make_assign_db_ref_id(int realNo, AstNode* value, int line);
AstNode* make_db_ref_link(int linkNo, int devNo, int regNo, int line);
AstNode* make_assign_db_ref_link(int linkNo, int devNo, int regNo, AstNode* value, int line);
AstNode* make_binary(TokenType op, AstNode* left, AstNode* right, int line);
AstNode* make_unary(TokenType op, AstNode* operand, int line);
AstNode* make_call(const char* callee, int length, AstNodeList* arguments, int line);

AstNode* make_expr_stmt(AstNode* expr, int line);
AstNode* make_var_decl(const char* name, int length, AstNode* initializer, int line);
AstNode* make_fun_decl(const char* name, int length, ParamList* parameters, AstNode* body, int line);
AstNode* make_if(AstNode* condition, AstNode* then_branch, AstNode* else_branch, int line);
AstNode* make_while(AstNode* condition, AstNode* body, int line);
AstNode* make_for(AstNode* initializer, AstNode* condition, AstNode* increment, AstNode* body, int line);
AstNode* make_block(AstNodeList* statements, int line);
AstNode* make_return(AstNode* expr, int line);

void free_ast(AstNode* node);
void free_ast_list(AstNodeList* list);
void free_param_list(ParamList* list);

#endif // AST_H
