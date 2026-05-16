#include <stdlib.h>

#include "heaps/binary_heap.h"
#include "heap_internal.h"

/**
 * @file binary_heap.c
 * @brief Binary min-heap backend for the abstract heapx_heap API.
 *
 * The backend stores entries with public handles in a contiguous array. Parent
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
    /** Contiguous array of stored entries. */
    struct binary_heap_handle *data;
    /** Number of items currently stored. */
    size_t size;
    /** Allocated number of slots in data. */
    size_t capacity;
    /** Pool for stable locators used by handled entries. */
    struct heapx_node_pool locators;
};

/** @brief Stable owner object stored in public handle slots. */
struct binary_heap_locator {
    /** Public generational handle associated with this locator. */
    struct heapx_handle handle;
    /** Current array position of the handled entry. */
    size_t index;
};

/** @brief Backend-specific entry for an item stored in a binary heap. */
struct binary_heap_handle {
    /** Caller-owned item pointer stored in this entry. */
    void *item;
    /** Stable locator for targeted operations, or NULL for plain inserts. */
    struct binary_heap_locator *locator;
};

static void binary_heap_destroy(struct heapx_heap *base);
static int binary_heap_insert(struct heapx_heap *base, void *item);
static int binary_heap_insert_handle(
    struct heapx_heap *base,
    void *item,
    struct heapx_handle *out
);
static int binary_heap_decrease_key(
    struct heapx_heap *base,
    void *owner
);
static void *binary_heap_remove(
    struct heapx_heap *base,
    void *owner
);
static int binary_heap_contains(
    const struct heapx_heap *base,
    const void *item
);
static void *binary_heap_peek_min(const struct heapx_heap *base);
static void *binary_heap_extract_min(struct heapx_heap *base);
static size_t binary_heap_size(const struct heapx_heap *base);
static int binary_heap_empty(const struct heapx_heap *base);
static int binary_heap_check_invariants(const struct heapx_heap *base);

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
    binary_heap_empty,
    binary_heap_check_invariants
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

/** @brief Store an entry at index and refresh its locator position. */
static void binary_heap_store_entry(
    struct binary_heap *heap,
    size_t index,
    struct binary_heap_handle entry
)
{
    heap->data[index] = entry;
    if (heap->data[index].locator != NULL)
        heap->data[index].locator->index = index;
}

/**
 * @brief Move an item toward the root until heap order is restored.
 *
 * Used after insertion, when only the new leaf can violate the heap property.
 */
static void binary_heap_sift_up(struct binary_heap *heap, size_t index)
{
    struct binary_heap_handle entry = heap->data[index];
    size_t original = index;

    while (index > 0) {
        size_t parent = (index - 1) / 2;

        if (heap->base.cmp(entry.item, heap->data[parent].item) >= 0)
            break;

        binary_heap_store_entry(heap, index, heap->data[parent]);
        index = parent;
    }

    if (index != original)
        binary_heap_store_entry(heap, index, entry);
}

/**
 * @brief Move an item away from the root until heap order is restored.
 *
 * Used after extract-min, when the last leaf has been moved into the root slot.
 */
