#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "heapx/heap.h"
#include "heap_internal.h"

struct bench_item {
    uint64_t key;
    uint64_t id;
    int live;
    struct heapx_handle handle;
};

struct implementation_case {
    enum heapx_implementation implementation;
    const char *name;
};

struct benchmark_result {
    double seconds;
    uint64_t operations;
    uint64_t checksum;
};

enum output_format {
    OUTPUT_TEXT,
    OUTPUT_TSV
};

static const struct implementation_case IMPLEMENTATIONS[] = {
    { HEAPX_BINARY_HEAP, "binary" },
    { HEAPX_FIBONACCI_HEAP, "fibonacci" },
    { HEAPX_KAPLAN_HEAP, "kaplan" }
};

static const uint64_t DEFAULT_SEED = UINT64_C(0x123456789abcdef0);

static int bench_item_cmp(const void *lhs, const void *rhs)
{
    const struct bench_item *left = lhs;
    const struct bench_item *right = rhs;

    if (left->key < right->key)
        return -1;
    if (left->key > right->key)
        return 1;
    if (left->id < right->id)
        return -1;
    if (left->id > right->id)
        return 1;
    return 0;
}

static uint64_t lcg_next(uint64_t *state)
{
    *state = *state * UINT64_C(6364136223846793005) + UINT64_C(1442695040888963407);
    return *state;
}

static double elapsed_seconds(struct timespec start, struct timespec end)
{
    return (double)(end.tv_sec - start.tv_sec) +
        (double)(end.tv_nsec - start.tv_nsec) / 1000000000.0;
}

static int parse_size(const char *text, size_t *value)
{
    char *end;
    unsigned long long parsed;

    errno = 0;
    parsed = strtoull(text, &end, 10);
    if (errno != 0 || *end != '\0' || parsed > (unsigned long long)SIZE_MAX)
        return -1;

    *value = (size_t)parsed;
    return 0;
}

static int parse_uint64(const char *text, uint64_t *value)
{
    char *end;
    unsigned long long parsed;

    errno = 0;
    parsed = strtoull(text, &end, 0);
    if (errno != 0 || *end != '\0')
        return -1;

    *value = (uint64_t)parsed;
    return 0;
}

static uint64_t mix_checksum(uint64_t checksum, const struct bench_item *item)
{
    return checksum ^ (item->key + UINT64_C(0x9e3779b97f4a7c15) +
        (item->id << 6) + (item->id >> 2));
}

static struct bench_item *bench_items_alloc(size_t count)
{
    size_t bytes;

    if (heapx_size_mul(count, sizeof(struct bench_item), &bytes) != 0)
        return NULL;
    if (bytes == 0)
        return calloc(1, 1);

    return calloc(count, sizeof(struct bench_item));
}

static int verify_monotonic(
    const struct bench_item *previous,
    const struct bench_item *current
)
{
    if (previous == NULL)
        return 0;
    return bench_item_cmp(previous, current) <= 0 ? 0 : -1;
}

static int run_insert_extract(
    enum heapx_implementation implementation,
    size_t count,
    uint64_t seed,
    struct benchmark_result *result
)
{
    struct bench_item *items;
    struct heapx_heap *heap;
    struct timespec start;
    struct timespec end;
    uint64_t rng = seed;
    uint64_t checksum = 0;
    const struct bench_item *previous = NULL;
    size_t i;

    items = bench_items_alloc(count);
    heap = heapx_create(implementation, bench_item_cmp);
    if (items == NULL || heap == NULL)
        goto fail;

    clock_gettime(CLOCK_MONOTONIC, &start);

    for (i = 0; i < count; i++) {
        items[i].key = (uint64_t)count * 10 + (lcg_next(&rng) % (count + 1));
        items[i].id = (uint64_t)i;
        if (heapx_insert(heap, &items[i]) != 0)
            goto fail;
    }

    while (!heapx_empty(heap)) {
        const struct bench_item *item = heapx_extract_min(heap);

        if (item == NULL || verify_monotonic(previous, item) != 0)
            goto fail;
        checksum = mix_checksum(checksum, item);
        previous = item;
    }

    clock_gettime(CLOCK_MONOTONIC, &end);

    result->seconds = elapsed_seconds(start, end);
    result->operations = (uint64_t)count * 2;
    result->checksum = checksum;

    heapx_destroy(heap);
    free(items);
    return 0;

fail:
    heapx_destroy(heap);
    free(items);
    return -1;
}

