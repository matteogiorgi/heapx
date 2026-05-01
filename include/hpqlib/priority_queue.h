#ifndef HPQLIB_PRIORITY_QUEUE_H
#define HPQLIB_PRIORITY_QUEUE_H

#include <stddef.h>

/**
 * @file priority_queue.h
 * @brief Public C API for hpqlib priority queues.
 *
 * hpqlib exposes one opaque priority-queue type. Client code selects a
 * concrete heap backend at construction time and then uses the common API
 * declared here.
 *
 * @mainpage hpqlib C API
 *
 * hpqlib is a small C99 library for heap-backed priority queues. The library
 * exposes one abstract handle, struct priority_queue, and lets callers choose
 * a concrete heap implementation when creating a queue. The project is also a
 * compact experimental workspace for comparing heap variants behind one stable
 * interface, so the generated documentation includes both public API notes and
 * selected maintainer-facing implementation notes.
 *
 * The public API is intentionally narrow:
 *
 * - create and destroy a queue;
 * - insert an item;
 * - inspect or remove the highest-priority item;
 * - query size and emptiness;
 * - use insertion handles for efficient targeted operations.
 *
 * @section ordering Ordering
 *
 * Ordering is defined by a caller-provided priority_queue_cmp_fn. A negative
 * comparator result means the left item has higher priority and will be
 * returned before the right item. With a normal ascending integer comparator,
 * smaller integers are popped first.
 *
 * Equal-priority items are allowed, but extraction order among equivalent
 * items is implementation-dependent and is not stable.
 *
 * @section ownership Ownership
 *
 * Queues store item pointers as `void *`. hpqlib never copies, frees, or
 * otherwise owns the pointed-to objects. The caller must keep every stored
 * object valid until it is popped or the queue is destroyed.
 *
 * Destroying a queue releases only queue-owned storage. It does not call any
 * item destructor and does not inspect stored items after releasing backend
 * nodes.
 *
 * @section implementations Implementations
 *
 * Available backends are selected with enum priority_queue_implementation:
 *
 * | Selector | Backend | Push | Peek | Pop |
 * | --- | --- | --- | --- | --- |
 * | PRIORITY_QUEUE_BINARY_HEAP | Binary min-heap | O(log n) | O(1) | O(log n) |
 * | PRIORITY_QUEUE_FIBONACCI_HEAP | Fibonacci heap | amortized O(1) | O(1) | amortized O(log n) |
 * | PRIORITY_QUEUE_KAPLAN_HEAP | Simple Fibonacci heap from "Fibonacci Heaps Revisited" | amortized O(1) | O(1) | amortized O(log n) |
 *
 * All implementations use the same comparator and ownership contract.
 *
 * Operations that target an existing item use priority_queue_handle. This
 * follows the Fibonacci heap paper's assumption that decrease-key and arbitrary
 * deletion know the item's heap position.
 *
 * @section null_handles NULL Handles
 *
 * Public functions define simple NULL-handle behavior:
 *
 * - priority_queue_destroy() accepts NULL and does nothing;
 * - priority_queue_push() returns -1 for a NULL queue;
 * - priority_queue_push_handle() returns NULL for a NULL queue;
 * - priority_queue_peek() and priority_queue_pop() return NULL for a NULL
 *   queue;
 * - priority_queue_size() returns 0 for a NULL queue;
 * - priority_queue_empty() treats NULL as empty;
 * - priority_queue_decrease_key() returns -1 for a NULL queue;
 * - priority_queue_remove() returns NULL for a NULL queue.
 *
 * @section failures Allocation Failures
 *
 * Constructors return NULL when allocation fails. priority_queue_push() returns
 * -1 when a backend cannot allocate the storage required for the new item.
 * priority_queue_push_handle() returns NULL for the same condition. On a failed
 * push, the item is not inserted and remains entirely caller-owned.
 *
 * @section example Example
 *
 * @code{.c}
 * #include <stdio.h>
 *
 * #include "hpqlib/priority_queue.h"
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
 *     struct priority_queue *queue;
 *     size_t i;
 *
 *     queue = priority_queue_create(PRIORITY_QUEUE_BINARY_HEAP, int_cmp);
 *     if (queue == NULL)
 *         return 1;
 *
 *     for (i = 0; i < sizeof(values) / sizeof(values[0]); i++)
 *         priority_queue_push(queue, &values[i]);
 *
 *     while (!priority_queue_empty(queue)) {
 *         int *value = priority_queue_pop(queue);
 *         printf("%d\n", *value);
 *     }
 *
 *     priority_queue_destroy(queue);
 *     return 0;
 * }
 * @endcode
 *
 */

