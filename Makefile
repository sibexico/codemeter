CC ?= gcc
CFLAGS ?= -std=c23 -O2 -Wall -Wextra -pedantic
TARGET ?= codemeter

ifeq ($(OS),Windows_NT)
TARGET := codemeter.exe
endif

.PHONY: all clean run

all: $(TARGET)

$(TARGET): codemeter.c
	$(CC) $(CFLAGS) codemeter.c -o $(TARGET)

run: $(TARGET)
	./$(TARGET) .

clean:
	$(RM) $(TARGET)
