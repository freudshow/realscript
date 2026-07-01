# ============================================================================
# Makefile -- Build configuration for RealScript
#
# Compiler:  gcc with C99 standard (-std=c99), all warnings enabled
# Flags:     -Wall -Wextra -g (debug symbols)
# Target:    realscript
#
# Compilation order (matters for header dependencies):
#   db → value → lexer → ast → parser → compiler → vm → main
# ============================================================================

CC = gcc
CFLAGS = -Wall -Wextra -std=c99 -g
TARGET = realscript

# All source files in the project
SRCS = db.c value.c lexer.c ast.c parser.c compiler.c vm.c main.c
OBJS = $(SRCS:.c=.o)

# Default target: build the executable
all: $(TARGET)

# Link all object files into the final executable
$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $(TARGET) $(OBJS)

# Compile each .c file into a .o file
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# Clean: remove all build artifacts
clean:
	rm -f $(OBJS) $(TARGET)

.PHONY: all clean
