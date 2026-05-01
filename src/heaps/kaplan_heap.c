#include <stdlib.h>

#include "heaps/kaplan_heap.h"
#include "priority_queue_internal.h"

/**
 * @file kaplan_heap.c
 * @brief Kaplan/simple-Fibonacci heap backend for priority_queue.
 *
 * Simple Fibonacci heap from "Fibonacci Heaps Revisited", exposed in hpqlib as
 * the Kaplan heap backend.
 *
 * Unlike the classic Fibonacci heap backend, this representation keeps a single
 * heap-ordered tree. Insertions use naive links against the root. delete-min
 * removes the root, performs fair links among children with equal rank, then
 * folds the remaining roots back into one tree with naive links.
 *
 * decrease-key follows the paper's simple-Fibonacci-heap repair: one cut plus
 * cascading rank/state changes, not cascading cuts. remove uses the same
 * structural idea to simulate delete-by-handle as
 * decrease-key-to-minus-infinity followed by delete-min.
 */

/**
 * @ingroup heap_backends
 * @brief Node stored in the Kaplan heap tree.
 *
 * Children are maintained as a doubly linked list through before/after. The
 * rank is used by fair linking during delete-min consolidation.
 *
 * The after pointer is also used to thread temporary root lists while rebuilding
 * the heap after a pop.
 */
struct kaplan_heap_node {
    /** Common public handle header. Must be the first field. */
    struct priority_queue_handle handle;
    /** Parent node, or NULL for a root candidate. */
    struct kaplan_heap_node *parent;
    /** First child in the child list, or NULL. */
    struct kaplan_heap_node *child;
    /** Previous sibling in a child/root list. */
    struct kaplan_heap_node *before;
    /** Next sibling in a child/root list. */
    struct kaplan_heap_node *after;
    /** Rank used for fair links. */
    size_t rank;
    /** Mark/state bit used by cascading rank changes. */
    int marked;
};

/**
 * @ingroup heap_backends
 * @brief Concrete Kaplan heap object.
 *
 * Unlike the Fibonacci backend, there is at most one root tree when the queue
 * is in its steady state. delete-min temporarily treats the removed root's
 * children as roots before consolidating them back into one tree.
 */
struct kaplan_heap {
    /** Common priority_queue base. Must be the first field. */
    struct priority_queue base;
    /** Root of the heap-ordered tree, or NULL when empty. */
    struct kaplan_heap_node *root;
    /** Number of stored items. */
    size_t size;
};

static void kaplan_heap_destroy(struct priority_queue *queue);
static int kaplan_heap_push(struct priority_queue *queue, void *item);
static struct priority_queue_handle *kaplan_heap_push_handle(
    struct priority_queue *queue,
    void *item
);
static int kaplan_heap_decrease_key(
    struct priority_queue *queue,
    struct priority_queue_handle *handle
);
static void *kaplan_heap_remove(
    struct priority_queue *queue,
    struct priority_queue_handle *handle
);
static int kaplan_heap_contains(
    const struct priority_queue *queue,
    const void *item
);
static void *kaplan_heap_peek(const struct priority_queue *queue);
static void *kaplan_heap_pop(struct priority_queue *queue);
static size_t kaplan_heap_size(const struct priority_queue *queue);
static int kaplan_heap_empty(const struct priority_queue *queue);

/** @brief Static vtable exposed through the common priority_queue base. */
static const struct priority_queue_vtable kaplan_heap_vtable = {
    kaplan_heap_destroy,
    kaplan_heap_push,
    kaplan_heap_push_handle,
    kaplan_heap_decrease_key,
    kaplan_heap_remove,
    kaplan_heap_contains,
    kaplan_heap_peek,
    kaplan_heap_pop,
    kaplan_heap_size,
    kaplan_heap_empty
};

/** @brief Recover the concrete heap object from the abstract base pointer. */
static struct kaplan_heap *kaplan_heap_from_queue(struct priority_queue *queue)
{
    return (struct kaplan_heap *)queue;
}

/** @brief Const-preserving variant of kaplan_heap_from_queue(). */
static const struct kaplan_heap *kaplan_heap_from_const_queue(
    const struct priority_queue *queue
)
{
    return (const struct kaplan_heap *)queue;
}