static int run_insert_only(
    enum heapx_implementation implementation,
    size_t count,
    uint64_t seed,
    struct benchmark_result *result
)
{
    struct bench_item *items;
    struct heapx_heap *heap;
    struct timespec start;
    struct timespec end;
    uint64_t rng = seed;
    uint64_t checksum = 0;
    size_t i;

    items = bench_items_alloc(count);
    heap = heapx_create(implementation, bench_item_cmp);
    if (items == NULL || heap == NULL)
        goto fail;

    clock_gettime(CLOCK_MONOTONIC, &start);

    for (i = 0; i < count; i++) {
        items[i].key = (uint64_t)count * 10 + (lcg_next(&rng) % (count + 1));
        items[i].id = (uint64_t)i;
        if (heapx_insert(heap, &items[i]) != 0)
            goto fail;
        checksum = mix_checksum(checksum, &items[i]);
    }

    if (count > 0 && heapx_peek_min(heap) == NULL)
        goto fail;

    clock_gettime(CLOCK_MONOTONIC, &end);

    result->seconds = elapsed_seconds(start, end);
    result->operations = (uint64_t)count;
    result->checksum = checksum;

    heapx_destroy(heap);
    free(items);
    return 0;

fail:
    heapx_destroy(heap);
    free(items);
    return -1;
}

static int run_first_extract(
    enum heapx_implementation implementation,
    size_t count,
    uint64_t seed,
    struct benchmark_result *result
)
{
    struct bench_item *items;
    struct heapx_heap *heap;
    struct timespec start;
    struct timespec end;
    uint64_t rng = seed;
    uint64_t checksum = 0;
    size_t i;

    items = bench_items_alloc(count);
    heap = heapx_create(implementation, bench_item_cmp);
    if (items == NULL || heap == NULL)
        goto fail;

    clock_gettime(CLOCK_MONOTONIC, &start);

    for (i = 0; i < count; i++) {
        items[i].key = (uint64_t)count * 10 + (lcg_next(&rng) % (count + 1));
        items[i].id = (uint64_t)i;
        if (heapx_insert(heap, &items[i]) != 0)
            goto fail;
    }

    if (count > 0) {
        const struct bench_item *item = heapx_extract_min(heap);

        if (item == NULL)
            goto fail;
        checksum = mix_checksum(checksum, item);
    }

    clock_gettime(CLOCK_MONOTONIC, &end);

    result->seconds = elapsed_seconds(start, end);
    result->operations = (uint64_t)count + (count > 0 ? 1u : 0u);
    result->checksum = checksum;

    heapx_destroy(heap);
    free(items);
    return 0;

fail:
    heapx_destroy(heap);
    free(items);
    return -1;
}

static int run_decrease_extract(
    enum heapx_implementation implementation,
    size_t count,
    uint64_t seed,
    struct benchmark_result *result
)
{
    struct bench_item *items;
    struct heapx_heap *heap;
    struct timespec start;
    struct timespec end;
    uint64_t checksum = 0;
    const struct bench_item *previous = NULL;
    size_t i;

    (void)seed;

    items = bench_items_alloc(count);
    heap = heapx_create(implementation, bench_item_cmp);
    if (items == NULL || heap == NULL)
        goto fail;

    clock_gettime(CLOCK_MONOTONIC, &start);

    for (i = 0; i < count; i++) {
        items[i].key = (uint64_t)count * 2 + (uint64_t)i;
        items[i].id = (uint64_t)i;
        if (heapx_insert_handle(heap, &items[i], &items[i].handle) != 0)
            goto fail;
    }

    for (i = 0; i < count; i++) {
        items[i].key = (uint64_t)(count - i);
        if (heapx_decrease_key(heap, items[i].handle) != 0)
            goto fail;
    }

    while (!heapx_empty(heap)) {
        const struct bench_item *item = heapx_extract_min(heap);

        if (item == NULL || verify_monotonic(previous, item) != 0)
            goto fail;
        checksum = mix_checksum(checksum, item);
        previous = item;
    }

    clock_gettime(CLOCK_MONOTONIC, &end);

    result->seconds = elapsed_seconds(start, end);
    result->operations = (uint64_t)count * 3;
    result->checksum = checksum;

    heapx_destroy(heap);
    free(items);
    return 0;

fail:
    heapx_destroy(heap);
    free(items);
    return -1;
}

