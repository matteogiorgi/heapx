#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

#include "heapx/heap.h"
#include "heap_internal.h"

static void assert_heap_invariants(const struct heapx_heap *heap)
{
    assert(heapx_check_invariants(heap) == 0);
}

static unsigned test_seed_from_env(void)
{
    const char *text = getenv("HEAPX_TEST_SEED");
    char *end;
    unsigned long parsed;

    if (text == NULL || *text == '\0')
        return 0x5eed1234u;

    errno = 0;
    parsed = strtoul(text, &end, 0);
    assert(errno == 0);
    assert(*end == '\0');
    assert(parsed <= UINT_MAX);

    return (unsigned)parsed;
}

static size_t test_size_from_env(const char *name, size_t default_value)
{
    const char *text = getenv(name);
    char *end;
    unsigned long parsed;

    if (text == NULL || *text == '\0')
        return default_value;

    errno = 0;
    parsed = strtoul(text, &end, 0);
    assert(errno == 0);
    assert(*end == '\0');
    assert(parsed > 0);
    assert((unsigned long)(size_t)parsed == parsed);

    return (size_t)parsed;
}

static int int_cmp(const void *lhs, const void *rhs)
{
    const int *left = lhs;
    const int *right = rhs;

    if (*left < *right)
        return -1;
    if (*left > *right)
        return 1;

    return 0;
}

struct keyed_value {
    int id;
    int priority;
};

static int keyed_value_cmp(const void *lhs, const void *rhs)
{
    const struct keyed_value *left = lhs;
    const struct keyed_value *right = rhs;

    if (left->priority < right->priority)
        return -1;
    if (left->priority > right->priority)
        return 1;

    return 0;
}

static void test_create_rejects_missing_cmp(void)
{
    struct heapx_handle invalid = { 0, 0, 0 };

    assert(heapx_create(HEAPX_BINARY_HEAP, NULL) == NULL);
    assert(heapx_create(HEAPX_FIBONACCI_HEAP, NULL) == NULL);
    assert(heapx_create(HEAPX_KAPLAN_HEAP, NULL) == NULL);

    assert(heapx_contains(NULL, NULL) == 0);
    assert(heapx_remove(NULL, invalid) == NULL);
    assert(heapx_decrease_key(NULL, invalid) == -1);
}

static void test_empty_heap(enum heapx_implementation implementation)
{
    struct heapx_heap *heap;

    heap = heapx_create(implementation, int_cmp);
    assert(heap != NULL);

    assert(heapx_empty(heap));
    assert(heapx_size(heap) == 0);
    assert(heapx_peek_min(heap) == NULL);
    assert(heapx_extract_min(heap) == NULL);
    assert_heap_invariants(heap);

    heapx_destroy(heap);
}

static void test_insert_extract_order(enum heapx_implementation implementation)
{
    struct heapx_heap *heap;
    int values[] = { 7, 3, 9, 1, 4, 8, 2 };
    int expected[] = { 1, 2, 3, 4, 7, 8, 9 };
    size_t i;

    heap = heapx_create(implementation, int_cmp);
    assert(heap != NULL);

    for (i = 0; i < sizeof(values) / sizeof(values[0]); i++)
        assert(heapx_insert(heap, &values[i]) == 0);
    assert_heap_invariants(heap);

    assert(heapx_size(heap) == sizeof(values) / sizeof(values[0]));
    assert(*(int *) heapx_peek_min(heap) == 1);

    for (i = 0; i < sizeof(expected) / sizeof(expected[0]); i++)
        assert(*(int *) heapx_extract_min(heap) == expected[i]);
    assert_heap_invariants(heap);

    assert(heapx_empty(heap));
    heapx_destroy(heap);
}

static void test_duplicates(enum heapx_implementation implementation)
{
    struct heapx_heap *heap;
    int values[] = { 5, 1, 5, 1, 3 };
    int expected[] = { 1, 1, 3, 5, 5 };
    size_t i;

    heap = heapx_create(implementation, int_cmp);
    assert(heap != NULL);

    for (i = 0; i < sizeof(values) / sizeof(values[0]); i++)
        assert(heapx_insert(heap, &values[i]) == 0);
    assert_heap_invariants(heap);

    for (i = 0; i < sizeof(expected) / sizeof(expected[0]); i++)
        assert(*(int *) heapx_extract_min(heap) == expected[i]);
    assert_heap_invariants(heap);

    heapx_destroy(heap);
}