/** @brief Recover a Kaplan heap node from its public handle pointer. */
static struct kaplan_heap_node *kaplan_heap_node_from_handle(
    struct priority_queue_handle *handle
)
{
    return (struct kaplan_heap_node *)handle;
}

/** @brief Allocate and initialize a Kaplan heap node. */
static struct kaplan_heap_node *kaplan_heap_node_create(void *item)
{
    struct kaplan_heap_node *node;

    node = malloc(sizeof(*node));
    if (node == NULL)
        return NULL;

    priority_queue_handle_init(&node->handle, NULL, item);
    node->parent = NULL;
    node->child = NULL;
    node->before = NULL;
    node->after = NULL;
    node->rank = 0;
    node->marked = 0;

    return node;
}

/**
 * @brief Add child as the first child of parent.
 *
 * Children are prepended because the child list does not need stable order.
 */
static void kaplan_heap_add_child(
    struct kaplan_heap_node *parent,
    struct kaplan_heap_node *child
)
{
    child->parent = parent;
    child->before = NULL;
    child->after = parent->child;

    if (parent->child != NULL)
        parent->child->before = child;

    parent->child = child;
}

/**
 * @brief Detach node from its parent child list or sibling root list.
 *
 * Rank changes are intentionally handled by callers. In particular,
 * decrease-key performs the paper's cascading rank-change loop before cutting
 * the decreased node.
 */
static void kaplan_heap_detach_without_rank_change(
    struct kaplan_heap_node *node
)
{
    if (node->before != NULL)
        node->before->after = node->after;
    else if (node->parent != NULL)
        node->parent->child = node->after;

    if (node->after != NULL)
        node->after->before = node->before;

    node->parent = NULL;
    node->before = NULL;
    node->after = NULL;
}

/**
 * @brief Link two roots and return the root with higher priority.
 *
 * The lower-priority root becomes a child of the higher-priority root. This is
 * the primitive used for both naive and fair links.
 *
 * Ties keep the left node as the root, so equal-priority items are not stable
 * across the whole backend but this local operation is deterministic.
 */
static struct kaplan_heap_node *kaplan_heap_link(
    struct kaplan_heap *heap,
    struct kaplan_heap_node *left,
    struct kaplan_heap_node *right
)
{
    if (heap->base.cmp(right->handle.item, left->handle.item) < 0) {
        kaplan_heap_add_child(right, left);
        return right;
    }

    kaplan_heap_add_child(left, right);
    return left;
}

/**
 * @brief Link two roots of equal rank and increment the resulting root rank.
 *
 * Fair links are used during delete-min consolidation to keep the resulting
 * tree ranks under control.
 */
static struct kaplan_heap_node *kaplan_heap_fair_link(
    struct kaplan_heap *heap,
    struct kaplan_heap_node *left,
    struct kaplan_heap_node *right
)
{
    struct kaplan_heap_node *root;

    root = kaplan_heap_link(heap, left, right);
    root->rank++;

    return root;
}

/**
 * @brief Fold a list of roots into one tree with naive links.
 *
 * This is also the allocation-failure fallback for consolidation.
 * It preserves correctness even when the rank table cannot be allocated, at
 * the cost of weaker structural guarantees for that operation.
 */
static struct kaplan_heap_node *kaplan_heap_link_roots_naively(
    struct kaplan_heap *heap,
    struct kaplan_heap_node *roots
)
{
    struct kaplan_heap_node *root = NULL;

    while (roots != NULL) {
        struct kaplan_heap_node *node = roots;

        roots = roots->after;
        node->parent = NULL;
        node->before = NULL;
        node->after = NULL;

        if (root == NULL)
            root = node;
        else
            root = kaplan_heap_link(heap, root, node);
    }

    return root;
}

/**
 * @brief Consolidate child roots after delete-min.
 *
 * Roots of the same rank are joined with fair links. The remaining roots are
 * then linked naively into a single heap-ordered tree.
 *
 * The by-rank table is sized from the number of items remaining after the root
 * has been removed and from the largest rank currently present among the roots.
 * The second bound keeps consolidation safe after decrease-key rank repairs.
 * If allocation fails, the fallback still rebuilds a valid heap-ordered tree.
 */
