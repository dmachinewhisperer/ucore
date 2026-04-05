CC = gcc
CFLAGS = -Wall -Wextra -std=c99 -g -I../include

SRCS = test_jmp_bin.c ../jmp_bin.c ../utils.c
TARGET = test_jmp_bin

.PHONY: all clean test

all: $(TARGET)

$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) -o $@ $^

test: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET)
