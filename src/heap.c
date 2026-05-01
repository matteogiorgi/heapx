#include <stddef.h>

#include "heapx/heap.h"
#include "heap_internal.h"
#include "heaps/binary_heap.h"
#include "heaps/fibonacci_heap.h"
#include "heaps/kaplan_heap.h"

/**
 * @file heap.c
 * @brief Public heapx_heap dispatch layer and backend factory.
 *
 * This file contains the only public-operation implementations. They validate
 * NULL-handle edge cases once, then dispatch to the selected backend through
 * the private vtable.
 *
 * Keeping dispatch here gives every backend the same external behavior for
 * unsupported implementations, missing comparators, and NULL heap handles.
 */

/**
 * @ingroup internals
 * @brief Initialize the base object shared by all concrete heaps.
 *
 * Constructors call this after allocating their concrete object. No heap
 * invariant is established here beyond storing the vtable and comparator.
 */
void heapx_heap_init(
    struct heapx_heap *heap,
    const struct heapx_vtable *vtable,
    heapx_cmp_fn cmp
)
{
    heap->vtable = vtable;
    heap->cmp = cmp;
}

void heapx_handle_init(
    struct heapx_handle *handle,
    struct heapx_heap *heap,
    void *item
)
{
    handle->heap = heap;
    handle->item = item;
}

struct heapx_heap *heapx_create(
    enum heapx_implementation implementation,
    heapx_cmp_fn cmp
)
{
    if (cmp == NULL)
        return NULL;

    switch (implementation) {
    case HEAPX_BINARY_HEAP:
        return binary_heap_create(cmp);
    case HEAPX_FIBONACCI_HEAP:
        return fibonacci_heap_create(cmp);
    case HEAPX_KAPLAN_HEAP:
        return kaplan_heap_create(cmp);
    default:
        return NULL;
    }
}

void heapx_destroy(struct heapx_heap *heap)
{
    if (heap == NULL)
        return;

    heap->vtable->destroy(heap);
}

int heapx_insert(struct heapx_heap *heap, void *item)
{
    if (heap == NULL)
        return -1;

    return heap->vtable->insert(heap, item);
}

struct heapx_handle *heapx_insert_handle(
    struct heapx_heap *heap,
    void *item
)
{
    if (heap == NULL)
        return NULL;

    return heap->vtable->insert_handle(heap, item);
}

int heapx_decrease_key(
    struct heapx_heap *heap,
    struct heapx_handle *handle
)
{
    if (heap == NULL)
        return -1;
    if (handle == NULL || handle->heap != heap)
        return -1;

    return heap->vtable->decrease_key(heap, handle);
}

void *heapx_remove(
    struct heapx_heap *heap,
    struct heapx_handle *handle
)
{
    if (heap == NULL)
        return NULL;
    if (handle == NULL || handle->heap != heap)
        return NULL;

    return heap->vtable->remove(heap, handle);
}

int heapx_contains(const struct heapx_heap *heap, const void *item)
{
    if (heap == NULL)
        return 0;

    return heap->vtable->contains(heap, item);
}

void *heapx_peek_min(const struct heapx_heap *heap)
{
    if (heap == NULL)
        return NULL;

    return heap->vtable->peek_min(heap);
}

void *heapx_extract_min(struct heapx_heap *heap)
{
    if (heap == NULL)
        return NULL;

    return heap->vtable->extract_min(heap);
}

size_t heapx_size(const struct heapx_heap *heap)
{
    if (heap == NULL)
        return 0;

    return heap->vtable->size(heap);
}

int heapx_empty(const struct heapx_heap *heap)
{
    if (heap == NULL)
        return 1;

    return heap->vtable->empty(heap);
}
