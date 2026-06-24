CC = gcc
CFLAGS = -Wall -Wextra -std=c99 -g
TARGET = realscript

SRCS = db.c value.c lexer.c ast.c parser.c compiler.c vm.c main.c
OBJS = $(SRCS:.c=.o)

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $(TARGET) $(OBJS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)

.PHONY: all clean
