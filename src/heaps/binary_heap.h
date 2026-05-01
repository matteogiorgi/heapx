#ifndef HEAPX_HEAPS_BINARY_HEAP_H
#define HEAPX_HEAPS_BINARY_HEAP_H

#include "heapx/heap.h"

/**
 * @file binary_heap.h
 * @brief Private binary min-heap backend factory.
 */

/**
 * @defgroup heap_backends Heap Backends
 * @brief Private heap implementations behind the public heapx_heap API.
 *
 * Backends are documented because this repository is used to compare and
 * evolve heap variants. The symbols in this group are still private: external
 * users should create heaps only through heapx_create().
 *
 * @{
 */

/**
 * @brief Create a heapx_heap backed by a binary min-heap.
 *
 * This is a private factory used by the public heapx_create()
 * dispatcher. The returned object must be handled only through the abstract
 * heapx_heap API.
 *
 * @param cmp Comparator used to order stored items.
 * @return A new heap, or NULL if allocation fails.
 */
struct heapx_heap *binary_heap_create(heapx_cmp_fn cmp);

/** @} */

#endif
