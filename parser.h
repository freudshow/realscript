// ============================================================================
// parser.h -- Recursive-Descent Parser Header
//
// The parser sits between the lexer and the compiler in the pipeline:
//   source → lexer → tokens → parser → AST → compiler → bytecode
//
// It consumes the token stream produced by the lexer and builds an
// Abstract Syntax Tree (AstNodeList) using the recursive-descent technique
// with one function per grammar production.
// ============================================================================

#ifndef PARSER_H
#define PARSER_H

#include "ast.h"

// ---------------------------------------------------------------------------
// parse -- entry point: parse the entire source string into an AST
//
// Returns a linked list of top-level statement/declaration AstNode pointers.
// On any parse error, returns NULL (and prints error messages to stderr).
// ---------------------------------------------------------------------------
AstNodeList* parse(const char* source);

#endif // PARSER_H
