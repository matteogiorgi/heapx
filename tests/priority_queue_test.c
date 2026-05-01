#include <assert.h>
#include <stddef.h>
#include <stdio.h>

#include "hpqlib/priority_queue.h"

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
    assert(priority_queue_create(PRIORITY_QUEUE_BINARY_HEAP, NULL) == NULL);
    assert(priority_queue_create(PRIORITY_QUEUE_FIBONACCI_HEAP, NULL) == NULL);
    assert(priority_queue_create(PRIORITY_QUEUE_KAPLAN_HEAP, NULL) == NULL);

    assert(priority_queue_contains(NULL, NULL) == 0);
    assert(priority_queue_remove(NULL, NULL) == NULL);
    assert(priority_queue_decrease_key(NULL, NULL) == -1);
}

static void test_empty_queue(enum priority_queue_implementation implementation)
{
    struct priority_queue *queue;

    queue = priority_queue_create(implementation, int_cmp);
    assert(queue != NULL);

    assert(priority_queue_empty(queue));
    assert(priority_queue_size(queue) == 0);
    assert(priority_queue_peek(queue) == NULL);
    assert(priority_queue_pop(queue) == NULL);

    priority_queue_destroy(queue);
}

static void test_push_pop_order(enum priority_queue_implementation implementation)
{
    struct priority_queue *queue;
    int values[] = { 7, 3, 9, 1, 4, 8, 2 };
    int expected[] = { 1, 2, 3, 4, 7, 8, 9 };
    size_t i;

    queue = priority_queue_create(implementation, int_cmp);
    assert(queue != NULL);

    for (i = 0; i < sizeof(values) / sizeof(values[0]); i++)
        assert(priority_queue_push(queue, &values[i]) == 0);

    assert(priority_queue_size(queue) == sizeof(values) / sizeof(values[0]));
    assert(*(int *) priority_queue_peek(queue) == 1);

    for (i = 0; i < sizeof(expected) / sizeof(expected[0]); i++)
        assert(*(int *) priority_queue_pop(queue) == expected[i]);

    assert(priority_queue_empty(queue));
    priority_queue_destroy(queue);
}

static void test_duplicates(enum priority_queue_implementation implementation)
{
    struct priority_queue *queue;
    int values[] = { 5, 1, 5, 1, 3 };
    int expected[] = { 1, 1, 3, 5, 5 };
    size_t i;

    queue = priority_queue_create(implementation, int_cmp);
    assert(queue != NULL);

    for (i = 0; i < sizeof(values) / sizeof(values[0]); i++)
        assert(priority_queue_push(queue, &values[i]) == 0);

    for (i = 0; i < sizeof(expected) / sizeof(expected[0]); i++)
        assert(*(int *) priority_queue_pop(queue) == expected[i]);

    priority_queue_destroy(queue);
}

static void test_interleaved_operations(
    enum priority_queue_implementation implementation
)
{
    struct priority_queue *queue;
    int values[] = { 10, 4, 7, 1, 6, 3 };

    queue = priority_queue_create(implementation, int_cmp);
    assert(queue != NULL);

    assert(priority_queue_push(queue, &values[0]) == 0);
    assert(priority_queue_push(queue, &values[1]) == 0);
    assert(priority_queue_push(queue, &values[2]) == 0);
    assert(*(int *) priority_queue_pop(queue) == 4);

    assert(priority_queue_push(queue, &values[3]) == 0);
    assert(priority_queue_push(queue, &values[4]) == 0);
    assert(*(int *) priority_queue_pop(queue) == 1);

    assert(priority_queue_push(queue, &values[5]) == 0);
    assert(*(int *) priority_queue_pop(queue) == 3);
    assert(*(int *) priority_queue_pop(queue) == 6);
    assert(*(int *) priority_queue_pop(queue) == 7);
    assert(*(int *) priority_queue_pop(queue) == 10);
    assert(priority_queue_pop(queue) == NULL);

    priority_queue_destroy(queue);
}

