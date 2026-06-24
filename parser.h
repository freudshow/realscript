#ifndef PARSER_H
#define PARSER_H

#include "ast.h"

// Parses the source program and returns a list of AST nodes (statements).
// If parsing fails, returns NULL and prints errors.
AstNodeList* parse(const char* source);

#endif // PARSER_H