static struct kaplan_heap_node *kaplan_heap_consolidate(
    struct kaplan_heap *heap,
    struct kaplan_heap_node *roots
)
{
    struct kaplan_heap_node **by_rank;
    struct kaplan_heap_node *node;
    struct kaplan_heap_node *root = NULL;
    size_t rank_capacity;
    size_t max_rank = 0;
    size_t root_count = 0;
    size_t i;

    if (roots == NULL)
        return NULL;

    for (node = roots; node != NULL; node = node->after) {
        root_count++;
        if (node->rank > max_rank)
            max_rank = node->rank;
    }

    rank_capacity = heap->size + 1;
    if (rank_capacity <= max_rank + root_count)
        rank_capacity = max_rank + root_count + 1;

    by_rank = calloc(rank_capacity, sizeof(*by_rank));
    if (by_rank == NULL)
        return kaplan_heap_link_roots_naively(heap, roots);

    node = roots;
    while (node != NULL) {
        struct kaplan_heap_node *next = node->after;
        size_t rank;

        node->parent = NULL;
        node->before = NULL;
        node->after = NULL;

        rank = node->rank;
        while (by_rank[rank] != NULL) {
            struct kaplan_heap_node *other = by_rank[rank];

            by_rank[rank] = NULL;
            node = kaplan_heap_fair_link(heap, node, other);
            rank = node->rank;
        }

        by_rank[rank] = node;
        if (rank > max_rank)
            max_rank = rank;

        node = next;
    }

    for (i = 0; i <= max_rank; i++) {
        if (by_rank[i] == NULL)
            continue;

        if (root == NULL)
            root = by_rank[i];
        else
            root = kaplan_heap_link(heap, root, by_rank[i]);
    }

    free(by_rank);
    return root;
}

/** @brief Recursively release node and its siblings/children. */
static void kaplan_heap_destroy_nodes(struct kaplan_heap_node *node)
{
    while (node != NULL) {
        struct kaplan_heap_node *next = node->after;

        kaplan_heap_destroy_nodes(node->child);
        free(node);
        node = next;
    }
}

/**
 * @brief Find the first node storing item by pointer identity.
 */
static struct kaplan_heap_node *kaplan_heap_find_node(
    struct kaplan_heap_node *node,
    const void *item
)
{
    while (node != NULL) {
        struct kaplan_heap_node *found;

        if (node->handle.item == item)
            return node;

        found = kaplan_heap_find_node(node->child, item);
        if (found != NULL)
            return found;

        node = node->after;
    }

    return NULL;
}

/**
 * @brief Apply the paper's cascading rank/state changes before a cut.
 *
 * Starting at the parent of node, walk toward the root. Each visited node loses
 * one rank if possible and toggles its mark bit. The loop stops when a node
 * becomes marked.
 */
static void kaplan_heap_decrease_ranks(
    struct kaplan_heap *heap,
    struct kaplan_heap_node *node
)
{
    struct kaplan_heap_node *current = node;

    if (heap->root != NULL)
        heap->root->marked = 0;

    do {
        current = current->parent;
        if (current->rank > 0)
            current->rank--;
        current->marked = !current->marked;
    } while (!current->marked);
}

struct priority_queue *kaplan_heap_create(priority_queue_cmp_fn cmp)
{
    struct kaplan_heap *heap;

    heap = malloc(sizeof(*heap));
    if (heap == NULL)
        return NULL;

    priority_queue_init(&heap->base, &kaplan_heap_vtable, cmp);
    heap->root = NULL;
    heap->size = 0;

    return &heap->base;
}

/** @brief Release all Kaplan-heap-owned nodes and the heap object. */
static void kaplan_heap_destroy(struct priority_queue *queue)
{
    struct kaplan_heap *heap = kaplan_heap_from_queue(queue);

    kaplan_heap_destroy_nodes(heap->root);
    free(heap);
}

/**
 * @brief Insert an item by linking a singleton node against the root.
 *
 * This is the naive-link insertion step from the simple Fibonacci heap model.
 */
