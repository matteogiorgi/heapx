#ifndef HPQLIB_PRIORITY_QUEUE_INTERNAL_H
#define HPQLIB_PRIORITY_QUEUE_INTERNAL_H

#include <stddef.h>

#include "hpqlib/priority_queue.h"

/**
 * @file priority_queue_internal.h
 * @brief Internal base layout and dispatch table for priority queues.
 *
 * This header is private to hpqlib. It documents the small object model used by
 * the C implementation: every concrete backend embeds struct priority_queue as
 * its first field and provides a vtable implementing the public operations.
 *
 * The internal API deliberately mirrors only the public operations. Backend
 * implementations are responsible for their own storage and invariants, while
 * src/priority_queue.c owns public NULL-handle behavior and implementation
 * selection.
 */

/**
 * @defgroup internals Priority Queue Internals
 * @brief Private dispatch and object-layout helpers used by heap backends.
 *
 * These declarations are not part of the installed public API. They are
 * documented for maintainers and for experiments that add new heap backends.
 * A new backend should provide one concrete object whose first field is struct
 * priority_queue, one static priority_queue_vtable, and one private factory
 * used by priority_queue_create().
 *
 * @{
 */

/**
 * @brief Implementation dispatch table for the abstract priority_queue API.
 *
 * Each concrete backend provides one static vtable and implements all entries
 * using the common base pointer. Public functions perform basic NULL handling
 * before dispatching through this table.
 *
 * Vtable functions may assume their queue argument is non-NULL and points to
 * the correct concrete object. They should preserve the public ownership
 * contract: stored items remain caller-owned, and destroy callbacks release
 * only backend-owned storage.
 */
struct priority_queue_vtable {
    /** Release backend-owned storage. The queue pointer is never NULL here. */
    void (*destroy)(struct priority_queue *queue);
    /** Insert an item into a valid queue. */
    int (*push)(struct priority_queue *queue, void *item);
    /** Insert an item and return its backend-owned handle. */
    struct priority_queue_handle *(*push_handle)(
        struct priority_queue *queue,
        void *item
    );
    /** Repair heap order after a stored item moves closer to the minimum. */
    int (*decrease_key)(
        struct priority_queue *queue,
        struct priority_queue_handle *handle
    );
    /** Remove one stored item by handle and return the item pointer. */
    void *(*remove)(
        struct priority_queue *queue,
        struct priority_queue_handle *handle
    );
    /** Return non-zero if the queue stores item by pointer identity. */
    int (*contains)(const struct priority_queue *queue, const void *item);
    /** Return the current minimum item from a valid queue, or NULL if empty. */
    void *(*peek)(const struct priority_queue *queue);
    /** Remove and return the current minimum item from a valid queue. */
    void *(*pop)(struct priority_queue *queue);
    /** Return the number of stored items in a valid queue. */
    size_t(*size)(const struct priority_queue *queue);
    /** Return non-zero if a valid queue is empty. */
    int (*empty)(const struct priority_queue *queue);
};

/**
 * @brief Common header embedded as the first field of every concrete queue.
 *
 * This layout allows a concrete implementation pointer to be safely exposed as
 * struct priority_queue * and later recovered by the implementation-specific
 * cast helpers.
 *
 * This is the only common state shared by all backends. Size, root pointers,
 * arrays, and node storage belong to concrete implementation structs.
 */
struct priority_queue {
    /** Backend operation table. */
    const struct priority_queue_vtable *vtable;
    /** Comparator shared by the public wrapper and backend implementation. */
    priority_queue_cmp_fn cmp;
};

/**
 * @brief Common header embedded in every backend-specific item handle.
 *
 * Backends may embed this as the first field of a node or of a separate handle
 * object. The public API uses it to validate that a live handle belongs to the
 * queue passed to a targeted operation.
 */
struct priority_queue_handle {
    /** Queue that owns this handle while it is live. */
    struct priority_queue *queue;
    /** Caller-owned item pointer associated with this handle. */
    void *item;
};

/**
 * @brief Initialize the common priority_queue base object.
 *
 * Concrete constructors call this after allocating their full implementation
 * object and before returning the abstract handle to client code.
 *
 * The function does not allocate and does not validate its arguments; private
 * constructors are expected to pass valid pointers.
 *
 * @param queue Base object embedded in a concrete backend.
 * @param vtable Backend operation table.
 * @param cmp Comparator to store in the base object.
 */
void priority_queue_init(
    struct priority_queue *queue,
    const struct priority_queue_vtable *vtable,
    priority_queue_cmp_fn cmp
);

/**
 * @brief Initialize a backend-owned handle for a stored item.
 */
void priority_queue_handle_init(
    struct priority_queue_handle *handle,
    struct priority_queue *queue,
    void *item
);

/** @} */

#endif
