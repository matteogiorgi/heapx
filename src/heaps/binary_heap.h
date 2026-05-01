#ifndef HPQLIB_HEAPS_BINARY_HEAP_H
#define HPQLIB_HEAPS_BINARY_HEAP_H

#include "hpqlib/priority_queue.h"

/**
 * @file binary_heap.h
 * @brief Private binary min-heap backend factory.
 */

/**
 * @defgroup heap_backends Heap Backends
 * @brief Private heap implementations behind the public priority_queue API.
 *
 * Backends are documented because this repository is used to compare and
 * evolve heap variants. The symbols in this group are still private: external
 * users should create queues only through priority_queue_create().
 *
 * @{
 */

/**
 * @brief Create a priority_queue backed by a binary min-heap.
 *
 * This is a private factory used by the public priority_queue_create()
 * dispatcher. The returned object must be handled only through the abstract
 * priority_queue API.
 *
 * @param cmp Comparator used to order stored items.
 * @return A new queue, or NULL if allocation fails.
 */
struct priority_queue *binary_heap_create(priority_queue_cmp_fn cmp);

/** @} */

#endif
