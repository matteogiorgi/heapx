#include <stdlib.h>

#include "heaps/fibonacci_heap.h"
#include "heap_internal.h"

/**
 * @file fibonacci_heap.c
 * @brief Fibonacci heap backend for the abstract heapx_heap API.
 *
 * This backend implements the operations currently exposed by heapx:
 * create/destroy/insert/decrease-key/remove/contains/peek_min/extract_min/size/empty. Its
 * handle-based targeted operations follow the paper's assumption that the item
 * position is known: decrease-key uses cut plus cascading cuts, and remove
 * promotes the deleted node's children before applying the same cascading-cut
 * rule.
 */

/**
 * @ingroup heap_backends
 * @brief Node stored in a circular-list Fibonacci heap.
 *
 * Root lists and child lists are circular doubly linked lists. The item pointer
 * is caller-owned and is compared through the heap's common base comparator.
 *
 * When the node is alone in a list, left and right both point back to the node.
 * A node with parent == NULL is in the root list.
 */
struct fibonacci_heap_node {
    /** Public generational handle associated with this node. */
    struct heapx_handle handle;
    /** Non-zero when handle was requested by the caller. */
    int has_handle;
    /** Caller-owned item pointer stored in this node. */
    void *item;
    /** Parent node, or NULL when the node is a root. */
    struct fibonacci_heap_node *parent;
    /** One child in the circular child list, or NULL. */
    struct fibonacci_heap_node *child;
    /** Previous node in the circular sibling list. */
    struct fibonacci_heap_node *left;
    /** Next node in the circular sibling list. */
    struct fibonacci_heap_node *right;
    /** Number of children. */
    size_t degree;
    /** Mark bit used by cascading cuts after child losses. */
    int marked;
};

/**
 * @ingroup heap_backends
 * @brief Concrete Fibonacci heap object.
 *
 * The minimum pointer is either NULL for an empty heap or points at one root in
 * the circular root list. The root list may contain several heap-ordered trees.
 */
struct fibonacci_heap {
    /** Common heapx_heap base. Must be the first field. */
    struct heapx_heap base;
    /** Current minimum root, or NULL when empty. */
    struct fibonacci_heap_node *minimum;
    /** Number of stored items. */
    size_t size;
    /** Pool used for node wrappers. */
    struct heapx_node_pool nodes;
    /** Reusable consolidation table indexed by degree. */
    struct fibonacci_heap_node **degree_table;
    /** Capacity of degree_table. */
    size_t degree_table_capacity;
};

static void fibonacci_heap_destroy(struct heapx_heap *base);
static int fibonacci_heap_insert(struct heapx_heap *base, void *item);
static int fibonacci_heap_insert_handle(
    struct heapx_heap *base,
    void *item,
    struct heapx_handle *out
);
static int fibonacci_heap_decrease_key(
    struct heapx_heap *base,
    void *owner
);
static void *fibonacci_heap_remove(
    struct heapx_heap *base,
    void *owner
);
static int fibonacci_heap_contains(
    const struct heapx_heap *base,
    const void *item
);
static void *fibonacci_heap_peek_min(const struct heapx_heap *base);
static void *fibonacci_heap_extract_min(struct heapx_heap *base);
static size_t fibonacci_heap_size(const struct heapx_heap *base);
static int fibonacci_heap_empty(const struct heapx_heap *base);
static int fibonacci_heap_check_invariants(const struct heapx_heap *base);

/** @brief Static vtable exposed through the common heapx_heap base. */
static const struct heapx_vtable fibonacci_heap_vtable = {
    fibonacci_heap_destroy,
    fibonacci_heap_insert,
    fibonacci_heap_insert_handle,
    fibonacci_heap_decrease_key,
    fibonacci_heap_remove,
    fibonacci_heap_contains,
    fibonacci_heap_peek_min,
    fibonacci_heap_extract_min,
    fibonacci_heap_size,
    fibonacci_heap_empty,
    fibonacci_heap_check_invariants
};

