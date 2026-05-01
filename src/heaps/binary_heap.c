#include <stdlib.h>

#include "heaps/binary_heap.h"
#include "heap_internal.h"

/**
 * @file binary_heap.c
 * @brief Binary min-heap backend for the abstract heapx_heap API.
 *
 * The backend stores backend-owned handles in a contiguous array. Parent
 * and child positions follow the usual zero-based binary heap layout:
 * parent `(i - 1) / 2`, left child `2 * i + 1`, right child `2 * i + 2`.
 *
 * The array contains no sentinel. The root item is always at index 0 when the
 * heap is non-empty. Equal-priority items are allowed and are not made stable
 * by this backend.
 */

/**
 * @ingroup heap_backends
 * @brief Concrete binary min-heap object.
 *
 * Ordering is defined entirely by the comparator stored in the embedded base
 * object. The base field must remain first so the object can be exposed as
 * struct heapx_heap.
 *
 * Invariant: for every occupied index except the root, the parent item has
 * priority less than or equal to the child item according to base.cmp().
 */
struct binary_heap {
    /** Common heapx_heap base. Must be the first field. */
    struct heapx_heap base;
    /** Contiguous array of stored handles. */
    struct binary_heap_handle **data;
    /** Number of items currently stored. */
    size_t size;
    /** Allocated number of slots in data. */
    size_t capacity;
};

/** @brief Backend-specific handle for an item stored in a binary heap. */
struct binary_heap_handle {
    /** Common public handle header. Must be the first field. */
    struct heapx_handle base;
    /** Current array position, updated whenever handles are swapped. */
    size_t index;
};

static void binary_heap_destroy(struct heapx_heap *base);
static int binary_heap_insert(struct heapx_heap *base, void *item);
static struct heapx_handle *binary_heap_insert_handle(
    struct heapx_heap *base,
    void *item
);
static int binary_heap_decrease_key(
    struct heapx_heap *base,
    struct heapx_handle *handle
);
static void *binary_heap_remove(
    struct heapx_heap *base,
    struct heapx_handle *handle
);
static int binary_heap_contains(
    const struct heapx_heap *base,
    const void *item
);
static void *binary_heap_peek_min(const struct heapx_heap *base);
static void *binary_heap_extract_min(struct heapx_heap *base);
static size_t binary_heap_size(const struct heapx_heap *base);
static int binary_heap_empty(const struct heapx_heap *base);

/** @brief Static vtable exposed through the common heapx_heap base. */
static const struct heapx_vtable binary_heap_vtable = {
    binary_heap_destroy,
    binary_heap_insert,
    binary_heap_insert_handle,
    binary_heap_decrease_key,
    binary_heap_remove,
    binary_heap_contains,
    binary_heap_peek_min,
    binary_heap_extract_min,
    binary_heap_size,
    binary_heap_empty
};

/**
 * @brief Recover the concrete heap object from the abstract base pointer.
 *
 * This cast is valid because struct heapx_heap is the first field of
 * struct binary_heap.
 */
static struct binary_heap *binary_heap_from_base(struct heapx_heap *base)
{
    return (struct binary_heap *)base;
}

/** @brief Const-preserving variant of binary_heap_from_base(). */
static const struct binary_heap *binary_heap_from_const_base(
    const struct heapx_heap *base
)
{
    return (const struct binary_heap *)base;
}

/** @brief Recover the binary-heap handle from the common handle pointer. */
static struct binary_heap_handle *binary_heap_handle_from_handle(
    struct heapx_handle *handle
)
{
    return (struct binary_heap_handle *)handle;
}

/** @brief Swap two handle slots in the heap storage. */
static void binary_heap_swap(struct binary_heap *heap, size_t lhs, size_t rhs)
{
    struct binary_heap_handle *tmp = heap->data[lhs];

    heap->data[lhs] = heap->data[rhs];
    heap->data[rhs] = tmp;
    heap->data[lhs]->index = lhs;
    heap->data[rhs]->index = rhs;
}

