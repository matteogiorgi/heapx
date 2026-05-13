CC := gcc
CFLAGS := -std=c99 -Wall -Wextra -pedantic
CPPFLAGS := -Iinclude -Isrc

BUILD_DIR := build
SANITIZE_BUILD_DIR := build/sanitize
SANITIZE_CFLAGS := $(CFLAGS) -fsanitize=address,undefined -g
LEAK_CHECK_BUILD_DIR := build/leak-check
DEBUG_CHECK_BUILD_DIR := build/debug-checks
DEBUG_CHECK_CFLAGS := $(CFLAGS) -DHEAPX_ENABLE_INTERNAL_CHECKS
OPTIMIZED_BUILD_DIR := build/optimized
OPTIMIZED_CFLAGS := $(CFLAGS) -O2 -DNDEBUG
TEST_ENV ?=
LIB := $(BUILD_DIR)/libheapx.a
TARGET_TEST := $(BUILD_DIR)/heap_test
TARGET_DIJKSTRA_BENCHMARK := $(BUILD_DIR)/dijkstra_benchmark
TARGET_HEAP_BENCHMARK := $(BUILD_DIR)/heap_benchmark
DEFAULT_DIMACS_GRAPH := graphs/dimacs/tiny.gr
BENCHMARK_DIR := benchmarks
BENCHMARK_HEAP_N ?= $(if $(N),$(N),100000)
BENCHMARK_HEAP_SEED ?= $(if $(SEED),$(SEED),1311768467463790320)
BENCHMARK_HEAP_TSV ?= $(BENCHMARK_DIR)/heap-n$(BENCHMARK_HEAP_N)-seed$(BENCHMARK_HEAP_SEED).tsv

SRC := src/heap.c src/heaps/binary_heap.c src/heaps/fibonacci_heap.c src/heaps/kaplan_heap.c
OBJ := $(SRC:%.c=$(BUILD_DIR)/%.o)
SRC_TEST := tests/heap_test.c
SRC_DIJKSTRA_BENCHMARK := tests/dijkstra_benchmark.c
SRC_HEAP_BENCHMARK := tests/heap_benchmark.c
PUBLIC_HEADERS := include/heapx/heap.h
INTERNAL_HEADERS := src/heap_internal.h src/heaps/binary_heap.h src/heaps/fibonacci_heap.h src/heaps/kaplan_heap.h

.PHONY: all clean test sanitize leak-check debug-checks optimized-check check ci benchmark benchmark-smoke benchmark-heap benchmark-heap-smoke benchmark-heap-tsv benchmark-heap-compare docs

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

leak-check:
	$(MAKE) BUILD_DIR=$(LEAK_CHECK_BUILD_DIR) CFLAGS='$(SANITIZE_CFLAGS)' TEST_ENV='ASAN_OPTIONS=detect_leaks=1' test

debug-checks:
	$(MAKE) BUILD_DIR=$(DEBUG_CHECK_BUILD_DIR) CFLAGS='$(DEBUG_CHECK_CFLAGS)' test

optimized-check:
	$(MAKE) BUILD_DIR=$(OPTIMIZED_BUILD_DIR) CFLAGS='$(OPTIMIZED_CFLAGS)' test benchmark-smoke benchmark-heap-smoke

check: test debug-checks sanitize benchmark-smoke benchmark-heap-smoke

ci: check optimized-check

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

benchmark-heap-tsv: $(TARGET_HEAP_BENCHMARK)
	mkdir -p $(dir $(BENCHMARK_HEAP_TSV))
	$(TARGET_HEAP_BENCHMARK) --format=tsv $(BENCHMARK_HEAP_N) $(BENCHMARK_HEAP_SEED) > $(BENCHMARK_HEAP_TSV)
	@echo "wrote $(BENCHMARK_HEAP_TSV)"

benchmark-heap-compare:
	@if [ -z "$(BASE)" ] || [ -z "$(HEAD)" ]; then \
		echo "usage: make benchmark-heap-compare BASE=old.tsv HEAD=new.tsv"; \
		exit 2; \
	fi
	@awk -F '\t' ' \
		NR == FNR { \
			if (FNR > 1) base[$$1 "\t" $$2] = $$7; \
			next; \
		} \
		FNR == 1 { \
			printf "%-10s %-17s %14s %14s %9s\n", "impl", "scenario", "base_ops/s", "head_ops/s", "change"; \
			next; \
		} \
		{ \
			key = $$1 "\t" $$2; \
			if (key in base && base[key] > 0) \
				printf "%-10s %-17s %14.0f %14.0f %+8.2f%%\n", $$1, $$2, base[key], $$7, (($$7 / base[key]) - 1) * 100; \
		} \
	' $(BASE) $(HEAD)

docs: | $(BUILD_DIR)
	@if ! command -v doxygen >/dev/null 2>&1; then \
		echo "doxygen is required to build the C API documentation"; \
		exit 127; \
	fi
	doxygen Doxyfile

clean:
	rm -rf $(BUILD_DIR)