/** @brief Recover the concrete heap object from the abstract base pointer. */
static struct fibonacci_heap *fibonacci_heap_from_base(
    struct heapx_heap *base
)
{
    return (struct fibonacci_heap *)base;
}

/** @brief Const-preserving variant of fibonacci_heap_from_base(). */
static const struct fibonacci_heap *fibonacci_heap_from_const_base(
    const struct heapx_heap *base
)
{
    return (const struct fibonacci_heap *)base;
}

/** @brief Allocate and initialize a Fibonacci heap node. */
static struct fibonacci_heap_node *fibonacci_heap_node_create(
    struct fibonacci_heap *heap,
    void *item
)
{
    struct fibonacci_heap_node *node;

    node = heapx_node_pool_alloc(&heap->nodes);
    if (node == NULL)
        return NULL;

    node->has_handle = 0;
    node->item = item;
    node->parent = NULL;
    node->child = NULL;
    node->left = node;
    node->right = node;
    node->degree = 0;
    node->marked = 0;

    return node;
}

/** @brief Return a node wrapper to the heap's node pool. */
static void fibonacci_heap_node_destroy(
    struct fibonacci_heap *heap,
    struct fibonacci_heap_node *node
)
{
    heapx_node_pool_free(&heap->nodes, node);
}

/** @brief Insert node into a circular list immediately after position. */
static void fibonacci_heap_list_insert_after(
    struct fibonacci_heap_node *position,
    struct fibonacci_heap_node *node
)
{
    node->left = position;
    node->right = position->right;
    position->right->left = node;
    position->right = node;
}

/**
 * @brief Detach node from its circular sibling list.
 *
 * After removal, node becomes a singleton circular list. This makes the helper
 * safe to use before re-inserting the node elsewhere.
 */
static void fibonacci_heap_list_remove(struct fibonacci_heap_node *node)
{
    node->left->right = node->right;
    node->right->left = node->left;
    node->left = node;
    node->right = node;
}

/**
 * @brief Add node to the root list and update the minimum pointer.
 *
 * The node is marked unowned by any parent. This is used both for fresh
 * insertions and when extract-min promotes children of the removed root.
 */
static void fibonacci_heap_add_root(
    struct fibonacci_heap *heap,
    struct fibonacci_heap_node *node
)
{
    node->parent = NULL;
    node->marked = 0;

    if (heap->minimum == NULL) {
        node->left = node;
        node->right = node;
        heap->minimum = node;
        return;
    }

    fibonacci_heap_list_insert_after(heap->minimum, node);
    if (heap->base.cmp(node->item, heap->minimum->item) < 0)
        heap->minimum = node;
}

/**
 * @brief Add child to parent's child list and increment parent's degree.
 *
 * The helper assumes child is already detached from its previous sibling list.
 */
static void fibonacci_heap_add_child(
    struct fibonacci_heap_node *parent,
    struct fibonacci_heap_node *child
)
{
    child->parent = parent;
    child->marked = 0;

    if (parent->child == NULL) {
        child->left = child;
        child->right = child;
        parent->child = child;
    } else {
        fibonacci_heap_list_insert_after(parent->child, child);
    }

    parent->degree++;
}

/**
 * @brief Remove child from its parent child list.
 *
 * This only updates local parent/child/list links. Marking and cascading cuts
 * are handled by the callers that conceptually perform a cut.
 */
static void fibonacci_heap_remove_child(
    struct fibonacci_heap_node *parent,
    struct fibonacci_heap_node *child
)
{
    if (child->right == child)
        parent->child = NULL;
    else if (parent->child == child)
        parent->child = child->right;

    fibonacci_heap_list_remove(child);
    parent->degree--;
    child->parent = NULL;
}

/**
 * @brief Cut node from parent and add it to the root list.
 */
