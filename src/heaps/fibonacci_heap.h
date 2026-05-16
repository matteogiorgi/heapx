#ifndef HEAPX_HEAPS_FIBONACCI_HEAP_H
#define HEAPX_HEAPS_FIBONACCI_HEAP_H

#include "heapx/heap.h"

/**
 * @file fibonacci_heap.h
 * @brief Private Fibonacci heap backend factory.
 */

 /**
  * @addtogroup heap_backends
  * @{
  */

  /**
   * @brief Create a heapx_heap backed by a Fibonacci heap.
   *
   * This is a private factory used by the public heapx_create()
   * dispatcher. The returned object must be handled only through the abstract
   * heapx_heap API.
   *
   * @param cmp Comparator used to order stored items.
   * @return A new heap, or NULL if allocation fails.
   */
struct heapx_heap *fibonacci_heap_create(heapx_cmp_fn cmp);

/** @} */

#endif