static void test_interleaved_operations(
    enum heapx_implementation implementation
)
{
    struct heapx_heap *heap;
    int values[] = { 10, 4, 7, 1, 6, 3 };

    heap = heapx_create(implementation, int_cmp);
    assert(heap != NULL);

    assert(heapx_insert(heap, &values[0]) == 0);
    assert(heapx_insert(heap, &values[1]) == 0);
    assert(heapx_insert(heap, &values[2]) == 0);
    assert_heap_invariants(heap);
    assert(*(int *) heapx_extract_min(heap) == 4);
    assert_heap_invariants(heap);

    assert(heapx_insert(heap, &values[3]) == 0);
    assert(heapx_insert(heap, &values[4]) == 0);
    assert_heap_invariants(heap);
    assert(*(int *) heapx_extract_min(heap) == 1);
    assert_heap_invariants(heap);

    assert(heapx_insert(heap, &values[5]) == 0);
    assert_heap_invariants(heap);
    assert(*(int *) heapx_extract_min(heap) == 3);
    assert(*(int *) heapx_extract_min(heap) == 6);
    assert(*(int *) heapx_extract_min(heap) == 7);
    assert(*(int *) heapx_extract_min(heap) == 10);
    assert(heapx_extract_min(heap) == NULL);
    assert_heap_invariants(heap);

    heapx_destroy(heap);
}

static void test_larger_deterministic_order(
    enum heapx_implementation implementation
)
{
    struct heapx_heap *heap;
    int values[256];
    int expected[256];
    size_t i;
    size_t value;

    heap = heapx_create(implementation, int_cmp);
    assert(heap != NULL);

    for (i = 0; i < 256; i++) {
        values[i] = (int)((i * 37) % 101);
        assert(heapx_insert(heap, &values[i]) == 0);
    }
    assert_heap_invariants(heap);

    value = 0;
    for (i = 0; i < 256; i++) {
        expected[i] = (int)value;
        value += 37;
        value %= 101;
    }

    for (i = 1; i < 256; i++) {
        size_t j = i;
        while (j > 0 && expected[j] < expected[j - 1]) {
            int tmp = expected[j];
            expected[j] = expected[j - 1];
            expected[j - 1] = tmp;
            j--;
        }
    }

    for (i = 0; i < 256; i++)
        assert(*(int *) heapx_extract_min(heap) == expected[i]);
    assert_heap_invariants(heap);

    assert(heapx_empty(heap));
    heapx_destroy(heap);
}

static void test_contains_and_remove(
    enum heapx_implementation implementation
)
{
    struct heapx_heap *heap;
    struct heapx_handle handles[5];
    int values[] = { 7, 3, 9, 1, 4 };
    int missing = 42;
    size_t i;

    heap = heapx_create(implementation, int_cmp);
    assert(heap != NULL);

    assert(heapx_contains(heap, &values[2]) == 0);
    assert(heapx_remove(heap, (struct heapx_handle){ 0, 0, 0 }) == NULL);

    for (i = 0; i < sizeof(values) / sizeof(values[0]); i++)
        assert(heapx_insert_handle(heap, &values[i], &handles[i]) == 0);
    assert_heap_invariants(heap);

    assert(heapx_contains(heap, &values[2]) != 0);
    assert(heapx_contains(heap, &missing) == 0);
    assert(heapx_remove(heap, handles[2]) == &values[2]);
    assert_heap_invariants(heap);
    assert(heapx_contains(heap, &values[2]) == 0);
    assert(heapx_size(heap) == 4);

    assert(*(int *) heapx_extract_min(heap) == 1);
    assert(*(int *) heapx_extract_min(heap) == 3);
    assert(*(int *) heapx_extract_min(heap) == 4);
    assert(*(int *) heapx_extract_min(heap) == 7);
    assert(heapx_empty(heap));
    assert_heap_invariants(heap);

    heapx_destroy(heap);
}

static void test_remove_uses_handle_identity(
    enum heapx_implementation implementation
)
{
    struct heapx_heap *heap;
    struct keyed_value first = { 1, 5 };
    struct keyed_value second = { 2, 5 };
    struct keyed_value third = { 3, 9 };
    struct heapx_handle second_handle;
    struct keyed_value *item;

    heap = heapx_create(implementation, keyed_value_cmp);
    assert(heap != NULL);

    assert(heapx_insert(heap, &first) == 0);
    assert(heapx_insert_handle(heap, &second, &second_handle) == 0);
    assert(heapx_insert(heap, &third) == 0);
    assert_heap_invariants(heap);

    assert(heapx_remove(heap, second_handle) == &second);
    assert_heap_invariants(heap);
    assert(heapx_contains(heap, &first) != 0);
    assert(heapx_contains(heap, &second) == 0);

    item = heapx_extract_min(heap);
    assert(item == &first);
    item = heapx_extract_min(heap);
    assert(item == &third);

    assert(heapx_empty(heap));
    assert_heap_invariants(heap);
    heapx_destroy(heap);
}