static int kaplan_heap_push(struct priority_queue *queue, void *item)
{
    return kaplan_heap_push_handle(queue, item) == NULL ? -1 : 0;
}

/**
 * @brief Insert an item by linking a singleton node and return its handle.
 */
static struct priority_queue_handle *kaplan_heap_push_handle(
    struct priority_queue *queue,
    void *item
)
{
    struct kaplan_heap *heap = kaplan_heap_from_queue(queue);
    struct kaplan_heap_node *node;

    node = kaplan_heap_node_create(item);
    if (node == NULL)
        return NULL;

    node->handle.queue = queue;
    if (heap->root == NULL)
        heap->root = node;
    else
        heap->root = kaplan_heap_link(heap, heap->root, node);

    heap->size++;
    return &node->handle;
}

/**
 * @brief Repair a decreased node with cascading rank changes and one cut.
 */
static int kaplan_heap_decrease_key(
    struct priority_queue *queue,
    struct priority_queue_handle *handle
)
{
    struct kaplan_heap *heap = kaplan_heap_from_queue(queue);
    struct kaplan_heap_node *node = kaplan_heap_node_from_handle(handle);

    if (node != heap->root) {
        kaplan_heap_decrease_ranks(heap, node);
        kaplan_heap_detach_without_rank_change(node);
        heap->root = kaplan_heap_link(heap, heap->root, node);
    }

    return 0;
}

/**
 * @brief Remove one item by the paper's delete operation.
 *
 * The public API cannot express a backend-independent minus-infinity key, so
 * this function simulates decrease-key-to-minus-infinity structurally: repair
 * ranks, cut the target, force the old root to become one of its children, then
 * reuse delete-min.
 */
static void *kaplan_heap_remove(
    struct priority_queue *queue,
    struct priority_queue_handle *handle
)
{
    struct kaplan_heap *heap = kaplan_heap_from_queue(queue);
    struct kaplan_heap_node *node = kaplan_heap_node_from_handle(handle);
    struct kaplan_heap_node *old_root = heap->root;
    void *item = handle->item;

    if (node == heap->root) {
        (void)kaplan_heap_pop(queue);
        return item;
    }

    kaplan_heap_decrease_ranks(heap, node);
    kaplan_heap_detach_without_rank_change(node);
    kaplan_heap_add_child(node, old_root);
    node->marked = 0;
    heap->root = node;

    (void)kaplan_heap_pop(queue);
    return item;
}

/**
 * @brief Return whether item is stored by pointer identity.
 */
static int kaplan_heap_contains(
    const struct priority_queue *queue,
    const void *item
)
{
    const struct kaplan_heap *heap = kaplan_heap_from_const_queue(queue);

    return kaplan_heap_find_node(heap->root, item) != NULL;
}

/** @brief Return the root item without removing it. */
static void *kaplan_heap_peek(const struct priority_queue *queue)
{
    const struct kaplan_heap *heap = kaplan_heap_from_const_queue(queue);

    if (heap->root == NULL)
        return NULL;

    return heap->root->handle.item;
}

/**
 * @brief Remove the root item and rebuild the heap from its children.
 *
 * The root wrapper is freed after its child list has been consolidated into the
 * new heap root. The returned item pointer remains caller-owned.
 */
static void *kaplan_heap_pop(struct priority_queue *queue)
{
    struct kaplan_heap *heap = kaplan_heap_from_queue(queue);
    struct kaplan_heap_node *root = heap->root;
    struct kaplan_heap_node *children;
    void *item;

    if (root == NULL)
        return NULL;

    item = root->handle.item;
    root->handle.queue = NULL;
    children = root->child;
    heap->size--;
    heap->root = kaplan_heap_consolidate(heap, children);

    free(root);
    return item;
}

/** @brief Return the current number of stored items. */
static size_t kaplan_heap_size(const struct priority_queue *queue)
{
    return kaplan_heap_from_const_queue(queue)->size;
}

/** @brief Return whether the heap contains no items. */
static int kaplan_heap_empty(const struct priority_queue *queue)
{
    return kaplan_heap_size(queue) == 0;
}
