CC := gcc
CFLAGS := -std=c99 -Wall -Wextra -pedantic
CPPFLAGS := -Iinclude -Isrc

BUILD_DIR := build
SANITIZE_BUILD_DIR := build/sanitize
SANITIZE_CFLAGS := $(CFLAGS) -fsanitize=address,undefined -g
DEBUG_CHECK_BUILD_DIR := build/debug-checks
DEBUG_CHECK_CFLAGS := $(CFLAGS) -DHEAPX_ENABLE_INTERNAL_CHECKS
TEST_ENV ?=
LIB := $(BUILD_DIR)/libheapx.a
TARGET_TEST := $(BUILD_DIR)/heap_test
TARGET_DIJKSTRA_BENCHMARK := $(BUILD_DIR)/dijkstra_benchmark
TARGET_HEAP_BENCHMARK := $(BUILD_DIR)/heap_benchmark
DEFAULT_DIMACS_GRAPH := graphs/dimacs/tiny.gr

SRC := src/heap.c src/heaps/binary_heap.c src/heaps/fibonacci_heap.c src/heaps/kaplan_heap.c
OBJ := $(SRC:%.c=$(BUILD_DIR)/%.o)
SRC_TEST := tests/heap_test.c
SRC_DIJKSTRA_BENCHMARK := tests/dijkstra_benchmark.c
SRC_HEAP_BENCHMARK := tests/heap_benchmark.c
PUBLIC_HEADERS := include/heapx/heap.h
INTERNAL_HEADERS := src/heap_internal.h src/heaps/binary_heap.h src/heaps/fibonacci_heap.h src/heaps/kaplan_heap.h

.PHONY: all clean test sanitize debug-checks benchmark benchmark-smoke benchmark-heap benchmark-heap-smoke docs

all: $(LIB)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/%.o: %.c $(PUBLIC_HEADERS) $(INTERNAL_HEADERS) | $(BUILD_DIR)
	mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(LIB): $(OBJ)
	rm -f $(LIB)
	ar rcs $(LIB) $(OBJ)

$(TARGET_TEST): $(SRC_TEST) $(LIB) $(PUBLIC_HEADERS) $(INTERNAL_HEADERS)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SRC_TEST) $(LIB) -o $(TARGET_TEST)

test: $(TARGET_TEST)
	$(TEST_ENV) $(TARGET_TEST)

sanitize:
	$(MAKE) BUILD_DIR=$(SANITIZE_BUILD_DIR) CFLAGS='$(SANITIZE_CFLAGS)' TEST_ENV='ASAN_OPTIONS=detect_leaks=0' test

debug-checks:
	$(MAKE) BUILD_DIR=$(DEBUG_CHECK_BUILD_DIR) CFLAGS='$(DEBUG_CHECK_CFLAGS)' test

$(TARGET_DIJKSTRA_BENCHMARK): $(SRC_DIJKSTRA_BENCHMARK) $(LIB) $(PUBLIC_HEADERS)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SRC_DIJKSTRA_BENCHMARK) $(LIB) -o $(TARGET_DIJKSTRA_BENCHMARK)

benchmark: $(TARGET_DIJKSTRA_BENCHMARK)
	$(TARGET_DIJKSTRA_BENCHMARK) $(if $(GRAPH),$(GRAPH),$(DEFAULT_DIMACS_GRAPH)) $(if $(SOURCE),$(SOURCE),1)

benchmark-smoke: $(TARGET_DIJKSTRA_BENCHMARK)
	$(TARGET_DIJKSTRA_BENCHMARK) $(DEFAULT_DIMACS_GRAPH) 1

$(TARGET_HEAP_BENCHMARK): $(SRC_HEAP_BENCHMARK) $(LIB) $(PUBLIC_HEADERS)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SRC_HEAP_BENCHMARK) $(LIB) -o $(TARGET_HEAP_BENCHMARK)

benchmark-heap: $(TARGET_HEAP_BENCHMARK)
	$(TARGET_HEAP_BENCHMARK) $(if $(FORMAT),--format=$(FORMAT) )$(if $(N),$(N),100000)$(if $(SEED), $(SEED))

benchmark-heap-smoke: $(TARGET_HEAP_BENCHMARK)
	$(TARGET_HEAP_BENCHMARK) $(if $(FORMAT),--format=$(FORMAT) )1000$(if $(SEED), $(SEED))

docs: | $(BUILD_DIR)
	@if ! command -v doxygen >/dev/null 2>&1; then \
		echo "doxygen is required to build the C API documentation"; \
		exit 127; \
	fi
	doxygen Doxyfile

clean:
	rm -rf $(BUILD_DIR)