/**
 * @defgroup priority_queue Priority Queue
 * @brief Public abstract priority-queue API.
 *
 * This group contains the opaque handle, implementation selectors, comparator
 * type, and operations used by C clients.
 *
 * @{
 */

/**
 * @brief Comparison function used to order elements in a priority queue.
 *
 * The function must return a negative value if lhs has higher priority than
 * rhs, zero if they are equivalent, and a positive value if lhs has lower
 * priority than rhs. The library never takes ownership of the pointed values.
 *
 * The comparator must define a stable ordering for all items stored in the same
 * queue. If a stored object's priority changes while it is inside the queue,
 * heap order is no longer defined by hpqlib.
 *
 * The comparator should behave consistently for repeated calls with the same
 * logical values. It should also avoid mutating the queue or the compared
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
typedef int (*priority_queue_cmp_fn)(const void *lhs, const void *rhs);

/**
 * @brief Concrete heap-priority-queue implementations available through the
 * factory. Client code should select an implementation here and then use only
 * the abstract priority_queue operations below.
 *
 * The enum values are stable public selectors. They do not expose concrete
 * object layouts, and callers must not cast a struct priority_queue pointer to
 * any backend-specific type.
 *
 * All backends currently implement min-priority behavior according to the
 * provided comparator. That means "minimum" is not hard-coded as a numeric
 * relation; it is whatever the comparator reports as higher priority.
 */
enum priority_queue_implementation {
    /**
     * Binary min-heap backed by a contiguous array.
     *
     * This backend has O(log n) push and pop, O(1) peek, O(1) size, and O(1)
     * empty checks.
     */
    PRIORITY_QUEUE_BINARY_HEAP = 1,
    /**
     * Classic Fibonacci heap backend.
     *
     * This backend has amortized O(1) push, O(1) peek, amortized O(log n) pop,
     * O(1) size, and O(1) empty checks.
     */
    PRIORITY_QUEUE_FIBONACCI_HEAP = 2,
    /**
     * Simple Fibonacci heap from "Fibonacci Heaps Revisited".
     *
     * This backend is referred to as the Kaplan heap in hpqlib. It has
     * amortized O(1) push, O(1) peek, amortized O(log n) pop, O(1) size, and
     * O(1) empty checks.
     */
    PRIORITY_QUEUE_KAPLAN_HEAP = 3
};

/**
 * @brief Opaque priority-queue handle.
 *
 * Instances must be created with priority_queue_create() and released with
 * priority_queue_destroy().
 *
 * The pointed-to object embeds backend-specific storage that is private to
 * hpqlib. Client code should pass this handle only to the functions declared in
 * this header.
 */
struct priority_queue;

/**
 * @brief Opaque handle for one item stored in a priority queue.
 *
 * Handles are returned by priority_queue_push_handle(). They are valid only
 * while the associated item remains stored in the queue that created them. A
 * handle becomes invalid after the item is removed by priority_queue_pop(),
 * priority_queue_remove(), priority_queue_destroy(), or any future operation
 * that deletes that item.
 */
struct priority_queue_handle;

/**
 * @brief Create an empty priority queue backed by the selected implementation.
 *
 * The returned queue uses cmp for every ordering decision until it is
 * destroyed. The comparator pointer must remain valid for the lifetime of the
 * queue.
 *
 * The selected implementation is fixed for the lifetime of the queue. Moving
 * items between different implementations must be done by client code with
 * explicit pop/push operations.
 *
 * @param implementation Heap backend selector.
 * @param cmp Comparator used to order stored items.
 * @return A new queue on success, or NULL if the implementation is
 *         unsupported, cmp is NULL, or allocation fails.
 *
 * @post The returned queue is empty.
 * @see priority_queue_destroy
 */
struct priority_queue *priority_queue_create(
    enum priority_queue_implementation implementation,
    priority_queue_cmp_fn cmp
);

/**
 * @brief Destroy a priority queue created by priority_queue_create().
 *
 * This releases only memory owned by the queue itself. Stored items are not
 * destroyed. Passing NULL is allowed and has no effect.
 *
 * @param queue Queue to destroy, or NULL.
 *
 * @warning Any item pointers still stored in queue remain the caller's
 *          responsibility.
 * @see priority_queue_create
 */
void priority_queue_destroy(struct priority_queue *queue);

/**
 * @brief Insert an item into the priority queue.
 *
 * The item pointer is stored as-is and remains owned by the caller.
 * hpqlib does not copy the pointed-to object, so later changes to that object
 * are visible to the comparator. Mutating an object so that its key becomes
 * smaller, and therefore higher priority, can violate heap order unless
 * priority_queue_decrease_key() is called with the item's handle afterwards.
 *
 * @param queue Queue receiving the item.
 * @param item Caller-owned item pointer to store.
 * @return 0 on success, or -1 if queue is NULL or allocation fails.
 *
 * @note item may be NULL. In that case, client code must ensure its comparator
 *       can safely handle NULL items before any comparison involving that item
 *       occurs.
 * @post On success, priority_queue_size(queue) increases by one.
 */