static void test_invalidated_handles_are_rejected(
    enum heapx_implementation implementation
)
{
    struct heapx_heap *heap;
    int values[] = { 1, 2, 3, 4 };
    struct heapx_handle min_handle;
    struct heapx_handle middle_handle;
    struct heapx_handle max_handle;
    struct heapx_handle reused_handle;

    heap = heapx_create(implementation, int_cmp);
    assert(heap != NULL);

    assert(heapx_insert_handle(heap, &values[0], &min_handle) == 0);
    assert(heapx_insert_handle(heap, &values[1], &middle_handle) == 0);
    assert(heapx_insert_handle(heap, &values[2], &max_handle) == 0);
    assert_heap_invariants(heap);

    assert(heapx_extract_min(heap) == &values[0]);
    assert_heap_invariants(heap);
    assert(heapx_decrease_key(heap, min_handle) == -1);
    assert(heapx_remove(heap, min_handle) == NULL);

    assert(heapx_remove(heap, middle_handle) == &values[1]);
    assert_heap_invariants(heap);
    assert(heapx_decrease_key(heap, middle_handle) == -1);
    assert(heapx_remove(heap, middle_handle) == NULL);

    assert(heapx_insert_handle(heap, &values[3], &reused_handle) == 0);
    assert_heap_invariants(heap);
    assert(heapx_decrease_key(heap, middle_handle) == -1);
    assert(heapx_remove(heap, middle_handle) == NULL);
    assert(reused_handle.slot == middle_handle.slot);
    assert(reused_handle.generation != middle_handle.generation);
    assert(heapx_remove(heap, reused_handle) == &values[3]);
    assert_heap_invariants(heap);

    values[2] = 0;
    assert(heapx_decrease_key(heap, max_handle) == 0);
    assert_heap_invariants(heap);
    assert(heapx_extract_min(heap) == &values[2]);
    assert_heap_invariants(heap);

    assert(heapx_empty(heap));
    heapx_destroy(heap);
}

static void test_handles_reject_wrong_heap(
    enum heapx_implementation implementation
)
{
    struct heapx_heap *first_heap;
    struct heapx_heap *second_heap;
    int first = 5;
    int second = 10;
    struct heapx_handle first_handle;
    struct heapx_handle second_handle;

    first_heap = heapx_create(implementation, int_cmp);
    second_heap = heapx_create(implementation, int_cmp);
    assert(first_heap != NULL);
    assert(second_heap != NULL);

    assert(heapx_insert_handle(first_heap, &first, &first_handle) == 0);
    assert(heapx_insert_handle(second_heap, &second, &second_handle) == 0);
    assert_heap_invariants(first_heap);
    assert_heap_invariants(second_heap);

    assert(heapx_decrease_key(second_heap, first_handle) == -1);
    assert(heapx_remove(second_heap, first_handle) == NULL);
    assert(heapx_size(first_heap) == 1);
    assert(heapx_size(second_heap) == 1);
    assert(heapx_peek_min(first_heap) == &first);
    assert(heapx_peek_min(second_heap) == &second);

    assert(heapx_remove(first_heap, first_handle) == &first);
    assert(heapx_remove(second_heap, second_handle) == &second);
    assert_heap_invariants(first_heap);
    assert_heap_invariants(second_heap);

    heapx_destroy(first_heap);
    heapx_destroy(second_heap);
}

static void test_decrease_key(
    enum heapx_implementation implementation
)
{
    struct heapx_heap *heap;
    struct keyed_value slow = { 1, 30 };
    struct keyed_value fast = { 2, 10 };
    struct keyed_value middle = { 3, 20 };
    struct keyed_value missing = { 4, 1 };
    struct heapx_handle slow_handle;
    struct keyed_value *item;

    heap = heapx_create(implementation, keyed_value_cmp);
    assert(heap != NULL);

    assert(heapx_insert_handle(heap, &slow, &slow_handle) == 0);
    assert(heapx_insert(heap, &fast) == 0);
    assert(heapx_insert(heap, &middle) == 0);
    assert_heap_invariants(heap);
    assert(heapx_peek_min(heap) == &fast);

    slow.priority = 5;
    assert(heapx_decrease_key(heap, slow_handle) == 0);
    assert_heap_invariants(heap);
    assert(heapx_peek_min(heap) == &slow);
    assert(
        heapx_decrease_key(heap, (struct heapx_handle){ 0, 0, 0 }) == -1
    );
    assert(heapx_contains(heap, &missing) == 0);

    item = heapx_extract_min(heap);
    assert(item == &slow);
    item = heapx_extract_min(heap);
    assert(item == &fast);
    item = heapx_extract_min(heap);
    assert(item == &middle);

    assert(heapx_empty(heap));
    assert_heap_invariants(heap);
    heapx_destroy(heap);
}

