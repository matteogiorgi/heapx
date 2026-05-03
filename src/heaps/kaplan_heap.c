#include <stdlib.h>

#include "heaps/kaplan_heap.h"
#include "heap_internal.h"

/**
 * @file kaplan_heap.c
 * @brief Kaplan/simple-Fibonacci heap backend for heapx_heap.
 *
 * Simple Fibonacci heap from "Fibonacci Heaps Revisited", exposed in heapx as
 * the Kaplan heap backend.
 *
 * Unlike the classic Fibonacci heap backend, this representation keeps a single
 * heap-ordered tree. Insertions use naive links against the root. extract-min
 * removes the root, performs fair links among children with equal rank, then
 * folds the remaining roots back into one tree with naive links.
 *
 * decrease-key follows the paper's simple-Fibonacci-heap repair: one cut plus
 * cascading rank/state changes, not cascading cuts. remove uses the same
 * structural idea to simulate removal by handle as
 * decrease-key-to-minus-infinity followed by extract-min.
 */

/**
 * @ingroup heap_backends
 * @brief Node stored in the Kaplan heap tree.
 *
 * Children are maintained as a doubly linked list through before/after. The
 * rank is used by fair linking during extract-min consolidation.
 *
 * The after pointer is also used to thread temporary root lists while rebuilding
 * the heap after extract-min.
 */
