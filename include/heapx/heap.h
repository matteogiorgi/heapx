#ifndef HEAPX_HEAP_H
#define HEAPX_HEAP_H

#include <stddef.h>
#include <stdint.h>

/**
 * @file heap.h
 * @brief Public C API for heapx heap implementations.
 *
 * heapx is a heap library that exposes one opaque heap type.
 * Client code selects a concrete heap backend at construction time and then
 * uses the common API declared here.
 *
 * @mainpage heapx C API
 *
 * heapx is a small C99 library for heap data structures. The library exposes
 * one native heap API around struct heapx_heap, and lets callers
 * choose a concrete heap implementation when creating a heap. The project is
 * also a compact experimental workspace for comparing heap variants behind one
 * stable interface, so the generated documentation includes both public API
 * notes and selected maintainer-facing implementation notes.
 *
 * The public API is intentionally narrow and acts as the common base for all
 * heap backends:
 *
 * - create and destroy a heap;
 * - insert an item;
 * - inspect or remove the minimum item;
 * - query size and emptiness;
 * - use insertion handles for efficient targeted operations.
 *
 * @section ordering Ordering
 *
 * Ordering is defined by a caller-provided heapx_cmp_fn. A negative
 * comparator result means the left item has higher priority and will be
 * returned before the right item. With a normal ascending integer comparator,
 * smaller integers are extracted first.
 *
 * Equal-priority items are allowed, but extraction order among equivalent
 * items is implementation-dependent and is not stable.
 *
 * @section ownership Ownership
 *
 * Heaps store item pointers as `void *`. heapx never copies,
 * frees, or otherwise owns the pointed-to objects. The caller must keep every
 * stored object valid until it is extracted or the heap is destroyed.
 *
 * Destroying a heap releases only heap-owned storage. It does not call any
 * item destructor and does not inspect stored items after releasing backend
 * nodes.
 *
 * @section implementations Implementations
 *
 * Available heap backends are selected with enum
 * heapx_implementation:
 *
 * | Selector | Backend | Insert | Peek min | Extract min |
 * | --- | --- | --- | --- | --- |
 * | HEAPX_BINARY_HEAP | Binary min-heap | O(log n) | O(1) | O(log n) |
 * | HEAPX_FIBONACCI_HEAP | Fibonacci heap | amortized O(1) | O(1) | amortized O(log n) |
 * | HEAPX_KAPLAN_HEAP | Simple Fibonacci heap from "Fibonacci Heaps Revisited" | amortized O(1) | O(1) | amortized O(log n) |
 *
 * All implementations use the same comparator and ownership contract.
 *
 * Operations that target an existing item use heapx_handle. This
 * follows the Fibonacci heap paper's assumption that decrease-key and arbitrary
 * deletion know the item's heap position.
 *
 * @section null_handles NULL And Invalid Handles
 *
 * Public functions define simple NULL and invalid-handle behavior:
 *
 * - heapx_destroy() accepts NULL and does nothing;
 * - heapx_insert() returns -1 for a NULL heap;
 * - heapx_insert_handle() returns -1 for a NULL heap or NULL out pointer;
 * - heapx_peek_min() and heapx_extract_min() return NULL for a NULL
 *   heap;
 * - heapx_size() returns 0 for a NULL heap;
 * - heapx_empty() treats NULL as empty;
 * - heapx_decrease_key() returns -1 for a NULL heap;
 * - heapx_decrease_key() returns -1 for stale or non-live handles;
 * - heapx_remove() returns NULL for a NULL heap, stale handle, or non-live
 *   handle.
 *
 * @section failures Allocation Failures
 *
 * Constructors return NULL when allocation fails. heapx_insert() and
 * heapx_insert_handle() return -1 when a backend cannot allocate the storage
 * required for the new item. On a failed insert, the item is not inserted and
 * remains entirely caller-owned.
 *
 * @section example Example
 *
 * @code{.c}
 * #include <stdio.h>
 *
 * #include "heapx/heap.h"
 *
 * static int int_cmp(const void *lhs, const void *rhs)
 * {
 *     const int *left = lhs;
 *     const int *right = rhs;
 *
 *     if (*left < *right)
 *         return -1;
 *     if (*left > *right)
 *         return 1;
 *     return 0;
 * }
 *
 * int main(void)
 * {
 *     int values[] = { 7, 3, 9, 1 };
 *     struct heapx_heap *heap;
 *     size_t i;
 *
 *     heap = heapx_create(HEAPX_BINARY_HEAP, int_cmp);
 *     if (heap == NULL)
 *         return 1;
 *
 *     for (i = 0; i < sizeof(values) / sizeof(values[0]); i++)
 *         heapx_insert(heap, &values[i]);
 *
 *     while (!heapx_empty(heap)) {
 *         int *value = heapx_extract_min(heap);
 *         printf("%d\n", *value);
 *     }
 *
 *     heapx_destroy(heap);
 *     return 0;
 * }
 * @endcode
 *
 */

/**
 * @defgroup heapx_heap Heap API
 * @brief Public heap API for heap implementations.
 *
 * This group contains the opaque heap handle, heap implementation selectors,
 * comparator type, and operations used by C clients.
 *
 * @{
 */