static void binary_heap_sift_down(struct binary_heap *heap, size_t index)
{
    struct binary_heap_handle entry = heap->data[index];
    size_t original = index;
    size_t left, right, smallest;

    for (;;) {
        left = 2 * index + 1;
        right = left + 1;
        if (left >= heap->size)
            break;

        smallest = left;

        if (
            right < heap->size &&
            heap->base.cmp(
                heap->data[right].item,
                heap->data[left].item
            ) < 0
            )
            smallest = right;

        if (heap->base.cmp(heap->data[smallest].item, entry.item) >= 0)
            break;

        binary_heap_store_entry(heap, index, heap->data[smallest]);
        index = smallest;
    }

    if (index != original)
        binary_heap_store_entry(heap, index, entry);
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
    struct binary_heap_handle *new_data;
    size_t bytes;
    size_t i;

    if (capacity <= heap->capacity)
        return 0;

    if (heapx_size_mul(capacity, sizeof(*heap->data), &bytes) != 0)
        return -1;

    new_data = realloc(heap->data, bytes);
    if (new_data == NULL)
        return -1;

    heap->data = new_data;
    heap->capacity = capacity;
    for (i = 0; i < heap->size; i++) {
        if (heap->data[i].locator != NULL)
            heap->data[i].locator->index = i;
    }
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
        if (heap->data[i].item == item) {
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
            heap->data[index].item,
            heap->data[(index - 1) / 2].item
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
    if (
        heapx_node_pool_init(
            &heap->locators,
            sizeof(struct binary_heap_locator),
            256
        ) != 0
        ) {
        free(heap);
        return NULL;
    }

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

    free(heap->data);
    heapx_node_pool_destroy(&heap->locators);
}

/**
 * @brief Insert an item and restore heap order by sifting it upward.
 *
 * The item is first appended at the end of the occupied array and then moved
 * toward the root until the binary heap invariant holds again.
 */
static int binary_heap_insert_entry(
    struct heapx_heap *base,
    void *item,
    struct heapx_handle *out
)
{
    struct binary_heap *heap = binary_heap_from_base(base);
    struct binary_heap_handle *entry;
    struct binary_heap_locator *locator = NULL;
    size_t new_capacity;

    if (heap->size == heap->capacity) {
        new_capacity = heap->capacity == 0 ? 8 : heap->capacity * 2;
        if (new_capacity < heap->capacity)
            return -1;
        if (binary_heap_reserve(heap, new_capacity) != 0)
            return -1;
    }

    entry = &heap->data[heap->size];
    entry->item = item;
    entry->locator = NULL;

    if (out != NULL) {
        locator = heapx_node_pool_alloc(&heap->locators);
        if (locator == NULL)
            return -1;
        locator->index = heap->size;

        if (heapx_handle_attach(base, item, locator, out) != 0) {
            heapx_node_pool_free(&heap->locators, locator);
            return -1;
        }
        locator->handle = *out;
        entry->locator = locator;
    }

    binary_heap_sift_up(heap, heap->size);
    heap->size++;

    return 0;
}

/** @brief Insert an item without creating a public handle. */
static int binary_heap_insert(struct heapx_heap *base, void *item)
{
    return binary_heap_insert_entry(base, item, NULL);
}

/**
 * @brief Insert an item and return its handle.
 */
static int binary_heap_insert_handle(
    struct heapx_heap *base,
    void *item,
    struct heapx_handle *out
)
{
    return binary_heap_insert_entry(base, item, out);
}

/**
 * @brief Repair an item whose key decreased and priority improved.
 */
static int binary_heap_decrease_key(
    struct heapx_heap *base,
    void *owner
)
{
    struct binary_heap *heap = binary_heap_from_base(base);
    struct binary_heap_locator *locator = owner;

    binary_heap_sift_up(heap, locator->index);
    return 0;
}

/**
 * @brief Remove one item by handle.
 */
static void *binary_heap_remove(
    struct heapx_heap *base,
    void *owner
)
{
    struct binary_heap *heap = binary_heap_from_base(base);
    struct binary_heap_locator *locator = owner;
    struct binary_heap_handle *entry = &heap->data[locator->index];
    size_t index = locator->index;
    void *item = entry->item;

    heapx_handle_release(base, locator->handle);
    heapx_node_pool_free(&heap->locators, locator);

    heap->size--;
    if (index != heap->size) {
        heap->data[index] = heap->data[heap->size];
        if (heap->data[index].locator != NULL)
            heap->data[index].locator->index = index;
        binary_heap_repair_at(heap, index);
    }

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

    return heap->data[0].item;
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
    struct binary_heap_handle *entry;
    void *item;

    if (heap->size == 0)
        return NULL;

    entry = &heap->data[0];
    item = entry->item;
    if (entry->locator != NULL) {
        heapx_handle_release(base, entry->locator->handle);
        heapx_node_pool_free(&heap->locators, entry->locator);
    }
    heap->size--;

    if (heap->size > 0) {
        heap->data[0] = heap->data[heap->size];
        if (heap->data[0].locator != NULL)
            heap->data[0].locator->index = 0;
        binary_heap_sift_down(heap, 0);
    }

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

/** @brief Return 0 when binary heap storage and handle locators are valid. */
static int binary_heap_check_invariants(const struct heapx_heap *base)
{
    const struct binary_heap *heap = binary_heap_from_const_base(base);
    size_t i;

    if (heap->size > heap->capacity)
        return -1;
    if (heap->capacity == 0 && heap->data != NULL)
        return -1;
    if (heap->capacity > 0 && heap->data == NULL)
        return -1;

    for (i = 0; i < heap->size; i++) {
        const struct binary_heap_handle *entry = &heap->data[i];
        size_t left = 2 * i + 1;
        size_t right = left + 1;
        void *owner;

        if (entry->locator != NULL) {
            if (entry->locator->index != i)
                return -1;
            if (heapx_handle_resolve(base, entry->locator->handle, &owner) != 0)
                return -1;
            if (owner != entry->locator)
                return -1;
        }

        if (
            left < heap->size &&
            heap->base.cmp(heap->data[left].item, entry->item) < 0
            )
            return -1;
        if (
            right < heap->size &&
            heap->base.cmp(heap->data[right].item, entry->item) < 0
            )
            return -1;
    }

    return 0;
}
