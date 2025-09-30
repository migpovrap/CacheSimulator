CC := gcc
CFLAGS := -Wall -Wextra -O3

BUILD_DIR := build

SRC_DIR := src
BENCH_DIR := benchmarks

EXEC := $(BUILD_DIR)/tlbsim

SRCS := $(wildcard $(SRC_DIR)/*.c)
OBJS := $(patsubst $(SRC_DIR)/%.c, $(BUILD_DIR)/%.o, $(SRCS))
HEADERS := $(wildcard $(SRC_DIR)/*.h)

.PHONY: all clean

all: $(EXEC)

directories:
	@mkdir -p $(BUILD_DIR)

$(EXEC): $(OBJS) | directories
	$(CC) $(CFLAGS) $^ -o $@

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c $(HEADERS) | directories
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	@rm -rf $(BUILD_DIR)