# Variables
CC = gcc
CFLAGS = -O3 -msse4 -g -march=native
TARGET = re_banks
SRC = re_banks.c

# Default target
all: $(TARGET)

# Build the target
$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRC)

# Clean up build artifacts
clean:
	rm -f $(TARGET)

# Phony targets
.PHONY: all clean