struct kaplan_heap_node {
    /** Public generational handle associated with this node. */
    struct heapx_handle handle;
    /** Non-zero when handle was requested by the caller. */
    int has_handle;
    /** Caller-owned item pointer stored in this node. */
    void *item;
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
 * Unlike the Fibonacci backend, there is at most one root tree when the heap
 * is in its steady state. extract-min temporarily treats the removed root's
 * children as roots before consolidating them back into one tree.
 */
struct kaplan_heap {
    /** Common heapx_heap base. Must be the first field. */
    struct heapx_heap base;
    /** Root of the heap-ordered tree, or NULL when empty. */
    struct kaplan_heap_node *root;
    /** Number of stored items. */
    size_t size;
    /** Pool used for node wrappers. */
    struct heapx_node_pool nodes;
    /** Reusable consolidation table indexed by rank. */
    struct kaplan_heap_node **rank_table;
    /** Capacity of rank_table. */
    size_t rank_table_capacity;
};

static void kaplan_heap_destroy(struct heapx_heap *base);
static int kaplan_heap_insert(struct heapx_heap *base, void *item);
static int kaplan_heap_insert_handle(
    struct heapx_heap *base,
    void *item,
    struct heapx_handle *out
);
static int kaplan_heap_decrease_key(
    struct heapx_heap *base,
    void *owner
);
static void *kaplan_heap_remove(
    struct heapx_heap *base,
    void *owner
);
static int kaplan_heap_contains(
    const struct heapx_heap *base,
    const void *item
);
static void *kaplan_heap_peek_min(const struct heapx_heap *base);
static void *kaplan_heap_extract_min(struct heapx_heap *base);
static size_t kaplan_heap_size(const struct heapx_heap *base);
static int kaplan_heap_empty(const struct heapx_heap *base);
static int kaplan_heap_check_invariants(const struct heapx_heap *base);

/** @brief Static vtable exposed through the common heapx_heap base. */
static const struct heapx_vtable kaplan_heap_vtable = {
    kaplan_heap_destroy,
    kaplan_heap_insert,
    kaplan_heap_insert_handle,
    kaplan_heap_decrease_key,
    kaplan_heap_remove,
    kaplan_heap_contains,
    kaplan_heap_peek_min,
    kaplan_heap_extract_min,
    kaplan_heap_size,
    kaplan_heap_empty,
    kaplan_heap_check_invariants
};

/** @brief Recover the concrete heap object from the abstract base pointer. */
static struct kaplan_heap *kaplan_heap_from_base(struct heapx_heap *base)
{
    return (struct kaplan_heap *)base;
}

/** @brief Const-preserving variant of kaplan_heap_from_base(). */
static const struct kaplan_heap *kaplan_heap_from_const_base(
    const struct heapx_heap *base
)
{
    return (const struct kaplan_heap *)base;
}

/** @brief Allocate and initialize a Kaplan heap node. */
static struct kaplan_heap_node *kaplan_heap_node_create(
    struct kaplan_heap *heap,
    void *item
)
{
    struct kaplan_heap_node *node;

    node = heapx_node_pool_alloc(&heap->nodes);
    if (node == NULL)
        return NULL;

    node->has_handle = 0;
    node->item = item;
    node->parent = NULL;
    node->child = NULL;
    node->before = NULL;
    node->after = NULL;
    node->rank = 0;
    node->marked = 0;

    return node;
}

/** @brief Return a node wrapper to the heap's node pool. */
static void kaplan_heap_node_destroy(
    struct kaplan_heap *heap,
    struct kaplan_heap_node *node
)
{
    heapx_node_pool_free(&heap->nodes, node);
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
    if (heap->base.cmp(right->item, left->item) < 0) {
        kaplan_heap_add_child(right, left);
        return right;
    }

    kaplan_heap_add_child(left, right);
    return left;
}

/**
 * @brief Link two roots of equal rank and increment the resulting root rank.
 *
 * Fair links are used during extract-min consolidation to keep the resulting
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

/** @brief Ensure the reusable rank table can store capacity slots. */
static int kaplan_heap_reserve_rank_table(
    struct kaplan_heap *heap,
    size_t capacity
)
{
    struct kaplan_heap_node **table;
    size_t i;

    if (capacity <= heap->rank_table_capacity)
        return 0;

    table = realloc(heap->rank_table, capacity * sizeof(*table));
    if (table == NULL)
        return -1;

    heap->rank_table = table;
    for (i = heap->rank_table_capacity; i < capacity; i++)
        heap->rank_table[i] = NULL;

    heap->rank_table_capacity = capacity;
    return 0;
}

/**
 * @brief Consolidate child roots after extract-min.
 *
 * Roots of the same rank are joined with fair links. The remaining roots are
 * then linked naively into a single heap-ordered tree.
 *
 * The reusable rank table is sized from the largest rank currently present
 * among the roots and from the number of roots being consolidated. If
 * allocation fails, the fallback still rebuilds a valid heap-ordered tree.
 */
static struct kaplan_heap_node *kaplan_heap_consolidate(
    struct kaplan_heap *heap,
    struct kaplan_heap_node *roots
)
{
    struct kaplan_heap_node *node;
    struct kaplan_heap_node *root = NULL;
    size_t rank_capacity;
    size_t max_rank = 0;
    size_t max_rank_seen = 0;
    size_t root_count = 0;
    size_t i;

    if (roots == NULL)
        return NULL;

    for (node = roots; node != NULL; node = node->after) {
        root_count++;
        if (node->rank > max_rank)
            max_rank = node->rank;
    }

    if (max_rank > (size_t)-1 - root_count - 1)
        return kaplan_heap_link_roots_naively(heap, roots);

    rank_capacity = max_rank + root_count + 1;
    if (kaplan_heap_reserve_rank_table(heap, rank_capacity) != 0)
        return kaplan_heap_link_roots_naively(heap, roots);

    node = roots;
    while (node != NULL) {
        struct kaplan_heap_node *next = node->after;
        size_t rank;

        node->parent = NULL;
        node->before = NULL;
        node->after = NULL;

        rank = node->rank;
        while (heap->rank_table[rank] != NULL) {
            struct kaplan_heap_node *other = heap->rank_table[rank];

            heap->rank_table[rank] = NULL;
            node = kaplan_heap_fair_link(heap, node, other);
            rank = node->rank;
        }

        heap->rank_table[rank] = node;
        if (rank > max_rank_seen)
            max_rank_seen = rank;

        node = next;
    }

    for (i = 0; i <= max_rank_seen; i++) {
        if (heap->rank_table[i] == NULL)
            continue;

        if (root == NULL)
            root = heap->rank_table[i];
        else
            root = kaplan_heap_link(heap, root, heap->rank_table[i]);

        heap->rank_table[i] = NULL;
    }

    return root;
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

        if (node->item == item)
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

struct heapx_heap *kaplan_heap_create(heapx_cmp_fn cmp)
{
    struct kaplan_heap *heap;

    heap = malloc(sizeof(*heap));
    if (heap == NULL)
        return NULL;

    heapx_heap_init(&heap->base, &kaplan_heap_vtable, cmp);
    heap->root = NULL;
    heap->size = 0;
    heap->rank_table = NULL;
    heap->rank_table_capacity = 0;
    if (
        heapx_node_pool_init(
            &heap->nodes,
            sizeof(struct kaplan_heap_node),
            256
        ) != 0
        ) {
        free(heap);
        return NULL;
    }

    return &heap->base;
}

/** @brief Release all Kaplan-heap-owned node storage. */
static void kaplan_heap_destroy(struct heapx_heap *base)
{
    struct kaplan_heap *heap = kaplan_heap_from_base(base);

    free(heap->rank_table);
    heapx_node_pool_destroy(&heap->nodes);
}

/**
 * @brief Insert an item by linking a singleton node against the root.
 *
 * This is the naive-link insertion step from the simple Fibonacci heap model.
 */
static int kaplan_heap_insert_node(
    struct heapx_heap *base,
    void *item,
    struct heapx_handle *out
)
{
    struct kaplan_heap *heap = kaplan_heap_from_base(base);
    struct kaplan_heap_node *node;

    node = kaplan_heap_node_create(heap, item);
    if (node == NULL)
        return -1;

    node->has_handle = out != NULL;
    if (out != NULL) {
        if (heapx_handle_attach(base, item, node, out) != 0) {
            kaplan_heap_node_destroy(heap, node);
            return -1;
        }
        node->handle = *out;
    }

    if (heap->root == NULL)
        heap->root = node;
    else
        heap->root = kaplan_heap_link(heap, heap->root, node);

    heap->size++;
    return 0;
}

static int kaplan_heap_insert(struct heapx_heap *base, void *item)
{
    return kaplan_heap_insert_node(base, item, NULL);
}

/**
 * @brief Insert an item by linking a singleton node and return its handle.
 */
static int kaplan_heap_insert_handle(
    struct heapx_heap *base,
    void *item,
    struct heapx_handle *out
)
{
    return kaplan_heap_insert_node(base, item, out);
}

/**
 * @brief Repair a decreased node with cascading rank changes and one cut.
 */
static int kaplan_heap_decrease_key(
    struct heapx_heap *base,
    void *owner
)
{
    struct kaplan_heap *heap = kaplan_heap_from_base(base);
    struct kaplan_heap_node *node = owner;

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
 * reuse extract-min.
 */
static void *kaplan_heap_remove(
    struct heapx_heap *base,
    void *owner
)
{
    struct kaplan_heap *heap = kaplan_heap_from_base(base);
    struct kaplan_heap_node *node = owner;
    struct kaplan_heap_node *old_root = heap->root;
    void *item = node->item;

    if (node == heap->root) {
        (void)kaplan_heap_extract_min(base);
        return item;
    }

    kaplan_heap_decrease_ranks(heap, node);
    kaplan_heap_detach_without_rank_change(node);
    kaplan_heap_add_child(node, old_root);
    node->marked = 0;
    heap->root = node;

    (void)kaplan_heap_extract_min(base);
    return item;
}

/**
 * @brief Return whether item is stored by pointer identity.
 */
static int kaplan_heap_contains(
    const struct heapx_heap *base,
    const void *item
)
{
    const struct kaplan_heap *heap = kaplan_heap_from_const_base(base);

    return kaplan_heap_find_node(heap->root, item) != NULL;
}

/** @brief Return the root item without removing it. */
static void *kaplan_heap_peek_min(const struct heapx_heap *base)
{
    const struct kaplan_heap *heap = kaplan_heap_from_const_base(base);

    if (heap->root == NULL)
        return NULL;

    return heap->root->item;
}

/**
 * @brief Remove the root item and rebuild the heap from its children.
 *
 * The root wrapper is freed after its child list has been consolidated into the
 * new heap root. The returned item pointer remains caller-owned.
 */
static void *kaplan_heap_extract_min(struct heapx_heap *base)
{
    struct kaplan_heap *heap = kaplan_heap_from_base(base);
    struct kaplan_heap_node *root = heap->root;
    struct kaplan_heap_node *children;
    void *item;

    if (root == NULL)
        return NULL;

    item = root->item;
    if (root->has_handle)
        heapx_handle_release(base, root->handle);
    children = root->child;
    heap->size--;
    heap->root = kaplan_heap_consolidate(heap, children);

    kaplan_heap_node_destroy(heap, root);
    return item;
}

/** @brief Return the current number of stored items. */
static size_t kaplan_heap_size(const struct heapx_heap *base)
{
    return kaplan_heap_from_const_base(base)->size;
}

/** @brief Return whether the heap contains no items. */
static int kaplan_heap_empty(const struct heapx_heap *base)
{
    return kaplan_heap_size(base) == 0;
}

static int kaplan_heap_count_direct_children(
    const struct kaplan_heap *heap,
    const struct kaplan_heap_node *parent,
    size_t *child_count
)
{
    const struct kaplan_heap_node *child = parent->child;
    size_t count = 0;

    while (child != NULL) {
        if (child->parent != parent)
            return -1;
        if (child->before == NULL && parent->child != child)
            return -1;
        if (child->before != NULL && child->before->after != child)
            return -1;
        if (child->after != NULL && child->after->before != child)
            return -1;

        count++;
        if (count > heap->size)
            return -1;

        child = child->after;
    }

    *child_count = count;
    return 0;
}

static int kaplan_heap_check_node_list(
    const struct kaplan_heap *heap,
    const struct kaplan_heap_node *node,
    const struct kaplan_heap_node *parent,
    size_t *count
)
{
    const struct kaplan_heap_node *current = node;

    while (current != NULL) {
        size_t child_count;
        void *owner;

        if (*count >= heap->size)
            return -1;
        if (current->parent != parent)
            return -1;
        if (current->before != NULL && current->before->after != current)
            return -1;
        if (current->after != NULL && current->after->before != current)
            return -1;
        if (parent != NULL && current->before == NULL && parent->child != current)
            return -1;
        if (current->child != NULL && current->child->before != NULL)
            return -1;
        if (current->marked != 0 && current->marked != 1)
            return -1;
        if (current->rank > heap->size)
            return -1;
        if (kaplan_heap_count_direct_children(heap, current, &child_count) != 0)
            return -1;
        if (current->rank > child_count)
            return -1;
        if (
            parent != NULL &&
            heap->base.cmp(current->item, parent->item) < 0
            )
            return -1;

        if (current->has_handle) {
            if (heapx_handle_resolve(&heap->base, current->handle, &owner) != 0)
                return -1;
            if (owner != current)
                return -1;
        }

        (*count)++;
        if (kaplan_heap_check_node_list(heap, current->child, current, count) != 0)
            return -1;

        current = current->after;
    }

    return 0;
}

static int kaplan_heap_check_invariants(const struct heapx_heap *base)
{
    const struct kaplan_heap *heap = kaplan_heap_from_const_base(base);
    size_t count = 0;
    size_t i;

    for (i = 0; i < heap->rank_table_capacity; i++) {
        if (heap->rank_table[i] != NULL)
            return -1;
    }

    if (heap->size == 0)
        return heap->root == NULL ? 0 : -1;
    if (heap->root == NULL)
        return -1;
    if (
        heap->root->parent != NULL ||
        heap->root->before != NULL ||
        heap->root->after != NULL
        )
        return -1;

    if (kaplan_heap_check_node_list(heap, heap->root, NULL, &count) != 0)
        return -1;

    return count == heap->size ? 0 : -1;
}