int priority_queue_push(struct priority_queue *queue, void *item);

/**
 * @brief Insert an item and return a handle to its heap position.
 *
 * The returned handle can be used with priority_queue_decrease_key() and
 * priority_queue_remove(). Client code must not free the handle and must not
 * use it after the item leaves the queue.
 *
 * @param queue Queue receiving the item.
 * @param item Caller-owned item pointer to store.
 * @return A handle on success, or NULL if queue is NULL or allocation fails.
 *
 * @post On success, priority_queue_size(queue) increases by one.
 */
struct priority_queue_handle *priority_queue_push_handle(
    struct priority_queue *queue,
    void *item
);

/**
 * @brief Repair queue order after a handled item moves closer to the minimum.
 *
 * This operation assumes the caller has already updated the pointed-to object so
 * that it now compares before its old position according to the queue
 * comparator.
 *
 * @param queue Queue containing handle.
 * @param handle Handle returned by priority_queue_push_handle().
 * @return 0 on success, or -1 if queue or handle is NULL, or if handle does not
 *         belong to queue.
 *
 * @warning Calling this after lowering an item's priority, or after mutating an
 *          item whose handle is invalid, does not restore heap order.
 */
int priority_queue_decrease_key(
    struct priority_queue *queue,
    struct priority_queue_handle *handle
);

/**
 * @brief Remove a handled item from a priority queue.
 *
 * The pointed-to object remains caller-owned and is not destroyed.
 *
 * @param queue Queue to remove from.
 * @param handle Handle returned by priority_queue_push_handle().
 * @return The removed item on success, or NULL if queue or handle is NULL, or
 *         if handle does not belong to queue.
 *
 * @note If the stored item pointer itself is NULL, a successful remove also
 *       returns NULL. Use priority_queue_contains() or external bookkeeping if
 *       that distinction matters.
 * @post On success, priority_queue_size(queue) decreases by one.
 */
void *priority_queue_remove(
    struct priority_queue *queue,
    struct priority_queue_handle *handle
);

/**
 * @brief Return whether queue contains item by pointer identity.
 *
 * @param queue Queue to inspect, or NULL.
 * @param item Item pointer to search for.
 * @return Non-zero if item is stored in queue, zero otherwise.
 *
 * @note Current backends implement this as a linear pointer-identity search.
 */
int priority_queue_contains(const struct priority_queue *queue, const void *item);

/**
 * @brief Return the highest-priority item without removing it.
 *
 * @param queue Queue to inspect, or NULL.
 * @return The highest-priority item, or NULL if queue is NULL or empty.
 *
 * The returned pointer remains stored in the queue. Client code must not free
 * or invalidate it before the item is popped or the queue is destroyed.
 *
 * @note Because NULL may also be a stored item pointer, callers that insert
 *       NULL items should use priority_queue_empty() to distinguish an empty
 *       queue from a stored NULL item.
 * @see priority_queue_pop
 */
void *priority_queue_peek(const struct priority_queue *queue);

/**
 * @brief Remove and return the highest-priority item.
 *
 * Ownership of the returned item remains with the caller.
 * The queue no longer stores the returned pointer after this call succeeds on a
 * non-empty queue.
 *
 * @param queue Queue to remove from, or NULL.
 * @return The removed item, or NULL if queue is NULL or empty.
 *
 * @note Because NULL may also be a stored item pointer, callers that insert
 *       NULL items should use priority_queue_empty() before popping when they
 *       need to distinguish an empty queue from a stored NULL item.
 * @post If a non-empty queue is provided, priority_queue_size(queue) decreases
 *       by one.
 * @see priority_queue_peek
 */
void *priority_queue_pop(struct priority_queue *queue);

/**
 * @brief Return the number of items currently stored in the queue.
 *
 * @param queue Queue to inspect, or NULL.
 * @return Number of stored items, or 0 if queue is NULL.
 *
 * @note The operation is O(1) for all current backends.
 */
size_t priority_queue_size(const struct priority_queue *queue);

/**
 * @brief Return non-zero when the queue contains no items.
 *
 * A NULL queue is treated as empty.
 *
 * @param queue Queue to inspect, or NULL.
 * @return Non-zero if queue is NULL or empty, zero otherwise.
 *
 * @note The operation is O(1) for all current backends.
 */
int priority_queue_empty(const struct priority_queue *queue);

/** @} */

#endif
