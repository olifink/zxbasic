CC ?= gcc
CFLAGS ?= -std=c99 -Wall -Wextra -pedantic -O2 -g
LDFLAGS ?= -lm

SRC_DIR = src
TEST_DIR = tests
BUILD_DIR = build

SRCS = $(SRC_DIR)/main.c $(SRC_DIR)/program.c $(SRC_DIR)/error.c $(SRC_DIR)/value.c $(SRC_DIR)/symtab.c $(SRC_DIR)/lexer.c $(SRC_DIR)/expr.c $(SRC_DIR)/parser.c $(SRC_DIR)/runtime.c $(SRC_DIR)/normalize.c $(SRC_DIR)/linenoise.c
OBJS = $(BUILD_DIR)/main.o $(BUILD_DIR)/program.o $(BUILD_DIR)/error.o $(BUILD_DIR)/value.o $(BUILD_DIR)/symtab.o $(BUILD_DIR)/lexer.o $(BUILD_DIR)/expr.o $(BUILD_DIR)/parser.o $(BUILD_DIR)/runtime.o $(BUILD_DIR)/normalize.o $(BUILD_DIR)/linenoise.o

TARGET = zxbasic

all: $(TARGET)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

# Test targets
TEST_STORE_BIN = $(BUILD_DIR)/test_store
TEST_EXPR_BIN = $(BUILD_DIR)/test_expr
TEST_RUNTIME_BIN = $(BUILD_DIR)/test_runtime
TEST_V2_BIN = $(BUILD_DIR)/test_v2

CORE_OBJS = $(BUILD_DIR)/program.o $(BUILD_DIR)/error.o $(BUILD_DIR)/value.o $(BUILD_DIR)/symtab.o $(BUILD_DIR)/lexer.o $(BUILD_DIR)/expr.o $(BUILD_DIR)/parser.o $(BUILD_DIR)/runtime.o $(BUILD_DIR)/normalize.o $(BUILD_DIR)/linenoise.o

$(TEST_STORE_BIN): $(TEST_DIR)/test_store.c $(BUILD_DIR)/program.o $(BUILD_DIR)/lexer.o $(BUILD_DIR)/error.o | $(BUILD_DIR)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

$(TEST_EXPR_BIN): $(TEST_DIR)/test_expr.c $(CORE_OBJS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

$(TEST_RUNTIME_BIN): $(TEST_DIR)/test_runtime.c $(CORE_OBJS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

$(TEST_V2_BIN): $(TEST_DIR)/test_v2.c $(CORE_OBJS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

test: $(TEST_STORE_BIN) $(TEST_EXPR_BIN) $(TEST_RUNTIME_BIN) $(TEST_V2_BIN)
	@echo "Running test_store..."
	./$(TEST_STORE_BIN)
	@echo "Running test_expr..."
	./$(TEST_EXPR_BIN)
	@echo "Running test_runtime..."
	./$(TEST_RUNTIME_BIN)
	@echo "Running test_v2..."
	./$(TEST_V2_BIN)
	@echo "All tests passed!"

clean:
	rm -rf $(BUILD_DIR) $(TARGET)

.PHONY: all test clean