static void test_targeted_operations_after_restructuring(
    enum heapx_implementation implementation
)
{
    struct heapx_heap *heap;
    struct keyed_value values[] = {
        { 0, 40 },
        { 1, 10 },
        { 2, 70 },
        { 3, 20 },
        { 4, 60 },
        { 5, 30 },
        { 6, 50 },
        { 7, 80 },
        { 8, 90 },
        { 9, 100 },
        { 10, 110 },
        { 11, 120 }
    };
    struct keyed_value *item;
    struct heapx_handle handles[12];
    int previous_priority;
    size_t i;
    size_t extracted = 0;

    heap = heapx_create(implementation, keyed_value_cmp);
    assert(heap != NULL);

    for (i = 0; i < sizeof(values) / sizeof(values[0]); i++)
        assert(heapx_insert_handle(heap, &values[i], &handles[i]) == 0);
    assert_heap_invariants(heap);

    item = heapx_extract_min(heap);
    assert(item->priority == 10);
    assert_heap_invariants(heap);

    assert(heapx_remove(heap, handles[6]) == &values[6]);
    assert(heapx_remove(heap, handles[10]) == &values[10]);
    assert_heap_invariants(heap);
    assert(heapx_contains(heap, &values[6]) == 0);
    assert(heapx_contains(heap, &values[10]) == 0);

    values[9].priority = 5;
    assert(heapx_decrease_key(heap, handles[9]) == 0);
    values[2].priority = 15;
    assert(heapx_decrease_key(heap, handles[2]) == 0);
    assert_heap_invariants(heap);

    previous_priority = -1;
    while (!heapx_empty(heap)) {
        item = heapx_extract_min(heap);
        assert(item->priority >= previous_priority);
        previous_priority = item->priority;
        extracted++;
    }

    assert(extracted == 9);
    assert_heap_invariants(heap);
    heapx_destroy(heap);
}

static unsigned next_random(unsigned *state)
{
    *state = *state * 1103515245u + 12345u;
    return *state;
}

static size_t count_active_items(const int *active, size_t count)
{
    size_t active_count = 0;
    size_t i;

    for (i = 0; i < count; i++) {
        if (active[i])
            active_count++;
    }

    return active_count;
}

static int find_min_active_item(
    const struct keyed_value *values,
    const int *active,
    size_t count,
    size_t *index
)
{
    int found = 0;
    size_t min_index = 0;
    size_t i;

    for (i = 0; i < count; i++) {
        if (!active[i])
            continue;

        if (!found || keyed_value_cmp(&values[i], &values[min_index]) < 0) {
            min_index = i;
            found = 1;
        }
    }

    if (!found)
        return -1;

    *index = min_index;
    return 0;
}

static int select_nth_active_item(
    const int *active,
    size_t count,
    size_t ordinal,
    size_t *index
)
{
    size_t seen = 0;
    size_t i;

    for (i = 0; i < count; i++) {
        if (!active[i])
            continue;

        if (seen == ordinal) {
            *index = i;
            return 0;
        }
        seen++;
    }

    return -1;
}

static int select_nth_inactive_item(
    const int *active,
    size_t count,
    size_t ordinal,
    size_t *index
)
{
    size_t seen = 0;
    size_t i;

    for (i = 0; i < count; i++) {
        if (active[i])
            continue;

        if (seen == ordinal) {
            *index = i;
            return 0;
        }
        seen++;
    }

    return -1;
}

static void assert_randomized_heap_matches_oracle(
    struct heapx_heap *heap,
    const struct keyed_value *values,
    const int *active,
    size_t count
)
{
    size_t active_count = count_active_items(active, count);
    size_t min_index;
    struct keyed_value *peeked;

    assert(heapx_size(heap) == active_count);
    assert(heapx_empty(heap) == (active_count == 0));

    if (active_count == 0) {
        assert(heapx_peek_min(heap) == NULL);
        return;
    }

    assert(find_min_active_item(values, active, count, &min_index) == 0);
    peeked = heapx_peek_min(heap);
    assert(peeked != NULL);
    assert(peeked->priority == values[min_index].priority);
}