static void test_larger_deterministic_order(
    enum priority_queue_implementation implementation
)
{
    struct priority_queue *queue;
    int values[256];
    int expected[256];
    size_t i;
    size_t value;

    queue = priority_queue_create(implementation, int_cmp);
    assert(queue != NULL);

    for (i = 0; i < 256; i++) {
        values[i] = (int)((i * 37) % 101);
        assert(priority_queue_push(queue, &values[i]) == 0);
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
        assert(*(int *) priority_queue_pop(queue) == expected[i]);

    assert(priority_queue_empty(queue));
    priority_queue_destroy(queue);
}

static void test_contains_and_remove(
    enum priority_queue_implementation implementation
)
{
    struct priority_queue *queue;
    struct priority_queue_handle *handles[5];
    int values[] = { 7, 3, 9, 1, 4 };
    int missing = 42;
    size_t i;

    queue = priority_queue_create(implementation, int_cmp);
    assert(queue != NULL);

    assert(priority_queue_contains(queue, &values[2]) == 0);
    assert(priority_queue_remove(queue, NULL) == NULL);

    for (i = 0; i < sizeof(values) / sizeof(values[0]); i++) {
        handles[i] = priority_queue_push_handle(queue, &values[i]);
        assert(handles[i] != NULL);
    }

    assert(priority_queue_contains(queue, &values[2]) != 0);
    assert(priority_queue_contains(queue, &missing) == 0);
    assert(priority_queue_remove(queue, handles[2]) == &values[2]);
    assert(priority_queue_contains(queue, &values[2]) == 0);
    assert(priority_queue_size(queue) == 4);

    assert(*(int *) priority_queue_pop(queue) == 1);
    assert(*(int *) priority_queue_pop(queue) == 3);
    assert(*(int *) priority_queue_pop(queue) == 4);
    assert(*(int *) priority_queue_pop(queue) == 7);
    assert(priority_queue_empty(queue));

    priority_queue_destroy(queue);
}

static void test_remove_uses_handle_identity(
    enum priority_queue_implementation implementation
)
{
    struct priority_queue *queue;
    struct keyed_value first = { 1, 5 };
    struct keyed_value second = { 2, 5 };
    struct keyed_value third = { 3, 9 };
    struct priority_queue_handle *second_handle;
    struct keyed_value *item;

    queue = priority_queue_create(implementation, keyed_value_cmp);
    assert(queue != NULL);

    assert(priority_queue_push(queue, &first) == 0);
    second_handle = priority_queue_push_handle(queue, &second);
    assert(second_handle != NULL);
    assert(priority_queue_push(queue, &third) == 0);

    assert(priority_queue_remove(queue, second_handle) == &second);
    assert(priority_queue_contains(queue, &first) != 0);
    assert(priority_queue_contains(queue, &second) == 0);

    item = priority_queue_pop(queue);
    assert(item == &first);
    item = priority_queue_pop(queue);
    assert(item == &third);

    assert(priority_queue_empty(queue));
    priority_queue_destroy(queue);
}

static void test_decrease_key(
    enum priority_queue_implementation implementation
)
{
    struct priority_queue *queue;
    struct keyed_value slow = { 1, 30 };
    struct keyed_value fast = { 2, 10 };
    struct keyed_value middle = { 3, 20 };
    struct keyed_value missing = { 4, 1 };
    struct priority_queue_handle *slow_handle;
    struct keyed_value *item;

    queue = priority_queue_create(implementation, keyed_value_cmp);
    assert(queue != NULL);

    slow_handle = priority_queue_push_handle(queue, &slow);
    assert(slow_handle != NULL);
    assert(priority_queue_push(queue, &fast) == 0);
    assert(priority_queue_push(queue, &middle) == 0);
    assert(priority_queue_peek(queue) == &fast);

    slow.priority = 5;
    assert(priority_queue_decrease_key(queue, slow_handle) == 0);
    assert(priority_queue_peek(queue) == &slow);
    assert(priority_queue_decrease_key(queue, NULL) == -1);
    assert(priority_queue_contains(queue, &missing) == 0);

    item = priority_queue_pop(queue);
    assert(item == &slow);
    item = priority_queue_pop(queue);
    assert(item == &fast);
    item = priority_queue_pop(queue);
    assert(item == &middle);

    assert(priority_queue_empty(queue));
    priority_queue_destroy(queue);
}

static void test_targeted_operations_after_restructuring(
    enum priority_queue_implementation implementation
)
{
    struct priority_queue *queue;
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
    struct priority_queue_handle *handles[12];
    int previous_priority;
    size_t i;
    size_t popped = 0;

    queue = priority_queue_create(implementation, keyed_value_cmp);
    assert(queue != NULL);

    for (i = 0; i < sizeof(values) / sizeof(values[0]); i++) {
        handles[i] = priority_queue_push_handle(queue, &values[i]);
        assert(handles[i] != NULL);
    }

    item = priority_queue_pop(queue);
    assert(item->priority == 10);

    assert(priority_queue_remove(queue, handles[6]) == &values[6]);
    assert(priority_queue_remove(queue, handles[10]) == &values[10]);
    assert(priority_queue_contains(queue, &values[6]) == 0);
    assert(priority_queue_contains(queue, &values[10]) == 0);

    values[9].priority = 5;
    values[2].priority = 15;
    assert(priority_queue_decrease_key(queue, handles[9]) == 0);
    assert(priority_queue_decrease_key(queue, handles[2]) == 0);

    previous_priority = -1;
    while (!priority_queue_empty(queue)) {
        item = priority_queue_pop(queue);
        assert(item->priority >= previous_priority);
        previous_priority = item->priority;
        popped++;
    }

    assert(popped == 9);
    priority_queue_destroy(queue);
}

int main(void)
{
    enum priority_queue_implementation implementations[] = {
        PRIORITY_QUEUE_BINARY_HEAP,
        PRIORITY_QUEUE_FIBONACCI_HEAP,
        PRIORITY_QUEUE_KAPLAN_HEAP
    };
    size_t i;

    test_create_rejects_missing_cmp();

    for (i = 0; i < sizeof(implementations) / sizeof(implementations[0]); i++) {
        test_empty_queue(implementations[i]);
        test_push_pop_order(implementations[i]);
        test_duplicates(implementations[i]);
        test_interleaved_operations(implementations[i]);
        test_larger_deterministic_order(implementations[i]);
        test_contains_and_remove(implementations[i]);
        test_remove_uses_handle_identity(implementations[i]);
        test_decrease_key(implementations[i]);
        test_targeted_operations_after_restructuring(implementations[i]);
    }

    printf("All priority_queue tests passed\n");
    return 0;
}
