// ============================================================================
// ast.h -- Abstract Syntax Tree Header
//
// The parser produces an AST — a tree of AstNode structs — that represents
// the syntactic structure of the source program.  The compiler then walks
// this tree to emit bytecode.
//
// Each AstNode carries a type tag (AstNodeType) and a union of type-specific
// data (operands, children, etc.).
//
// Helper types:
//   AstNodeList -- singly-linked list of AstNode pointers (used for blocks,
//                  function arguments, and top-level statement sequences)
//   ParamList   -- singly-linked list of function parameter names
// ============================================================================

#ifndef AST_H
#define AST_H

#include "lexer.h"
#include "value.h"

// ---------------------------------------------------------------------------
// AstNodeType -- every possible AST node kind
// ---------------------------------------------------------------------------
typedef enum {
    // ---- Expression nodes ----
    AST_LITERAL,           // Literal value (int, double, bool, nil)
    AST_VAR_REF,           // Variable reference (read)
    AST_ASSIGN_VAR,        // Variable assignment (write)
    AST_DB_REF_ID,         // Database read by flat index (#N)
    AST_ASSIGN_DB_REF_ID,  // Database write by flat index (#N = val)
    AST_DB_REF_LINK,       // Database read by link-dev-reg (#(L,D,R))
    AST_ASSIGN_DB_REF_LINK, // Database write by link-dev-reg (#(L,D,R) = val)
    AST_BINARY,            // Binary operation (e.g. a + b, a && b)
    AST_UNARY,             // Unary operation (e.g. -a, !a, ~a)
    AST_CALL,              // Function call (e.g. foo(a, b))

    // ---- Statement nodes ----
    AST_EXPR_STMT,         // Expression statement (expression followed by ;)
    AST_VAR_DECL,          // Variable declaration (var x = ...)
    AST_FUN_DECL,          // Function declaration (fn foo(...) { ... })
    AST_IF,                // If-else statement
    AST_WHILE,             // While loop
    AST_FOR,               // For loop
    AST_BLOCK,             // Block (sequence of statements in { })
    AST_RETURN             // Return statement
} AstNodeType;

// Forward declaration so the struct can reference itself
typedef struct AstNode AstNode;

// ---------------------------------------------------------------------------
// AstNodeList -- singly-linked list of child nodes
// ---------------------------------------------------------------------------
typedef struct AstNodeList {
    AstNode* node;              // The child node
    struct AstNodeList* next;   // Next element in the list (NULL = end)
} AstNodeList;

// ---------------------------------------------------------------------------
// ParamList -- singly-linked list of function parameter names
// ---------------------------------------------------------------------------
typedef struct ParamList {
    char* name;                 // Parameter name (heap-allocated)
    struct ParamList* next;     // Next parameter (NULL = end)
} ParamList;

// ---------------------------------------------------------------------------
// AstNode -- the core AST node structure
//
// Uses a tagged union where 'type' selects which union member is active.
// Each member stores the fields relevant to that node kind.
// ---------------------------------------------------------------------------
struct AstNode {
    AstNodeType type;   // Which kind of node this is
    int line;           // Source line (for error messages)
    union {
        // AST_LITERAL                 → a compile-time constant Value
        Value literal;

        // AST_VAR_REF and AST_VAR_DECL share this layout:
        //   name         → variable name (heap-allocated copy)
        //   initializer  → for VAR_DECL: the initialiser expression (or NULL)
        struct {
            char* name;
            AstNode* initializer;
        } var;

        // AST_ASSIGN_VAR  → name = value
        struct {
            char* name;
            AstNode* value;
        } assign_var;

        // AST_DB_REF_ID  → #realNo  (read)
        struct {
            int realNo;
        } db_ref_id;

        // AST_ASSIGN_DB_REF_ID  → #realNo = value  (write)
        struct {
            int realNo;
            AstNode* value;
        } assign_db_id;

        // AST_DB_REF_LINK  → #(linkNo, devNo, regNo)  (read)
        struct {
            int linkNo;
            int devNo;
            int regNo;
        } db_ref_link;

        // AST_ASSIGN_DB_REF_LINK  → #(linkNo, devNo, regNo) = value  (write)
        struct {
            int linkNo;
            int devNo;
            int regNo;
            AstNode* value;
        } assign_db_link;

        // AST_BINARY  → left op right
        struct {
            TokenType op;       // Operator token (e.g. TOKEN_PLUS, TOKEN_AMPERSAND_AMPERSAND)
            AstNode* left;
            AstNode* right;
        } binary;

        // AST_UNARY  → op operand
        struct {
            TokenType op;       // Operator token (e.g. TOKEN_MINUS, TOKEN_BANG, TOKEN_TILDE)
            AstNode* operand;
        } unary;

        // AST_CALL  → callee(arg1, arg2, ...)
        struct {
            char* callee;           // Function name (heap-allocated)
            AstNodeList* arguments; // Linked list of argument expressions
        } call;

        // AST_EXPR_STMT and AST_RETURN share this layout
        struct {
            AstNode* expr;      // The expression (or NULL for bare return)
        } expr_stmt;

        // AST_FUN_DECL  → fn name(params) { body }
        struct {
            char* name;
            ParamList* parameters;
            AstNode* body;      // Must be AST_BLOCK
        } fun_decl;

        // AST_IF  → if (cond) then_branch [else else_branch]
        struct {
            AstNode* condition;
            AstNode* then_branch;
            AstNode* else_branch;   // NULL if no else clause
        } if_stmt;

        // AST_WHILE  → while (cond) body
        struct {
            AstNode* condition;
            AstNode* body;
        } while_stmt;

        // AST_FOR  → for (init; cond; inc) body
        struct {
            AstNode* initializer;
            AstNode* condition;
            AstNode* increment;
            AstNode* body;
        } for_stmt;

        // AST_BLOCK  → { statements... }
        struct {
            AstNodeList* statements;   // Linked list of statement nodes
        } block;
    } as;
};

// ---------------------------------------------------------------------------
// Constructor functions  (each allocates a new AstNode on the heap)
// ---------------------------------------------------------------------------

// Expression constructors
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

// Statement constructors
AstNode* make_expr_stmt(AstNode* expr, int line);
AstNode* make_var_decl(const char* name, int length, AstNode* initializer, int line);
AstNode* make_fun_decl(const char* name, int length, ParamList* parameters, AstNode* body, int line);
AstNode* make_if(AstNode* condition, AstNode* then_branch, AstNode* else_branch, int line);
AstNode* make_while(AstNode* condition, AstNode* body, int line);
AstNode* make_for(AstNode* initializer, AstNode* condition, AstNode* increment, AstNode* body, int line);
AstNode* make_block(AstNodeList* statements, int line);
AstNode* make_return(AstNode* expr, int line);

// ---------------------------------------------------------------------------
// Destructors  (free entire AST sub-trees recursively)
// ---------------------------------------------------------------------------
void free_ast(AstNode* node);
void free_ast_list(AstNodeList* list);
void free_param_list(ParamList* list);

#endif // AST_H