static void fibonacci_heap_cut(
    struct fibonacci_heap *heap,
    struct fibonacci_heap_node *node,
    struct fibonacci_heap_node *parent
)
{
    fibonacci_heap_remove_child(parent, node);
    fibonacci_heap_add_root(heap, node);
}

/**
 * @brief Apply the paper's cascading-cut rule after a child loss.
 *
 * A nonroot node that loses its first child is marked. If it later loses
 * another child, it is cut from its own parent and the rule continues upward.
 */
static void fibonacci_heap_cascading_cut(
    struct fibonacci_heap *heap,
    struct fibonacci_heap_node *node
)
{
    struct fibonacci_heap_node *parent = node->parent;

    if (parent == NULL)
        return;

    if (!node->marked) {
        node->marked = 1;
        return;
    }

    fibonacci_heap_cut(heap, node, parent);
    fibonacci_heap_cascading_cut(heap, parent);
}

/** @brief Link child under parent during consolidation. */
static void fibonacci_heap_link(
    struct fibonacci_heap_node *child,
    struct fibonacci_heap_node *parent
)
{
    fibonacci_heap_add_child(parent, child);
}

/** @brief Count nodes in a circular list. */
static size_t fibonacci_heap_list_count(struct fibonacci_heap_node *node)
{
    struct fibonacci_heap_node *current;
    size_t count = 0;

    if (node == NULL)
        return 0;

    current = node;
    do {
        count++;
        current = current->right;
    } while (current != node);

    return count;
}

/** @brief Rescan the root list to recover a valid minimum pointer. */
static void fibonacci_heap_update_minimum(struct fibonacci_heap *heap)
{
    struct fibonacci_heap_node *start;
    struct fibonacci_heap_node *current;

    if (heap->minimum == NULL)
        return;

    start = heap->minimum;
    current = start->right;
    while (current != start) {
        if (
            heap->base.cmp(
                current->item,
                heap->minimum->item
            ) < 0
            )
            heap->minimum = current;
        current = current->right;
    }
}

/** @brief Ensure the reusable consolidation table can store capacity slots. */
static int fibonacci_heap_reserve_degree_table(
    struct fibonacci_heap *heap,
    size_t capacity
)
{
    struct fibonacci_heap_node **table;
    size_t bytes;
    size_t i;

    if (capacity <= heap->degree_table_capacity)
        return 0;

    if (heapx_size_mul(capacity, sizeof(*table), &bytes) != 0)
        return -1;

    table = realloc(heap->degree_table, bytes);
    if (table == NULL)
        return -1;

    heap->degree_table = table;
    for (i = heap->degree_table_capacity; i < capacity; i++)
        heap->degree_table[i] = NULL;

    heap->degree_table_capacity = capacity;
    return 0;
}

/**
 * @brief Consolidate roots so no two roots have the same degree.
 *
 * extract-min promotes the removed minimum's children to the root list. This
 * function links roots of equal degree until at most one root of each degree
 * remains, then rebuilds the root list and minimum pointer.
 *
 * If temporary allocation fails, the heap remains valid by falling back to a
 * linear minimum rescan without consolidation.
 *
 * The temporary roots array snapshots the original circular root list before
 * any links are performed. That keeps traversal independent from the mutations
 * performed while consolidating. The degree table itself is reused across
 * extract-min calls and is sized by the degrees that can actually occur in this
 * consolidation, not by the total heap size.
 */