/**
 * @brief Move an item toward the root until heap order is restored.
 *
 * Used after insertion, when only the new leaf can violate the heap property.
 */
static void binary_heap_sift_up(struct binary_heap *heap, size_t index)
{
    while (index > 0) {
        size_t parent = (index - 1) / 2;

        if (
            heap->base.cmp(
                heap->data[index]->base.item,
                heap->data[parent]->base.item
            ) >= 0
            )
            break;

        binary_heap_swap(heap, index, parent);
        index = parent;
    }
}

/**
 * @brief Move an item away from the root until heap order is restored.
 *
 * Used after extract-min, when the last leaf has been moved into the root slot.
 */
static void binary_heap_sift_down(struct binary_heap *heap, size_t index)
{
    size_t left, right, smallest;

    for (;;) {
        left = 2 * index + 1;
        right = left + 1;
        smallest = index;

        if (
            left < heap->size &&
            heap->base.cmp(
                heap->data[left]->base.item,
                heap->data[smallest]->base.item
            ) < 0
            )
            smallest = left;

        if (
            right < heap->size &&
            heap->base.cmp(
                heap->data[right]->base.item,
                heap->data[smallest]->base.item
            ) < 0
            )
            smallest = right;

        if (smallest == index)
            break;

        binary_heap_swap(heap, index, smallest);
        index = smallest;
    }
}

/**
 * @brief Ensure that the heap can store at least capacity items.
 *
 * Existing items remain valid and keep their relative array contents. Returns
 * 0 on success and -1 if allocation fails.
 *
 * The capacity grows geometrically through binary_heap_insert(), keeping
 * repeated insertions amortized O(1) for resizing work.
 */
static int binary_heap_reserve(struct binary_heap *heap, size_t capacity)
{
    struct binary_heap_handle **new_data;

    if (capacity <= heap->capacity)
        return 0;

    new_data = realloc(heap->data, capacity * sizeof(*heap->data));
    if (new_data == NULL)
        return -1;

    heap->data = new_data;
    heap->capacity = capacity;
    return 0;
}

/**
 * @brief Find the first occupied slot that stores item by pointer identity.
 */
static int binary_heap_find_index(
    const struct binary_heap *heap,
    const void *item,
    size_t *index
)
{
    size_t i;

    for (i = 0; i < heap->size; i++) {
        if (heap->data[i]->base.item == item) {
            *index = i;
            return 0;
        }
    }

    return -1;
}

/**
 * @brief Repair heap order after replacing or mutating one slot.
 */
static void binary_heap_repair_at(struct binary_heap *heap, size_t index)
{
    if (
        index > 0 &&
        heap->base.cmp(
            heap->data[index]->base.item,
            heap->data[(index - 1) / 2]->base.item
        ) < 0
        ) {
        binary_heap_sift_up(heap, index);
        return;
    }

    binary_heap_sift_down(heap, index);
}

struct heapx_heap *binary_heap_create(heapx_cmp_fn cmp)
{
    struct binary_heap *heap;

    heap = malloc(sizeof(*heap));
    if (heap == NULL)
        return NULL;

    heapx_heap_init(&heap->base, &binary_heap_vtable, cmp);
    heap->data = NULL;
    heap->size = 0;
    heap->capacity = 0;

    return &heap->base;
}

/**
 * @brief Release heap-owned memory.
 *
 * Stored item pointers are caller-owned and are intentionally left untouched.
 */
static void binary_heap_destroy(struct heapx_heap *base)
{
    struct binary_heap *heap = binary_heap_from_base(base);
    size_t i;

    for (i = 0; i < heap->size; i++)
        free(heap->data[i]);
    free(heap->data);
    free(heap);
}

/**
 * @brief Insert an item and restore heap order by sifting it upward.
 *
 * The item is first appended at the end of the occupied array and then moved
 * toward the root until the binary heap invariant holds again.
 */
