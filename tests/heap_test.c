#include <assert.h>
#include <stddef.h>
#include <stdio.h>

#include "heapx/heap.h"

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
    assert(heapx_create(HEAPX_BINARY_HEAP, NULL) == NULL);
    assert(heapx_create(HEAPX_FIBONACCI_HEAP, NULL) == NULL);
    assert(heapx_create(HEAPX_KAPLAN_HEAP, NULL) == NULL);

    assert(heapx_contains(NULL, NULL) == 0);
    assert(heapx_remove(NULL, NULL) == NULL);
    assert(heapx_decrease_key(NULL, NULL) == -1);
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

    assert(heapx_size(heap) == sizeof(values) / sizeof(values[0]));
    assert(*(int *) heapx_peek_min(heap) == 1);

    for (i = 0; i < sizeof(expected) / sizeof(expected[0]); i++)
        assert(*(int *) heapx_extract_min(heap) == expected[i]);

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

    for (i = 0; i < sizeof(expected) / sizeof(expected[0]); i++)
        assert(*(int *) heapx_extract_min(heap) == expected[i]);

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
    assert(*(int *) heapx_extract_min(heap) == 4);

    assert(heapx_insert(heap, &values[3]) == 0);
    assert(heapx_insert(heap, &values[4]) == 0);
    assert(*(int *) heapx_extract_min(heap) == 1);

    assert(heapx_insert(heap, &values[5]) == 0);
    assert(*(int *) heapx_extract_min(heap) == 3);
    assert(*(int *) heapx_extract_min(heap) == 6);
    assert(*(int *) heapx_extract_min(heap) == 7);
    assert(*(int *) heapx_extract_min(heap) == 10);
    assert(heapx_extract_min(heap) == NULL);

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

    assert(heapx_empty(heap));
    heapx_destroy(heap);
}

static void test_contains_and_remove(
    enum heapx_implementation implementation
)
{
    struct heapx_heap *heap;
    struct heapx_handle *handles[5];
    int values[] = { 7, 3, 9, 1, 4 };
    int missing = 42;
    size_t i;

    heap = heapx_create(implementation, int_cmp);
    assert(heap != NULL);

    assert(heapx_contains(heap, &values[2]) == 0);
    assert(heapx_remove(heap, NULL) == NULL);

    for (i = 0; i < sizeof(values) / sizeof(values[0]); i++) {
        handles[i] = heapx_insert_handle(heap, &values[i]);
        assert(handles[i] != NULL);
    }

    assert(heapx_contains(heap, &values[2]) != 0);
    assert(heapx_contains(heap, &missing) == 0);
    assert(heapx_remove(heap, handles[2]) == &values[2]);
    assert(heapx_contains(heap, &values[2]) == 0);
    assert(heapx_size(heap) == 4);

    assert(*(int *) heapx_extract_min(heap) == 1);
    assert(*(int *) heapx_extract_min(heap) == 3);
    assert(*(int *) heapx_extract_min(heap) == 4);
    assert(*(int *) heapx_extract_min(heap) == 7);
    assert(heapx_empty(heap));

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
    struct heapx_handle *second_handle;
    struct keyed_value *item;

    heap = heapx_create(implementation, keyed_value_cmp);
    assert(heap != NULL);

    assert(heapx_insert(heap, &first) == 0);
    second_handle = heapx_insert_handle(heap, &second);
    assert(second_handle != NULL);
    assert(heapx_insert(heap, &third) == 0);

    assert(heapx_remove(heap, second_handle) == &second);
    assert(heapx_contains(heap, &first) != 0);
    assert(heapx_contains(heap, &second) == 0);

    item = heapx_extract_min(heap);
    assert(item == &first);
    item = heapx_extract_min(heap);
    assert(item == &third);

    assert(heapx_empty(heap));
    heapx_destroy(heap);
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
    struct heapx_handle *slow_handle;
    struct keyed_value *item;

    heap = heapx_create(implementation, keyed_value_cmp);
    assert(heap != NULL);

    slow_handle = heapx_insert_handle(heap, &slow);
    assert(slow_handle != NULL);
    assert(heapx_insert(heap, &fast) == 0);
    assert(heapx_insert(heap, &middle) == 0);
    assert(heapx_peek_min(heap) == &fast);

    slow.priority = 5;
    assert(heapx_decrease_key(heap, slow_handle) == 0);
    assert(heapx_peek_min(heap) == &slow);
    assert(heapx_decrease_key(heap, NULL) == -1);
    assert(heapx_contains(heap, &missing) == 0);

    item = heapx_extract_min(heap);
    assert(item == &slow);
    item = heapx_extract_min(heap);
    assert(item == &fast);
    item = heapx_extract_min(heap);
    assert(item == &middle);

    assert(heapx_empty(heap));
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
    struct heapx_handle *handles[12];
    int previous_priority;
    size_t i;
    size_t extracted = 0;

    heap = heapx_create(implementation, keyed_value_cmp);
    assert(heap != NULL);

    for (i = 0; i < sizeof(values) / sizeof(values[0]); i++) {
        handles[i] = heapx_insert_handle(heap, &values[i]);
        assert(handles[i] != NULL);
    }

    item = heapx_extract_min(heap);
    assert(item->priority == 10);

    assert(heapx_remove(heap, handles[6]) == &values[6]);
    assert(heapx_remove(heap, handles[10]) == &values[10]);
    assert(heapx_contains(heap, &values[6]) == 0);
    assert(heapx_contains(heap, &values[10]) == 0);

    values[9].priority = 5;
    values[2].priority = 15;
    assert(heapx_decrease_key(heap, handles[9]) == 0);
    assert(heapx_decrease_key(heap, handles[2]) == 0);

    previous_priority = -1;
    while (!heapx_empty(heap)) {
        item = heapx_extract_min(heap);
        assert(item->priority >= previous_priority);
        previous_priority = item->priority;
        extracted++;
    }

    assert(extracted == 9);
    heapx_destroy(heap);
}

int main(void)
{
    enum heapx_implementation implementations[] = {
        HEAPX_BINARY_HEAP,
        HEAPX_FIBONACCI_HEAP,
        HEAPX_KAPLAN_HEAP
    };
    size_t i;

    test_create_rejects_missing_cmp();

    for (i = 0; i < sizeof(implementations) / sizeof(implementations[0]); i++) {
        test_empty_heap(implementations[i]);
        test_insert_extract_order(implementations[i]);
        test_duplicates(implementations[i]);
        test_interleaved_operations(implementations[i]);
        test_larger_deterministic_order(implementations[i]);
        test_contains_and_remove(implementations[i]);
        test_remove_uses_handle_identity(implementations[i]);
        test_decrease_key(implementations[i]);
        test_targeted_operations_after_restructuring(implementations[i]);
    }

    printf("All heapx heap tests passed\n");
    return 0;
}
