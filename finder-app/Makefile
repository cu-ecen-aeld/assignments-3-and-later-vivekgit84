# Define the compiler and flags
CC := gcc
CFLAGS := -Wall -Wextra -Werror

# Define the target executable
TARGET := writer

# Define the source files
SRCS := writer.c

# Define the object files
OBJS := $(SRCS:.c=.o)

# Check if CROSS_COMPILE is specified
ifdef CROSS_COMPILE
CC := $(CROSS_COMPILE)gcc
endif

# Default target to build the application
all: $(TARGET)

# Rule to build the target executable
$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^

# Rule to compile the source files into object files
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# Clean target to remove the executable and object files
clean:
	rm -f $(TARGET) $(OBJS)

.PHONY: all clean
