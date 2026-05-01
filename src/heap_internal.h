#ifndef HEAPX_HEAP_INTERNAL_H
#define HEAPX_HEAP_INTERNAL_H

#include <stddef.h>

#include "heapx/heap.h"

/**
 * @file heap_internal.h
 * @brief Internal base layout and dispatch table for heap backends.
 *
 * This header is private to heapx. It documents the small object model used by
 * the C implementation: every concrete heap backend embeds
 * struct heapx_heap as its first field and provides a vtable implementing
 * the public heap operations.
 *
 * The internal API deliberately mirrors only the public operations. Backend
 * heap implementations are responsible for their own storage and invariants,
 * while src/heap.c owns public NULL-handle behavior and heap
 * implementation selection.
 */

/**
 * @defgroup internals Heap Internals
 * @brief Private dispatch and object-layout helpers used by heap backends.
 *
 * These declarations are not part of the installed public API. They are
 * documented for maintainers and for experiments that add new heap backends.
 * A new backend should provide one concrete object whose first field is struct
 * heapx_heap, one static heapx_vtable, and one private factory
 * used by heapx_create().
 *
 * @{
 */

/**
 * @brief Implementation dispatch table for the heapx_heap API.
 *
 * Each concrete backend provides one static vtable and implements all entries
 * using the common base pointer. Public functions perform basic NULL handling
 * before dispatching through this table.
 *
 * Vtable functions may assume their heap argument is non-NULL and points to
 * the correct concrete object. They should preserve the public ownership
 * contract: stored items remain caller-owned, and destroy callbacks release
 * only backend-owned storage.
 */
struct heapx_vtable {
    /** Release backend-owned storage. The heap pointer is never NULL here. */
    void (*destroy)(struct heapx_heap *heap);
    /** Insert an item into a valid heap. */
    int (*insert)(struct heapx_heap *heap, void *item);
    /** Insert an item and return its backend-owned handle. */
    struct heapx_handle *(*insert_handle)(
        struct heapx_heap *heap,
        void *item
    );
    /** Repair heap order after a stored item moves closer to the minimum. */
    int (*decrease_key)(
        struct heapx_heap *heap,
        struct heapx_handle *handle
    );
    /** Remove one stored item by handle and return the item pointer. */
    void *(*remove)(
        struct heapx_heap *heap,
        struct heapx_handle *handle
    );
    /** Return non-zero if the heap stores item by pointer identity. */
    int (*contains)(const struct heapx_heap *heap, const void *item);
    /** Return the current minimum item from a valid heap, or NULL if empty. */
    void *(*peek_min)(const struct heapx_heap *heap);
    /** Remove and return the current minimum item from a valid heap. */
    void *(*extract_min)(struct heapx_heap *heap);
    /** Return the number of stored items in a valid heap. */
    size_t(*size)(const struct heapx_heap *heap);
    /** Return non-zero if a valid heap is empty. */
    int (*empty)(const struct heapx_heap *heap);
};

/**
 * @brief Common base header embedded as the first field of every heap.
 *
 * This layout allows a concrete heap implementation pointer to be safely
 * exposed as struct heapx_heap * and later recovered by the
 * implementation-specific cast helpers.
 *
 * This is the only common state shared by all backends. Size, root pointers,
 * arrays, and node storage belong to concrete implementation structs.
 */
struct heapx_heap {
    /** Backend operation table. */
    const struct heapx_vtable *vtable;
    /** Comparator shared by the public wrapper and backend implementation. */
    heapx_cmp_fn cmp;
};

/**
 * @brief Common header embedded in every backend-specific item handle.
 *
 * Backends may embed this as the first field of a node or of a separate handle
 * object. The public API uses it to validate that a live handle belongs to the
 * heap passed to a targeted operation.
 */
struct heapx_handle {
    /** Heap that owns this handle while it is live. */
    struct heapx_heap *heap;
    /** Caller-owned item pointer associated with this handle. */
    void *item;
};

/**
 * @brief Initialize the common heapx_heap base object.
 *
 * Concrete constructors call this after allocating their full implementation
 * object and before returning the abstract handle to client code.
 *
 * The function does not allocate and does not validate its arguments; private
 * constructors are expected to pass valid pointers.
 *
 * @param heap Base object embedded in a concrete backend.
 * @param vtable Backend operation table.
 * @param cmp Comparator to store in the base object.
 */
void heapx_heap_init(
    struct heapx_heap *heap,
    const struct heapx_vtable *vtable,
    heapx_cmp_fn cmp
);

/**
 * @brief Initialize a backend-owned handle for a stored item.
 */
void heapx_handle_init(
    struct heapx_handle *handle,
    struct heapx_heap *heap,
    void *item
);

/** @} */

#endif