static int run_decrease_only(
    enum heapx_implementation implementation,
    size_t count,
    uint64_t seed,
    struct benchmark_result *result
)
{
    struct bench_item *items;
    struct heapx_heap *heap;
    struct timespec start;
    struct timespec end;
    uint64_t checksum = 0;
    size_t i;

    (void)seed;

    items = bench_items_alloc(count);
    heap = heapx_create(implementation, bench_item_cmp);
    if (items == NULL || heap == NULL)
        goto fail;

    clock_gettime(CLOCK_MONOTONIC, &start);

    for (i = 0; i < count; i++) {
        items[i].key = (uint64_t)count * 2 + (uint64_t)i;
        items[i].id = (uint64_t)i;
        if (heapx_insert_handle(heap, &items[i], &items[i].handle) != 0)
            goto fail;
    }

    for (i = 0; i < count; i++) {
        items[i].key = (uint64_t)(count - i);
        if (heapx_decrease_key(heap, items[i].handle) != 0)
            goto fail;
        checksum = mix_checksum(checksum, &items[i]);
    }

    if (count > 0) {
        const struct bench_item *item = heapx_peek_min(heap);

        if (item == NULL || item->key != 1)
            goto fail;
    }

    clock_gettime(CLOCK_MONOTONIC, &end);

    result->seconds = elapsed_seconds(start, end);
    result->operations = (uint64_t)count * 2;
    result->checksum = checksum;

    heapx_destroy(heap);
    free(items);
    return 0;

fail:
    heapx_destroy(heap);
    free(items);
    return -1;
}

static int run_mixed_handles(
    enum heapx_implementation implementation,
    size_t count,
    uint64_t seed,
    struct benchmark_result *result
)
{
    struct bench_item *items;
    struct heapx_heap *heap;
    struct timespec start;
    struct timespec end;
    uint64_t rng = seed;
    uint64_t checksum = 0;
    uint64_t operations = 0;
    size_t i;

    items = bench_items_alloc(count);
    heap = heapx_create(implementation, bench_item_cmp);
    if (items == NULL || heap == NULL)
        goto fail;

    clock_gettime(CLOCK_MONOTONIC, &start);

    for (i = 0; i < count; i++) {
        items[i].key = (uint64_t)count * 10 + (lcg_next(&rng) % (count + 1));
        items[i].id = (uint64_t)i;
        items[i].live = 1;
        if (heapx_insert_handle(heap, &items[i], &items[i].handle) != 0)
            goto fail;
        operations++;
    }

    for (i = 0; i < count; i += 5) {
        struct bench_item *removed = heapx_remove(heap, items[i].handle);

        if (removed != &items[i])
            goto fail;
        items[i].live = 0;
        checksum = mix_checksum(checksum, removed);
        operations++;
    }

    for (i = 1; i < count; i += 3) {
        if (!items[i].live)
            continue;
        items[i].key = (uint64_t)i;
        if (heapx_decrease_key(heap, items[i].handle) != 0)
            goto fail;
        operations++;
    }

    while (!heapx_empty(heap)) {
        const struct bench_item *item = heapx_extract_min(heap);

        if (item == NULL)
            goto fail;
        checksum = mix_checksum(checksum, item);
        operations++;
    }

    clock_gettime(CLOCK_MONOTONIC, &end);

    result->seconds = elapsed_seconds(start, end);
    result->operations = operations;
    result->checksum = checksum;

    heapx_destroy(heap);
    free(items);
    return 0;

fail:
    heapx_destroy(heap);
    free(items);
    return -1;
}

static int print_result(
    enum output_format format,
    const char *implementation,
    const char *scenario,
    size_t count,
    uint64_t seed,
    const struct benchmark_result *result
)
{
    double operations_per_second;

    if (result->seconds <= 0.0)
        return -1;

    operations_per_second = (double)result->operations / result->seconds;
    if (format == OUTPUT_TSV) {
        printf(
            "%s\t%s\t%zu\t%" PRIu64 "\t%.9f\t%" PRIu64
            "\t%.0f\t%" PRIu64 "\t%s\n",
            implementation,
            scenario,
            count,
            seed,
            result->seconds,
            result->operations,
            operations_per_second,
            result->checksum,
            __VERSION__
        );
    } else {
        printf(
            "%-9s %-16s n=%zu seconds=%.6f ops=%" PRIu64
            " ops_per_sec=%.0f checksum=%" PRIu64 "\n",
            implementation,
            scenario,
            count,
            result->seconds,
            result->operations,
            operations_per_second,
            result->checksum
        );
    }
    return 0;
}