static void fibonacci_heap_consolidate(struct fibonacci_heap *heap)
{
    struct fibonacci_heap_node **roots;
    struct fibonacci_heap_node *current;
    size_t root_count;
    size_t root_bytes;
    size_t degree_capacity;
    size_t max_degree = 0;
    size_t max_degree_seen = 0;
    size_t i;

    root_count = fibonacci_heap_list_count(heap->minimum);
    if (root_count == 0)
        return;

    if (heapx_size_mul(root_count, sizeof(*roots), &root_bytes) != 0) {
        fibonacci_heap_update_minimum(heap);
        return;
    }

    roots = malloc(root_bytes);
    if (roots == NULL) {
        fibonacci_heap_update_minimum(heap);
        return;
    }

    current = heap->minimum;
    for (i = 0; i < root_count; i++) {
        roots[i] = current;
        if (current->degree > max_degree)
            max_degree = current->degree;
        current = current->right;
    }

    if (max_degree > (size_t)-1 - root_count - 1) {
        free(roots);
        fibonacci_heap_update_minimum(heap);
        return;
    }

    degree_capacity = max_degree + root_count + 1;
    if (fibonacci_heap_reserve_degree_table(heap, degree_capacity) != 0) {
        free(roots);
        fibonacci_heap_update_minimum(heap);
        return;
    }

    for (i = 0; i < root_count; i++) {
        roots[i]->left = roots[i];
        roots[i]->right = roots[i];
    }
    heap->minimum = NULL;

    for (i = 0; i < root_count; i++) {
        struct fibonacci_heap_node *node = roots[i];
        size_t degree = node->degree;

        while (heap->degree_table[degree] != NULL) {
            struct fibonacci_heap_node *other = heap->degree_table[degree];

            if (heap->base.cmp(other->item, node->item) < 0) {
                struct fibonacci_heap_node *tmp = node;
                node = other;
                other = tmp;
            }

            heap->degree_table[degree] = NULL;
            fibonacci_heap_link(other, node);
            degree = node->degree;
        }

        heap->degree_table[degree] = node;
        if (degree > max_degree_seen)
            max_degree_seen = degree;
    }

    for (i = 0; i <= max_degree_seen; i++) {
        if (heap->degree_table[i] != NULL)
            fibonacci_heap_add_root(heap, heap->degree_table[i]);
        heap->degree_table[i] = NULL;
    }

    free(roots);
}

/**
 * @brief Promote all children of node to the root list.
 */
static void fibonacci_heap_promote_children(
    struct fibonacci_heap *heap,
    struct fibonacci_heap_node *node
)
{
    while (node->degree > 0) {
        struct fibonacci_heap_node *child = node->child;

        fibonacci_heap_remove_child(node, child);
        fibonacci_heap_add_root(heap, child);
    }
}

/**
 * @brief Find the first node storing item by pointer identity.
 */
static struct fibonacci_heap_node *fibonacci_heap_find_node(
    struct fibonacci_heap_node *node,
    const void *item
)
{
    struct fibonacci_heap_node *current;
    size_t count;
    size_t i;

    if (node == NULL)
        return NULL;

    count = fibonacci_heap_list_count(node);
    current = node;
    for (i = 0; i < count; i++) {
        struct fibonacci_heap_node *found;

        if (current->item == item)
            return current;

        found = fibonacci_heap_find_node(current->child, item);
        if (found != NULL)
            return found;

        current = current->right;
    }

    return NULL;
}

struct heapx_heap *fibonacci_heap_create(heapx_cmp_fn cmp)
{
    struct fibonacci_heap *heap;

    heap = malloc(sizeof(*heap));
    if (heap == NULL)
        return NULL;

    heapx_heap_init(&heap->base, &fibonacci_heap_vtable, cmp);
    heap->minimum = NULL;
    heap->size = 0;
    heap->degree_table = NULL;
    heap->degree_table_capacity = 0;
    if (
        heapx_node_pool_init(
            &heap->nodes,
            sizeof(struct fibonacci_heap_node),
            256
        ) != 0
        ) {
        free(heap);
        return NULL;
    }

    return &heap->base;
}

/** @brief Release all Fibonacci-heap-owned node storage. */
static void fibonacci_heap_destroy(struct heapx_heap *base)
{
    struct fibonacci_heap *heap = fibonacci_heap_from_base(base);

    free(heap->degree_table);
    heapx_node_pool_destroy(&heap->nodes);
}

