#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#include "heapx/heap.h"
#include "heap_internal.h"
#include "heaps/binary_heap.h"
#include "heaps/fibonacci_heap.h"
#include "heaps/kaplan_heap.h"

/** @brief Next process-local heap identifier assigned to a new heap. */
static uint64_t heapx_next_heap_id = 1;

/**
 * @brief Allocation block owned by a fixed-size node pool.
 *
 * The union members force enough alignment for pooled object payloads placed
 * immediately after the block header.
 */
union heapx_pool_block {
    /** Block-list linkage fields. */
    struct {
        /** Next allocated block in the pool-owned block list. */
        union heapx_pool_block *next;
    } fields;
    /** Pointer-alignment member for pooled payloads. */
    void *pointer_alignment;
    /** Long-double-alignment member for pooled payloads. */
    long double long_double_alignment;
};

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
    heap->id = heapx_next_heap_id++;
    if (heap->id == 0)
        heap->id = heapx_next_heap_id++;
    heap->handle_slots = NULL;
    heap->handle_slot_count = 0;
    heap->handle_slot_capacity = 0;
    heap->free_handle_slot = (size_t)-1;
}

/** @brief Ensure the common handle slot table can store capacity slots. */
static int heapx_handle_reserve(struct heapx_heap *heap, size_t capacity)
{
    struct heapx_handle_slot *slots;
    size_t bytes;
    size_t i;

    if (capacity <= heap->handle_slot_capacity)
        return 0;

    if (heapx_size_mul(capacity, sizeof(*slots), &bytes) != 0)
        return -1;

    slots = realloc(heap->handle_slots, bytes);
    if (slots == NULL)
        return -1;

    heap->handle_slots = slots;
    for (i = heap->handle_slot_capacity; i < capacity; i++) {
        heap->handle_slots[i].item = NULL;
        heap->handle_slots[i].owner = NULL;
        heap->handle_slots[i].generation = 1;
        heap->handle_slots[i].live = 0;
        heap->handle_slots[i].next_free = (size_t)-1;
    }

    heap->handle_slot_capacity = capacity;
    return 0;
}

int heapx_size_mul(size_t left, size_t right, size_t *out)
{
    if (left != 0 && right > (size_t)-1 / left)
        return -1;

    *out = left * right;
    return 0;
}

int heapx_size_add(size_t left, size_t right, size_t *out)
{
    if (left > (size_t)-1 - right)
        return -1;

    *out = left + right;
    return 0;
}

int heapx_handle_attach(
    struct heapx_heap *heap,
    void *item,
    void *owner,
    struct heapx_handle *out
)
{
    struct heapx_handle_slot *slot;
    size_t index;
    size_t new_capacity;

    if (heap->free_handle_slot != (size_t)-1) {
        index = heap->free_handle_slot;
        slot = &heap->handle_slots[index];
        heap->free_handle_slot = slot->next_free;
    } else {
        if (heap->handle_slot_count == heap->handle_slot_capacity) {
            new_capacity = heap->handle_slot_capacity == 0 ?
                8 : heap->handle_slot_capacity * 2;
            if (new_capacity < heap->handle_slot_capacity)
                return -1;
            if (heapx_handle_reserve(heap, new_capacity) != 0)
                return -1;
        }

        index = heap->handle_slot_count;
        heap->handle_slot_count++;
    }

    slot = &heap->handle_slots[index];
    slot->item = item;
    slot->owner = owner;
    slot->live = 1;
    slot->next_free = (size_t)-1;

    out->heap_id = heap->id;
    out->slot = index;
    out->generation = slot->generation;
    return 0;
}

void heapx_heap_destroy_handle_slots(struct heapx_heap *heap)
{
    free(heap->handle_slots);
    heap->handle_slots = NULL;
    heap->handle_slot_count = 0;
    heap->handle_slot_capacity = 0;
    heap->free_handle_slot = (size_t)-1;
}

int heapx_handle_resolve(
    const struct heapx_heap *heap,
    struct heapx_handle handle,
    void **owner
)
{
    const struct heapx_handle_slot *slot;

    if (handle.heap_id != heap->id)
        return -1;
    if (handle.slot >= heap->handle_slot_count)
        return -1;

    slot = &heap->handle_slots[handle.slot];
    if (!slot->live || slot->generation != handle.generation)
        return -1;

    if (owner != NULL)
        *owner = slot->owner;
    return 0;
}

void heapx_handle_release(struct heapx_heap *heap, struct heapx_handle handle)
{
    struct heapx_handle_slot *slot;

    if (handle.slot >= heap->handle_slot_count)
        return;

    slot = &heap->handle_slots[handle.slot];
    if (!slot->live || slot->generation != handle.generation)
        return;

    slot->item = NULL;
    slot->owner = NULL;
    slot->live = 0;
    slot->generation++;
    if (slot->generation == 0)
        slot->generation = 1;
    slot->next_free = heap->free_handle_slot;
    heap->free_handle_slot = handle.slot;
}

/** @brief Round value up to alignment, returning 0 on overflow. */
static size_t heapx_round_up_size(size_t value, size_t alignment)
{
    size_t remainder = value % alignment;

    if (remainder == 0)
        return value;
    if (value > (size_t)-1 - (alignment - remainder))
        return 0;
    return value + alignment - remainder;
}

int heapx_node_pool_init(
    struct heapx_node_pool *pool,
    size_t object_size,
    size_t block_capacity
)
{
    size_t alignment = sizeof(long double);

    if (sizeof(void *) > alignment)
        alignment = sizeof(void *);

    object_size = heapx_round_up_size(object_size, alignment);
    if (object_size == 0 || block_capacity == 0)
        return -1;

    pool->object_size = object_size;
    pool->block_capacity = block_capacity;
    pool->free_list = NULL;
    pool->blocks = NULL;
    return 0;
}

