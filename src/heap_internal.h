#ifndef HEAPX_HEAP_INTERNAL_H
#define HEAPX_HEAP_INTERNAL_H

#include <stddef.h>
#include <stdint.h>

#ifdef HEAPX_ENABLE_INTERNAL_CHECKS
#include <assert.h>
#endif

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
 * the correct concrete object. Targeted-operation callbacks receive the
 * already validated backend owner pointer associated with a live generational
 * handle. They should preserve the public ownership contract: stored items
 * remain caller-owned, and destroy callbacks release only backend-owned storage
 * inside the concrete heap object. The common dispatch layer releases the
 * handle slot table and the concrete heap allocation itself after the destroy
 * callback returns.
 */
struct heapx_vtable {
    /** Release backend-owned storage. The heap pointer is never NULL here. */
    void (*destroy)(struct heapx_heap *heap);
    /** Insert an item into a valid heap. */
    int (*insert)(struct heapx_heap *heap, void *item);
    /** Insert an item and return its generational handle through out. */
    int (*insert_handle)(
        struct heapx_heap *heap,
        void *item,
        struct heapx_handle *out
    );
    /** Repair heap order after a stored item moves closer to the minimum. */
    int (*decrease_key)(
        struct heapx_heap *heap,
        void *owner
    );
    /** Remove one stored item by handle and return the item pointer. */
    void *(*remove)(
        struct heapx_heap *heap,
        void *owner
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
    /** Return 0 when backend invariants hold, non-zero otherwise. */
    int (*check_invariants)(const struct heapx_heap *heap);
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
    /** Logical heap identifier used by public generational handles. */
    uint64_t id;
    /** Generational handle slots for targeted operations. */
    struct heapx_handle_slot *handle_slots;
    /** Number of allocated handle slots. */
    size_t handle_slot_count;
    /** Capacity of the handle slot table. */
    size_t handle_slot_capacity;
    /** Head of the free-slot list, or SIZE_MAX if empty. */
    size_t free_handle_slot;
};

/**
 * @brief Internal slot backing a public generational handle.
 *
 * Slots are reused through a free list. The generation is incremented whenever
 * a live slot is released, so stale handles cannot refer to a future item that
 * happens to reuse the same slot index.
 */
struct heapx_handle_slot {
    /** Caller-owned item pointer associated with this handle while live. */
    void *item;
    /** Backend-owned node or slot object while live. */
    void *owner;
    /** Current slot generation. */
    unsigned generation;
    /** Non-zero while the associated item remains stored in the heap. */
    int live;
    /** Next slot in the free list when not live. */
    size_t next_free;
};

struct heapx_pool_block;

/**
 * @brief Fixed-size object pool used by pointer-heavy heap backends.
 *
 * The pool owns blocks of same-sized objects and recycles released objects
 * through an intrusive free list. It is intended for backend node wrappers, not
 * caller-owned heap items.
 */
struct heapx_node_pool {
    /** Size of each object slot, including internal alignment padding. */
    size_t object_size;
    /** Number of object slots allocated per block. */
    size_t block_capacity;
    /** Head of the free-object list. */
    void *free_list;
    /** List of allocated blocks owned by the pool. */
    struct heapx_pool_block *blocks;
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

/** @brief Allocate or reuse a slot and create a public generational handle. */
int heapx_handle_attach(
    struct heapx_heap *heap,
    void *item,
    void *owner,
    struct heapx_handle *out
);

/** @brief Release the handle slot table owned by heap. */
void heapx_heap_destroy_handle_slots(struct heapx_heap *heap);

/** @brief Resolve a public handle to its live backend owner. */
int heapx_handle_resolve(
    const struct heapx_heap *heap,
    struct heapx_handle handle,
    void **owner
);

/** @brief Release the live slot identified by handle. */
void heapx_handle_release(struct heapx_heap *heap, struct heapx_handle handle);

/** @brief Initialize a fixed-size node pool. */
int heapx_node_pool_init(
    struct heapx_node_pool *pool,
    size_t object_size,
    size_t block_capacity
);

/** @brief Release every block owned by pool. */
void heapx_node_pool_destroy(struct heapx_node_pool *pool);

/** @brief Allocate one object from pool. */
void *heapx_node_pool_alloc(struct heapx_node_pool *pool);

/** @brief Return one object to pool for future reuse. */
void heapx_node_pool_free(struct heapx_node_pool *pool, void *object);

/** @brief Return 0 when common and backend invariants hold. */
int heapx_check_invariants(const struct heapx_heap *heap);

#ifdef HEAPX_ENABLE_INTERNAL_CHECKS
/** @brief Assert common and backend invariants in debug-check builds. */
#define HEAPX_ASSERT_INVARIANTS(heap) \
    assert(heapx_check_invariants((heap)) == 0)
#else
/** @brief No-op invariant assertion for normal builds. */
#define HEAPX_ASSERT_INVARIANTS(heap) ((void)(heap))
#endif

/** @} */

#endif
