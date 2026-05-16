#ifndef HEAPX_HEAPS_KAPLAN_HEAP_H
#define HEAPX_HEAPS_KAPLAN_HEAP_H

#include "heapx/heap.h"

/**
 * @file kaplan_heap.h
 * @brief Private Kaplan/simple-Fibonacci heap backend factory.
 */

 /**
  * @addtogroup heap_backends
  * @{
  */

  /**
   * @brief Create a heapx_heap backed by the simple Fibonacci heap described in
   * "Fibonacci Heaps Revisited", referred to in this project as a Kaplan heap.
   *
   * This is a private factory used by the public heapx_create()
   * dispatcher. The returned object must be handled only through the abstract
   * heapx_heap API.
   *
   * @param cmp Comparator used to order stored items.
   * @return A new heap, or NULL if allocation fails.
   */
struct heapx_heap *kaplan_heap_create(heapx_cmp_fn cmp);

/** @} */

#endif