void heapx_node_pool_destroy(struct heapx_node_pool *pool)
{
    union heapx_pool_block *block = pool->blocks;

    while (block != NULL) {
        union heapx_pool_block *next = block->fields.next;

        free(block);
        block = next;
    }

    pool->object_size = 0;
    pool->block_capacity = 0;
    pool->free_list = NULL;
    pool->blocks = NULL;
}

/** @brief Allocate one new block and push its object slots onto the free list. */
static int heapx_node_pool_grow(struct heapx_node_pool *pool)
{
    union heapx_pool_block *block;
    unsigned char *data;
    size_t payload;
    size_t bytes;
    size_t i;

    if (heapx_size_mul(pool->object_size, pool->block_capacity, &payload) != 0)
        return -1;
    if (heapx_size_add(sizeof(*block), payload, &bytes) != 0)
        return -1;

    block = malloc(bytes);
    if (block == NULL)
        return -1;

    block->fields.next = pool->blocks;
    pool->blocks = block;

    data = (unsigned char *)(block + 1);
    for (i = 0; i < pool->block_capacity; i++) {
        void *object = data + i * pool->object_size;

        *(void **)object = pool->free_list;
        pool->free_list = object;
    }

    return 0;
}

void *heapx_node_pool_alloc(struct heapx_node_pool *pool)
{
    void *object;

    if (pool->free_list == NULL && heapx_node_pool_grow(pool) != 0)
        return NULL;

    object = pool->free_list;
    pool->free_list = *(void **)object;
    return object;
}

void heapx_node_pool_free(struct heapx_node_pool *pool, void *object)
{
    if (object == NULL)
        return;

    *(void **)object = pool->free_list;
    pool->free_list = object;
}

int heapx_check_invariants(const struct heapx_heap *heap)
{
    const struct heapx_handle_slot *slot;
    size_t free_count = 0;
    size_t index;

    if (heap == NULL)
        return -1;
    if (heap->vtable == NULL || heap->cmp == NULL || heap->id == 0)
        return -1;
    if (heap->handle_slot_count > heap->handle_slot_capacity)
        return -1;
    if (heap->handle_slot_capacity == 0 && heap->handle_slots != NULL)
        return -1;
    if (heap->handle_slot_capacity > 0 && heap->handle_slots == NULL)
        return -1;

    index = heap->free_handle_slot;
    while (index != (size_t)-1) {
        if (index >= heap->handle_slot_count)
            return -1;

        slot = &heap->handle_slots[index];
        if (slot->live || slot->owner != NULL || slot->item != NULL)
            return -1;

        free_count++;
        if (free_count > heap->handle_slot_count)
            return -1;

        index = slot->next_free;
    }

    if (heap->vtable->check_invariants == NULL)
        return -1;

    return heap->vtable->check_invariants(heap);
}

struct heapx_heap *heapx_create(
    enum heapx_implementation implementation,
    heapx_cmp_fn cmp
)
{
    struct heapx_heap *heap;

    if (cmp == NULL)
        return NULL;

    switch (implementation) {
    case HEAPX_BINARY_HEAP:
        heap = binary_heap_create(cmp);
        break;
    case HEAPX_FIBONACCI_HEAP:
        heap = fibonacci_heap_create(cmp);
        break;
    case HEAPX_KAPLAN_HEAP:
        heap = kaplan_heap_create(cmp);
        break;
    default:
        return NULL;
    }

    if (heap != NULL)
        HEAPX_ASSERT_INVARIANTS(heap);
    return heap;
}

void heapx_destroy(struct heapx_heap *heap)
{
    if (heap == NULL)
        return;

    heap->vtable->destroy(heap);
    heapx_heap_destroy_handle_slots(heap);
    free(heap);
}

int heapx_insert(struct heapx_heap *heap, void *item)
{
    int result;

    if (heap == NULL)
        return -1;

    result = heap->vtable->insert(heap, item);
    if (result == 0)
        HEAPX_ASSERT_INVARIANTS(heap);
    return result;
}

int heapx_insert_handle(
    struct heapx_heap *heap,
    void *item,
    struct heapx_handle *out
)
{
    int result;

    if (heap == NULL)
        return -1;
    if (out == NULL)
        return -1;

    result = heap->vtable->insert_handle(heap, item, out);
    if (result == 0)
        HEAPX_ASSERT_INVARIANTS(heap);
    return result;
}

int heapx_decrease_key(
    struct heapx_heap *heap,
    struct heapx_handle handle
)
{
    void *owner;
    int result;

    if (heap == NULL)
        return -1;
    if (heapx_handle_resolve(heap, handle, &owner) != 0)
        return -1;

    result = heap->vtable->decrease_key(heap, owner);
    if (result == 0)
        HEAPX_ASSERT_INVARIANTS(heap);
    return result;
}

void *heapx_remove(
    struct heapx_heap *heap,
    struct heapx_handle handle
)
{
    void *owner;
    void *item;

    if (heap == NULL)
        return NULL;
    if (heapx_handle_resolve(heap, handle, &owner) != 0)
        return NULL;

    item = heap->vtable->remove(heap, owner);
    HEAPX_ASSERT_INVARIANTS(heap);
    return item;
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
    void *item;

    if (heap == NULL)
        return NULL;

    item = heap->vtable->extract_min(heap);
    HEAPX_ASSERT_INVARIANTS(heap);
    return item;
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
