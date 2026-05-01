#include <stddef.h>

#include "hpqlib/priority_queue.h"
#include "priority_queue_internal.h"
#include "heaps/binary_heap.h"
#include "heaps/fibonacci_heap.h"
#include "heaps/kaplan_heap.h"

/**
 * @file priority_queue.c
 * @brief Public priority_queue dispatch layer and backend factory.
 *
 * This file contains the only public-operation implementations. They validate
 * NULL-handle edge cases once, then dispatch to the selected backend through
 * the private vtable.
 *
 * Keeping dispatch here gives every backend the same external behavior for
 * unsupported implementations, missing comparators, and NULL queue handles.
 */

/**
 * @ingroup internals
 * @brief Initialize the base object shared by all concrete priority queues.
 *
 * Constructors call this after allocating their concrete object. No heap
 * invariant is established here beyond storing the vtable and comparator.
 */
void priority_queue_init(
    struct priority_queue *queue,
    const struct priority_queue_vtable *vtable,
    priority_queue_cmp_fn cmp
)
{
    queue->vtable = vtable;
    queue->cmp = cmp;
}

void priority_queue_handle_init(
    struct priority_queue_handle *handle,
    struct priority_queue *queue,
    void *item
)
{
    handle->queue = queue;
    handle->item = item;
}

struct priority_queue *priority_queue_create(
    enum priority_queue_implementation implementation,
    priority_queue_cmp_fn cmp
)
{
    if (cmp == NULL)
        return NULL;

    switch (implementation) {
    case PRIORITY_QUEUE_BINARY_HEAP:
        return binary_heap_create(cmp);
    case PRIORITY_QUEUE_FIBONACCI_HEAP:
        return fibonacci_heap_create(cmp);
    case PRIORITY_QUEUE_KAPLAN_HEAP:
        return kaplan_heap_create(cmp);
    default:
        return NULL;
    }
}

void priority_queue_destroy(struct priority_queue *queue)
{
    if (queue == NULL)
        return;

    queue->vtable->destroy(queue);
}

int priority_queue_push(struct priority_queue *queue, void *item)
{
    if (queue == NULL)
        return -1;

    return queue->vtable->push(queue, item);
}

struct priority_queue_handle *priority_queue_push_handle(
    struct priority_queue *queue,
    void *item
)
{
    if (queue == NULL)
        return NULL;

    return queue->vtable->push_handle(queue, item);
}

int priority_queue_decrease_key(
    struct priority_queue *queue,
    struct priority_queue_handle *handle
)
{
    if (queue == NULL)
        return -1;
    if (handle == NULL || handle->queue != queue)
        return -1;

    return queue->vtable->decrease_key(queue, handle);
}

void *priority_queue_remove(
    struct priority_queue *queue,
    struct priority_queue_handle *handle
)
{
    if (queue == NULL)
        return NULL;
    if (handle == NULL || handle->queue != queue)
        return NULL;

    return queue->vtable->remove(queue, handle);
}

int priority_queue_contains(const struct priority_queue *queue, const void *item)
{
    if (queue == NULL)
        return 0;

    return queue->vtable->contains(queue, item);
}

void *priority_queue_peek(const struct priority_queue *queue)
{
    if (queue == NULL)
        return NULL;

    return queue->vtable->peek(queue);
}

void *priority_queue_pop(struct priority_queue *queue)
{
    if (queue == NULL)
        return NULL;

    return queue->vtable->pop(queue);
}

size_t priority_queue_size(const struct priority_queue *queue)
{
    if (queue == NULL)
        return 0;

    return queue->vtable->size(queue);
}

int priority_queue_empty(const struct priority_queue *queue)
{
    if (queue == NULL)
        return 1;

    return queue->vtable->empty(queue);
}