/**
 * @brief Comparison function used to order elements in a heap.
 *
 * The function must return a negative value if lhs has higher priority than
 * rhs, zero if they are equivalent, and a positive value if lhs has lower
 * priority than rhs. The library never takes ownership of the pointed values.
 *
 * The comparator must define a stable ordering for all items stored in the same
 * heap. If a stored object's priority changes while it is inside the
 * heap, heap order is no longer defined by heapx.
 *
 * The comparator should behave consistently for repeated calls with the same
 * logical values. It should also avoid mutating the heap or the compared
 * objects in ways that affect their priority while a heap operation is in
 * progress.
 *
 * @param lhs First stored item.
 * @param rhs Second stored item.
 * @return Negative if lhs has higher priority, zero if equivalent, positive if
 *         rhs has higher priority.
 *
 * @warning The comparator must not dereference invalid pointers. The caller is
 *          responsible for keeping stored objects alive.
 */
typedef int (*heapx_cmp_fn)(const void *lhs, const void *rhs);

/**
 * @brief Concrete heap implementations available through the API factory.
 * Client code should select an implementation here and then use only the
 * abstract heapx_heap operations below.
 *
 * The enum values are stable public selectors. They do not expose concrete
 * object layouts, and callers must not cast a struct heapx_heap pointer to
 * any backend-specific type.
 *
 * All backends currently implement min-priority behavior according to the
 * provided comparator. That means "minimum" is not hard-coded as a numeric
 * relation; it is whatever the comparator reports as higher priority.
 */
enum heapx_implementation {
    /**
     * Binary min-heap backed by a contiguous array.
     *
     * This backend has O(log n) insert and extract-min, O(1) peek-min, O(1) size, and O(1)
     * empty checks.
     */
    HEAPX_BINARY_HEAP = 1,
    /**
     * Classic Fibonacci heap backend.
     *
     * This backend has amortized O(1) insert, O(1) peek-min, amortized O(log n) extract-min,
     * O(1) size, and O(1) empty checks.
     */
    HEAPX_FIBONACCI_HEAP = 2,
    /**
     * Simple Fibonacci heap from "Fibonacci Heaps Revisited".
     *
     * This backend is referred to as the Kaplan heap in heapx. It has
     * amortized O(1) insert, O(1) peek-min, amortized O(log n) extract-min, O(1) size, and
     * O(1) empty checks.
     */
    HEAPX_KAPLAN_HEAP = 3
};

/**
 * @brief Opaque heap API handle.
 *
 * Instances must be created with heapx_create() and released with
 * heapx_destroy().
 *
 * The pointed-to object embeds backend-specific storage that is private to
 * heapx. Client code should pass this handle only to the functions declared in
 * this header.
 */
struct heapx_heap;

/**
 * @brief Generational handle for one item stored in a heap.
 *
 * Handles are value tokens returned through heapx_insert_handle(). They do
 * not own memory and callers may copy them freely. A handle is live only while
 * its associated item remains stored in the heap that created it.
 *
 * A handle becomes non-live after the item is removed by heapx_extract_min(),
 * heapx_remove(), or any future operation that deletes that item. Passing a
 * non-live handle back to heapx_decrease_key() or heapx_remove() is allowed
 * and fails cleanly. A logical heap identifier and slot generations prevent
 * handles from one heap or stale handles from becoming valid after internal
 * slot reuse.
 */
struct heapx_handle {
    /** Logical identifier of the heap that created this handle. */
    uint64_t heap_id;
    /** Internal slot index in the creating heap's handle table. */
    size_t slot;
    /** Generation recorded when the handle was created. */
    unsigned generation;
};

/**
 * @brief Create an empty heap backed by the selected heap implementation.
 *
 * The returned heap uses cmp for every ordering decision until it is
 * destroyed. The comparator pointer must remain valid for the lifetime of the
 * heap.
 *
 * The selected implementation is fixed for the lifetime of the heap. Moving
 * items between different implementations must be done by client code with
 * explicit extract/insert operations.
 *
 * @param implementation Heap backend selector.
 * @param cmp Comparator used to order stored items.
 * @return A new heap on success, or NULL if the implementation is
 *         unsupported, cmp is NULL, or allocation fails.
 *
 * @post The returned heap is empty.
 * @see heapx_destroy
 */
struct heapx_heap *heapx_create(
    enum heapx_implementation implementation,
    heapx_cmp_fn cmp
);

/**
 * @brief Destroy a heap created by heapx_create().
 *
 * This releases only memory owned by the heap itself. Stored items are not
 * destroyed. Passing NULL is allowed and has no effect.
 *
 * @param heap Heap to destroy, or NULL.
 *
 * @warning Any item pointers still stored in heap remain the caller's
 *          responsibility.
 * @see heapx_create
 */
void heapx_destroy(struct heapx_heap *heap);