/**
 * @brief Insert an item as a new singleton root.
 *
 * Fibonacci heap insertion does not consolidate. It just links the new node
 * into the root list and updates the minimum pointer if necessary.
 */
static int fibonacci_heap_insert_node(
    struct heapx_heap *base,
    void *item,
    struct heapx_handle *out
)
{
    struct fibonacci_heap *heap = fibonacci_heap_from_base(base);
    struct fibonacci_heap_node *node;

    node = fibonacci_heap_node_create(heap, item);
    if (node == NULL)
        return -1;

    node->has_handle = out != NULL;
    if (out != NULL) {
        if (heapx_handle_attach(base, item, node, out) != 0) {
            fibonacci_heap_node_destroy(heap, node);
            return -1;
        }
        node->handle = *out;
    }

    fibonacci_heap_add_root(heap, node);
    heap->size++;

    return 0;
}

/** @brief Insert an item without creating a public handle. */
static int fibonacci_heap_insert(struct heapx_heap *base, void *item)
{
    return fibonacci_heap_insert_node(base, item, NULL);
}

/**
 * @brief Insert an item as a new singleton root and return its handle.
 */
static int fibonacci_heap_insert_handle(
    struct heapx_heap *base,
    void *item,
    struct heapx_handle *out
)
{
    return fibonacci_heap_insert_node(base, item, out);
}

/**
 * @brief Cut a decreased node to the root list when it violates parent order.
 *
 * This mirrors the paper's decrease-key repair after locating the node: cut the
 * violating node, then cascade through ancestors that have already lost a child.
 */
static int fibonacci_heap_decrease_key(
    struct heapx_heap *base,
    void *owner
)
{
    struct fibonacci_heap *heap = fibonacci_heap_from_base(base);
    struct fibonacci_heap_node *node = owner;
    struct fibonacci_heap_node *parent;

    parent = node->parent;
    if (
        parent != NULL &&
        heap->base.cmp(node->item, parent->item) < 0
        ) {
        fibonacci_heap_cut(heap, node, parent);
        fibonacci_heap_cascading_cut(heap, parent);
    }

    if (heap->base.cmp(node->item, heap->minimum->item) < 0)
        heap->minimum = node;

    return 0;
}

/**
 * @brief Remove one item by the paper's delete operation.
 *
 * If the node is the current minimum, removal is extract-min. Otherwise its
 * children become roots, the node is cut from its parent or root list, and the
 * cascading-cut rule is applied to the parent that lost a child.
 */
static void *fibonacci_heap_remove(
    struct heapx_heap *base,
    void *owner
)
{
    struct fibonacci_heap *heap = fibonacci_heap_from_base(base);
    struct fibonacci_heap_node *node = owner;
    struct fibonacci_heap_node *parent = node->parent;
    void *item = node->item;

    if (node == heap->minimum) {
        (void)fibonacci_heap_extract_min(base);
        return item;
    }

    if (node->has_handle)
        heapx_handle_release(base, node->handle);

    fibonacci_heap_promote_children(heap, node);

    if (parent != NULL) {
        fibonacci_heap_remove_child(parent, node);
        fibonacci_heap_cascading_cut(heap, parent);
    } else {
        fibonacci_heap_list_remove(node);
    }

    heap->size--;
    fibonacci_heap_node_destroy(heap, node);
    return item;
}

/**
 * @brief Return whether item is stored by pointer identity.
 */
static int fibonacci_heap_contains(
    const struct heapx_heap *base,
    const void *item
)
{
    const struct fibonacci_heap *heap =
        fibonacci_heap_from_const_base(base);

    return fibonacci_heap_find_node(heap->minimum, item) != NULL;
}

/** @brief Return the current minimum item without removing it. */
static void *fibonacci_heap_peek_min(const struct heapx_heap *base)
{
    const struct fibonacci_heap *heap =
        fibonacci_heap_from_const_base(base);

    if (heap->minimum == NULL)
        return NULL;

    return heap->minimum->item;
}

