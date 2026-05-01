#ifndef HPQLIB_HEAPS_KAPLAN_HEAP_H
#define HPQLIB_HEAPS_KAPLAN_HEAP_H

#include "hpqlib/priority_queue.h"

/**
 * @file kaplan_heap.h
 * @brief Private Kaplan/simple-Fibonacci heap backend factory.
 */

/**
 * @addtogroup heap_backends
 * @{
 */

/**
 * @brief Create a priority_queue backed by the simple Fibonacci heap described in
 * "Fibonacci Heaps Revisited", referred to in this project as a Kaplan heap.
 *
 * This is a private factory used by the public priority_queue_create()
 * dispatcher. The returned object must be handled only through the abstract
 * priority_queue API.
 *
 * @param cmp Comparator used to order stored items.
 * @return A new queue, or NULL if allocation fails.
 */
struct priority_queue *kaplan_heap_create(priority_queue_cmp_fn cmp);

/** @} */

#endif