static void test_randomized_against_oracle(
    enum heapx_implementation implementation,
    unsigned base_seed
)
{
    size_t item_count = test_size_from_env("HEAPX_TEST_ITEMS", 96);
    size_t step_count = test_size_from_env("HEAPX_TEST_STEPS", 2500);
    struct heapx_heap *heap;
    struct keyed_value *values;
    struct heapx_handle *handles;
    int *active;
    unsigned random_state = base_seed + (unsigned)implementation;
    size_t i;
    size_t step;

    assert(item_count <= (size_t)INT_MAX);

    values = malloc(item_count * sizeof(*values));
    handles = malloc(item_count * sizeof(*handles));
    active = malloc(item_count * sizeof(*active));
    assert(values != NULL);
    assert(handles != NULL);
    assert(active != NULL);

    heap = heapx_create(implementation, keyed_value_cmp);
    assert(heap != NULL);

    for (i = 0; i < item_count; i++) {
        values[i].id = (int)i;
        values[i].priority = 0;
        active[i] = 0;
    }

    for (step = 0; step < step_count; step++) {
        size_t active_count = count_active_items(active, item_count);
        unsigned choice = next_random(&random_state) % 100u;
        size_t index;

        if (active_count == 0 || choice < 35u) {
            size_t inactive_count = item_count - active_count;

            if (inactive_count == 0)
                continue;

            assert(
                select_nth_inactive_item(
                    active,
                    item_count,
                    next_random(&random_state) % inactive_count,
                    &index
                ) == 0
            );
            values[index].priority =
                (int)(next_random(&random_state) % 1000u) - 500;
            assert(
                heapx_insert_handle(
                    heap,
                    &values[index],
                    &handles[index]
                ) == 0
            );
            active[index] = 1;
        } else if (choice < 65u) {
            assert(
                select_nth_active_item(
                    active,
                    item_count,
                    next_random(&random_state) % active_count,
                    &index
                ) == 0
            );
            values[index].priority -=
                1 + (int)(next_random(&random_state) % 50u);
            assert(heapx_decrease_key(heap, handles[index]) == 0);
        } else if (choice < 82u) {
            struct keyed_value *removed;

            assert(
                select_nth_active_item(
                    active,
                    item_count,
                    next_random(&random_state) % active_count,
                    &index
                ) == 0
            );
            removed = heapx_remove(heap, handles[index]);
            assert(removed == &values[index]);
            active[index] = 0;
        } else {
            size_t min_index;
            struct keyed_value *extracted;

            assert(find_min_active_item(values, active, item_count, &min_index) == 0);
            extracted = heapx_extract_min(heap);
            assert(extracted != NULL);
            assert(extracted->priority == values[min_index].priority);
            active[extracted->id] = 0;
        }

        assert_randomized_heap_matches_oracle(
            heap,
            values,
            active,
            item_count
        );
        assert_heap_invariants(heap);
    }

    while (!heapx_empty(heap)) {
        size_t min_index;
        struct keyed_value *extracted;

        assert(find_min_active_item(values, active, item_count, &min_index) == 0);
        extracted = heapx_extract_min(heap);
        assert(extracted != NULL);
        assert(extracted->priority == values[min_index].priority);
        active[extracted->id] = 0;
    }

    assert(count_active_items(active, item_count) == 0);
    assert_heap_invariants(heap);
    heapx_destroy(heap);
    free(active);
    free(handles);
    free(values);
}

int main(void)
{
    enum heapx_implementation implementations[] = {
        HEAPX_BINARY_HEAP,
        HEAPX_FIBONACCI_HEAP,
        HEAPX_KAPLAN_HEAP
    };
    unsigned base_seed = test_seed_from_env();
    size_t i;

    fprintf(stderr, "heapx test seed=%u\n", base_seed);

    test_create_rejects_missing_cmp();

    for (i = 0; i < sizeof(implementations) / sizeof(implementations[0]); i++) {
        test_empty_heap(implementations[i]);
        test_insert_extract_order(implementations[i]);
        test_duplicates(implementations[i]);
        test_interleaved_operations(implementations[i]);
        test_larger_deterministic_order(implementations[i]);
        test_contains_and_remove(implementations[i]);
        test_remove_uses_handle_identity(implementations[i]);
        test_invalidated_handles_are_rejected(implementations[i]);
        test_handles_reject_wrong_heap(implementations[i]);
        test_decrease_key(implementations[i]);
        test_targeted_operations_after_restructuring(implementations[i]);
        test_randomized_against_oracle(implementations[i], base_seed);
    }

    printf("All heapx heap tests passed\n");
    return 0;
}