/**
 * @brief Insert an item into the heap.
 *
 * The item pointer is stored as-is and remains owned by the caller.
 * heapx does not copy the pointed-to object, so later changes to that object
 * are visible to the comparator. Mutating an object so that its key becomes
 * smaller, and therefore higher priority, can violate heap order unless
 * heapx_decrease_key() is called with the item's handle afterwards.
 *
 * @param heap Heap receiving the item.
 * @param item Caller-owned item pointer to store.
 * @return 0 on success, or -1 if heap is NULL or allocation fails.
 *
 * @note item may be NULL. In that case, client code must ensure its comparator
 *       can safely handle NULL items before any comparison involving that item
 *       occurs.
 * @post On success, heapx_size(heap) increases by one.
 */
int heapx_insert(struct heapx_heap *heap, void *item);

/**
 * @brief Insert an item and return a handle to its heap position.
 *
 * The output handle can be used with heapx_decrease_key() and heapx_remove()
 * while the associated item remains stored in the heap. Handles are plain
 * values and do not need to be freed.
 *
 * If the item later leaves the heap, the handle becomes non-live. Passing that
 * non-live handle back to heapx_decrease_key() or heapx_remove() is allowed
 * and returns failure.
 *
 * @param heap Heap receiving the item.
 * @param item Caller-owned item pointer to store.
 * @param out Receives the handle on success.
 * @return 0 on success, or -1 if heap or out is NULL, or allocation fails.
 *
 * @post On success, heapx_size(heap) increases by one.
 */
int heapx_insert_handle(
    struct heapx_heap *heap,
    void *item,
    struct heapx_handle *out
);

/**
 * @brief Repair heap order after a handled item moves closer to the minimum.
 *
 * This operation assumes the caller has already updated the pointed-to object so
 * that it now compares before its old position according to the heap
 * comparator.
 *
 * @param heap Heap containing handle.
 * @param handle Handle returned by heapx_insert_handle().
 * @return 0 on success, or -1 if heap is NULL, if handle is not valid for heap,
 *         or if handle is no longer live.
 *
 * @warning Calling this after lowering an item's priority does not restore
 *          heap order.
 */
int heapx_decrease_key(
    struct heapx_heap *heap,
    struct heapx_handle handle
);

/**
 * @brief Remove a handled item from a heap.
 *
 * The pointed-to object remains caller-owned and is not destroyed.
 *
 * @param heap Heap to remove from.
 * @param handle Handle returned by heapx_insert_handle().
 * @return The removed item on success, or NULL if heap is NULL, if handle is
 *         not valid for heap, or if handle is no longer live.
 *
 * @note If the stored item pointer itself is NULL, a successful remove also
 *       returns NULL. Use heapx_contains() or external bookkeeping if
 *       that distinction matters.
 * @post On success, heapx_size(heap) decreases by one.
 */
void *heapx_remove(
    struct heapx_heap *heap,
    struct heapx_handle handle
);

/**
 * @brief Return whether heap contains item by pointer identity.
 *
 * @param heap Heap to inspect, or NULL.
 * @param item Item pointer to search for.
 * @return Non-zero if item is stored in heap, zero otherwise.
 *
 * @note Current backends implement this as a linear pointer-identity search.
 */
int heapx_contains(const struct heapx_heap *heap, const void *item);

/**
 * @brief Return the minimum item without removing it.
 *
 * @param heap Heap to inspect, or NULL.
 * @return The minimum item, or NULL if heap is NULL or empty.
 *
 * The returned pointer remains stored in the heap. Client code must not free
 * or invalidate it before the item is extracted or the heap is destroyed.
 *
 * @note Because NULL may also be a stored item pointer, callers that insert
 *       NULL items should use heapx_empty() to distinguish an empty
 *       heap from a stored NULL item.
 * @see heapx_extract_min
 */
void *heapx_peek_min(const struct heapx_heap *heap);

/**
 * @brief Remove and return the minimum item.
 *
 * Ownership of the returned item remains with the caller.
 * The heap no longer stores the returned pointer after this call succeeds on a
 * non-empty heap.
 *
 * @param heap Heap to remove from, or NULL.
 * @return The removed item, or NULL if heap is NULL or empty.
 *
 * @note Because NULL may also be a stored item pointer, callers that insert
 *       NULL items should use heapx_empty() before extracting when they
 *       need to distinguish an empty heap from a stored NULL item.
 * @post If a non-empty heap is provided, heapx_size(heap) decreases
 *       by one.
 * @see heapx_peek_min
 */
void *heapx_extract_min(struct heapx_heap *heap);

/**
 * @brief Return the number of items currently stored in the heap.
 *
 * @param heap Heap to inspect, or NULL.
 * @return Number of stored items, or 0 if heap is NULL.
 *
 * @note The operation is O(1) for all current backends.
 */
size_t heapx_size(const struct heapx_heap *heap);

/**
 * @brief Return non-zero when the heap contains no items.
 *
 * A NULL heap is treated as empty.
 *
 * @param heap Heap to inspect, or NULL.
 * @return Non-zero if heap is NULL or empty, zero otherwise.
 *
 * @note The operation is O(1) for all current backends.
 */
int heapx_empty(const struct heapx_heap *heap);

/** @} */

#endif