/**
 * @brief Remove the current minimum item.
 *
 * The removed root's children are promoted to roots, the old root is freed, and
 * the remaining roots are consolidated by degree.
 *
 * The returned item pointer is the caller-owned pointer stored in the removed
 * node. Only the node wrapper is freed.
 */
static void *fibonacci_heap_extract_min(struct heapx_heap *base)
{
    struct fibonacci_heap *heap = fibonacci_heap_from_base(base);
    struct fibonacci_heap_node *minimum = heap->minimum;
    void *item;
    size_t child_count;
    size_t i;

    if (minimum == NULL)
        return NULL;

    item = minimum->item;

    child_count = minimum->degree;
    for (i = 0; i < child_count; i++) {
        struct fibonacci_heap_node *child = minimum->child;

        fibonacci_heap_remove_child(minimum, child);
        fibonacci_heap_add_root(heap, child);
    }

    if (minimum->right == minimum) {
        heap->minimum = NULL;
    } else {
        heap->minimum = minimum->right;
        fibonacci_heap_list_remove(minimum);
    }

    heap->size--;
    if (heap->minimum != NULL)
        fibonacci_heap_consolidate(heap);

    if (minimum->has_handle)
        heapx_handle_release(base, minimum->handle);
    fibonacci_heap_node_destroy(heap, minimum);
    return item;
}

/** @brief Return the current number of stored items. */
static size_t fibonacci_heap_size(const struct heapx_heap *base)
{
    return fibonacci_heap_from_const_base(base)->size;
}

/** @brief Return whether the heap contains no items. */
static int fibonacci_heap_empty(const struct heapx_heap *base)
{
    return fibonacci_heap_size(base) == 0;
}

/** @brief Recursively validate one circular list and its child subtrees. */
static int fibonacci_heap_check_node_list(
    const struct fibonacci_heap *heap,
    const struct fibonacci_heap_node *node,
    const struct fibonacci_heap_node *parent,
    size_t *count
)
{
    const struct fibonacci_heap_node *current;
    size_t list_count;
    size_t i;

    if (node == NULL)
        return 0;

    list_count = fibonacci_heap_list_count((struct fibonacci_heap_node *)node);
    current = node;
    for (i = 0; i < list_count; i++) {
        void *owner;
        size_t subtree_count = 0;
        size_t child_count;

        if (current == NULL)
            return -1;
        if (current->left == NULL || current->right == NULL)
            return -1;
        if (current->left->right != current || current->right->left != current)
            return -1;
        if (current->parent != parent)
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

        child_count = fibonacci_heap_list_count(current->child);
        if (
            fibonacci_heap_check_node_list(
                heap,
                current->child,
                current,
                &subtree_count
            ) != 0
            )
            return -1;
        if (child_count != current->degree)
            return -1;

        *count += subtree_count + 1;
        current = current->right;
    }

    return 0;
}

/** @brief Return 0 when Fibonacci heap roots, subtrees, and handles are valid. */
static int fibonacci_heap_check_invariants(const struct heapx_heap *base)
{
    const struct fibonacci_heap *heap = fibonacci_heap_from_const_base(base);
    const struct fibonacci_heap_node *current;
    size_t count = 0;
    size_t root_count;
    size_t i;

    if (heap->size == 0)
        return heap->minimum == NULL ? 0 : -1;
    if (heap->minimum == NULL)
        return -1;

    root_count = fibonacci_heap_list_count(heap->minimum);
    current = heap->minimum;
    for (i = 0; i < root_count; i++) {
        if (current->parent != NULL)
            return -1;
        if (heap->base.cmp(current->item, heap->minimum->item) < 0)
            return -1;
        current = current->right;
    }

    if (fibonacci_heap_check_node_list(heap, heap->minimum, NULL, &count) != 0)
        return -1;

    return count == heap->size ? 0 : -1;
}