int main(int argc, char **argv)
{
    size_t count = 100000;
    uint64_t seed = DEFAULT_SEED;
    enum output_format format = OUTPUT_TEXT;
    int count_seen = 0;
    int seed_seen = 0;
    int argi;
    size_t i;

    for (argi = 1; argi < argc; argi++) {
        const char *arg = argv[argi];

        if (strncmp(arg, "--format=", 9) == 0) {
            const char *value = arg + 9;

            if (strcmp(value, "text") == 0) {
                format = OUTPUT_TEXT;
            } else if (strcmp(value, "tsv") == 0) {
                format = OUTPUT_TSV;
            } else {
                fprintf(stderr, "invalid format: %s\n", value);
                return 2;
            }
            continue;
        }

        if (!count_seen) {
            if (parse_size(arg, &count) != 0) {
                fprintf(stderr, "invalid item count: %s\n", arg);
                return 2;
            }
            count_seen = 1;
            continue;
        }

        if (!seed_seen) {
            if (parse_uint64(arg, &seed) != 0) {
                fprintf(stderr, "invalid seed: %s\n", arg);
                return 2;
            }
            seed_seen = 1;
            continue;
        }

        fprintf(stderr, "usage: %s [--format=text|tsv] [item-count] [seed]\n", argv[0]);
        return 2;
    }

    if (format == OUTPUT_TSV)
        printf("implementation\tscenario\tn\tseed\tseconds\tops\tops_per_sec\tchecksum\tcompiler\n");
    else
        printf(
            "config n=%zu seed=%" PRIu64 " compiler=\"%s\"\n",
            count,
            seed,
            __VERSION__
        );

    for (i = 0; i < sizeof(IMPLEMENTATIONS) / sizeof(IMPLEMENTATIONS[0]); i++) {
        struct benchmark_result result;

        if (
            run_insert_only(
                IMPLEMENTATIONS[i].implementation,
                count,
                seed ^ UINT64_C(0x510e527fade682d1),
                &result
            ) != 0 ||
            print_result(
                format,
                IMPLEMENTATIONS[i].name,
                "insert_only",
                count,
                seed,
                &result
            ) != 0
            )
            return 1;

        if (
            run_first_extract(
                IMPLEMENTATIONS[i].implementation,
                count,
                seed ^ UINT64_C(0x9b05688c2b3e6c1f),
                &result
            ) != 0 ||
            print_result(
                format,
                IMPLEMENTATIONS[i].name,
                "first_extract",
                count,
                seed,
                &result
            ) != 0
            )
            return 1;

        if (
            run_insert_extract(
                IMPLEMENTATIONS[i].implementation,
                count,
                seed ^ UINT64_C(0x6a09e667f3bcc909),
                &result
            ) != 0 ||
            print_result(
                format,
                IMPLEMENTATIONS[i].name,
                "insert_extract",
                count,
                seed,
                &result
            ) != 0
            )
            return 1;

        if (
            run_decrease_only(
                IMPLEMENTATIONS[i].implementation,
                count,
                seed ^ UINT64_C(0xa54ff53a5f1d36f1),
                &result
            ) != 0 ||
            print_result(
                format,
                IMPLEMENTATIONS[i].name,
                "decrease_only",
                count,
                seed,
                &result
            ) != 0
            )
            return 1;

        if (
            run_decrease_extract(
                IMPLEMENTATIONS[i].implementation,
                count,
                seed ^ UINT64_C(0xbb67ae8584caa73b),
                &result
            ) != 0 ||
            print_result(
                format,
                IMPLEMENTATIONS[i].name,
                "decrease_extract",
                count,
                seed,
                &result
            ) != 0
            )
            return 1;

        if (
            run_mixed_handles(
                IMPLEMENTATIONS[i].implementation,
                count,
                seed ^ UINT64_C(0x3c6ef372fe94f82b),
                &result
            ) != 0 ||
            print_result(
                format,
                IMPLEMENTATIONS[i].name,
                "mixed_handles",
                count,
                seed,
                &result
            ) != 0
            )
            return 1;
    }

    return 0;
}