static int binary_heap_insert(struct heapx_heap *base, void *item)
{
    return binary_heap_insert_handle(base, item) == NULL ? -1 : 0;
}

/**
 * @brief Insert an item and return its handle.
 */
static struct heapx_handle *binary_heap_insert_handle(
    struct heapx_heap *base,
    void *item
)
{
    struct binary_heap *heap = binary_heap_from_base(base);
    struct binary_heap_handle *handle;
    size_t new_capacity;

    handle = malloc(sizeof(*handle));
    if (handle == NULL)
        return NULL;

    if (heap->size == heap->capacity) {
        new_capacity = heap->capacity == 0 ? 8 : heap->capacity * 2;
        if (new_capacity < heap->capacity) {
            free(handle);
            return NULL;
        }
        if (binary_heap_reserve(heap, new_capacity) != 0) {
            free(handle);
            return NULL;
        }
    }

    heapx_handle_init(&handle->base, base, item);
    handle->index = heap->size;
    heap->data[heap->size] = handle;
    binary_heap_sift_up(heap, heap->size);
    heap->size++;

    return &handle->base;
}

/**
 * @brief Repair an item whose key decreased and priority improved.
 */
static int binary_heap_decrease_key(
    struct heapx_heap *base,
    struct heapx_handle *handle
)
{
    struct binary_heap *heap = binary_heap_from_base(base);
    struct binary_heap_handle *binary_handle =
        binary_heap_handle_from_handle(handle);

    binary_heap_sift_up(heap, binary_handle->index);
    return 0;
}

/**
 * @brief Remove one item by handle.
 */
static void *binary_heap_remove(
    struct heapx_heap *base,
    struct heapx_handle *handle
)
{
    struct binary_heap *heap = binary_heap_from_base(base);
    struct binary_heap_handle *binary_handle =
        binary_heap_handle_from_handle(handle);
    size_t index = binary_handle->index;
    void *item = handle->item;

    handle->heap = NULL;

    heap->size--;
    if (index != heap->size) {
        heap->data[index] = heap->data[heap->size];
        heap->data[index]->index = index;
        binary_heap_repair_at(heap, index);
    }

    free(binary_handle);
    return item;
}

/**
 * @brief Return whether item is stored by pointer identity.
 */
static int binary_heap_contains(
    const struct heapx_heap *base,
    const void *item
)
{
    const struct binary_heap *heap = binary_heap_from_const_base(base);
    size_t index;

    return binary_heap_find_index(heap, item, &index) == 0;
}

/** @brief Return the root item without modifying the heap. */
static void *binary_heap_peek_min(const struct heapx_heap *base)
{
    const struct binary_heap *heap = binary_heap_from_const_base(base);

    if (heap->size == 0)
        return NULL;

    return heap->data[0]->base.item;
}

/**
 * @brief Remove and return the root item, then restore heap order.
 *
 * The last occupied slot replaces the root, then binary_heap_sift_down()
 * repairs the only possible violation.
 */
static void *binary_heap_extract_min(struct heapx_heap *base)
{
    struct binary_heap *heap = binary_heap_from_base(base);
    struct binary_heap_handle *handle;
    void *item;

    if (heap->size == 0)
        return NULL;

    handle = heap->data[0];
    item = handle->base.item;
    handle->base.heap = NULL;
    heap->size--;

    if (heap->size > 0) {
        heap->data[0] = heap->data[heap->size];
        heap->data[0]->index = 0;
        binary_heap_sift_down(heap, 0);
    }

    free(handle);
    return item;
}

/** @brief Return the current number of stored items. */
static size_t binary_heap_size(const struct heapx_heap *base)
{
    return binary_heap_from_const_base(base)->size;
}

/** @brief Return whether the heap contains no items. */
static int binary_heap_empty(const struct heapx_heap *base)
{
    return binary_heap_size(base) == 0;
}
