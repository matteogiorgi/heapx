#include <stdlib.h>

#include "heaps/fibonacci_heap.h"
#include "priority_queue_internal.h"

/**
 * @file fibonacci_heap.c
 * @brief Fibonacci heap backend for the abstract priority_queue API.
 *
 * This backend implements the operations currently exposed by hpqlib:
 * create/destroy/push/decrease-key/remove/contains/peek/pop/size/empty. Its
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
    /** Common public handle header. Must be the first field. */
    struct priority_queue_handle handle;
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
    /** Common priority_queue base. Must be the first field. */
    struct priority_queue base;
    /** Current minimum root, or NULL when empty. */
    struct fibonacci_heap_node *minimum;
    /** Number of stored items. */
    size_t size;
};

static void fibonacci_heap_destroy(struct priority_queue *queue);
static int fibonacci_heap_push(struct priority_queue *queue, void *item);
static struct priority_queue_handle *fibonacci_heap_push_handle(
    struct priority_queue *queue,
    void *item
);
static int fibonacci_heap_decrease_key(
    struct priority_queue *queue,
    struct priority_queue_handle *handle
);
static void *fibonacci_heap_remove(
    struct priority_queue *queue,
    struct priority_queue_handle *handle
);
static int fibonacci_heap_contains(
    const struct priority_queue *queue,
    const void *item
);
static void *fibonacci_heap_peek(const struct priority_queue *queue);
static void *fibonacci_heap_pop(struct priority_queue *queue);
static size_t fibonacci_heap_size(const struct priority_queue *queue);
static int fibonacci_heap_empty(const struct priority_queue *queue);

/** @brief Static vtable exposed through the common priority_queue base. */
static const struct priority_queue_vtable fibonacci_heap_vtable = {
    fibonacci_heap_destroy,
    fibonacci_heap_push,
    fibonacci_heap_push_handle,
    fibonacci_heap_decrease_key,
    fibonacci_heap_remove,
    fibonacci_heap_contains,
    fibonacci_heap_peek,
    fibonacci_heap_pop,
    fibonacci_heap_size,
    fibonacci_heap_empty
};

/** @brief Recover the concrete heap object from the abstract base pointer. */
static struct fibonacci_heap *fibonacci_heap_from_queue(
    struct priority_queue *queue
)
{
    return (struct fibonacci_heap *)queue;
}

/** @brief Const-preserving variant of fibonacci_heap_from_queue(). */
static const struct fibonacci_heap *fibonacci_heap_from_const_queue(
    const struct priority_queue *queue
)
{
    return (const struct fibonacci_heap *)queue;
}

/** @brief Recover a Fibonacci heap node from its public handle pointer. */
static struct fibonacci_heap_node *fibonacci_heap_node_from_handle(
    struct priority_queue_handle *handle
)
{
    return (struct fibonacci_heap_node *)handle;
}

/** @brief Allocate and initialize a Fibonacci heap node. */
static struct fibonacci_heap_node *fibonacci_heap_node_create(void *item)
{
    struct fibonacci_heap_node *node;

    node = malloc(sizeof(*node));
    if (node == NULL)
        return NULL;

    priority_queue_handle_init(&node->handle, NULL, item);
    node->parent = NULL;
    node->child = NULL;
    node->left = node;
    node->right = node;
    node->degree = 0;
    node->marked = 0;

    return node;
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
 * insertions and when delete-min promotes children of the removed root.
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
    if (heap->base.cmp(node->handle.item, heap->minimum->handle.item) < 0)
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
                current->handle.item,
                heap->minimum->handle.item
            ) < 0
            )
            heap->minimum = current;
        current = current->right;
    }
}

/**
 * @brief Consolidate roots so no two roots have the same degree.
 *
 * delete-min promotes the removed minimum's children to the root list. This
 * function links roots of equal degree until at most one root of each degree
 * remains, then rebuilds the root list and minimum pointer.
 *
 * If temporary allocation fails, the heap remains valid by falling back to a
 * linear minimum rescan without consolidation.
 *
 * The temporary roots array snapshots the original circular root list before
 * any links are performed. That keeps traversal independent from the mutations
 * performed while consolidating.
 */
static void fibonacci_heap_consolidate(struct fibonacci_heap *heap)
{
    struct fibonacci_heap_node **roots;
    struct fibonacci_heap_node **by_degree;
    struct fibonacci_heap_node *current;
    size_t root_count;
    size_t degree_capacity;
    size_t i;

    root_count = fibonacci_heap_list_count(heap->minimum);
    if (root_count == 0)
        return;

    roots = malloc(root_count * sizeof(*roots));
    if (roots == NULL) {
        fibonacci_heap_update_minimum(heap);
        return;
    }

    degree_capacity = heap->size + 1;
    by_degree = calloc(degree_capacity, sizeof(*by_degree));
    if (by_degree == NULL) {
        free(roots);
        fibonacci_heap_update_minimum(heap);
        return;
    }

    current = heap->minimum;
    for (i = 0; i < root_count; i++) {
        roots[i] = current;
        current = current->right;
    }

    for (i = 0; i < root_count; i++) {
        roots[i]->left = roots[i];
        roots[i]->right = roots[i];
    }
    heap->minimum = NULL;

    for (i = 0; i < root_count; i++) {
        struct fibonacci_heap_node *node = roots[i];
        size_t degree = node->degree;

        while (by_degree[degree] != NULL) {
            struct fibonacci_heap_node *other = by_degree[degree];

            if (heap->base.cmp(other->handle.item, node->handle.item) < 0) {
                struct fibonacci_heap_node *tmp = node;
                node = other;
                other = tmp;
            }

            by_degree[degree] = NULL;
            fibonacci_heap_link(other, node);
            degree = node->degree;
        }

        by_degree[degree] = node;
    }

    for (i = 0; i < degree_capacity; i++) {
        if (by_degree[i] != NULL)
            fibonacci_heap_add_root(heap, by_degree[i]);
    }

    free(by_degree);
    free(roots);
}

/**
 * @brief Recursively release all nodes reachable from a circular list.
 *
 * The function counts the list before freeing nodes so traversal does not rely
 * on pointers after their owning node has been released.
 */
static void fibonacci_heap_destroy_nodes(struct fibonacci_heap_node *node)
{
    struct fibonacci_heap_node *current;
    struct fibonacci_heap_node *next;
    size_t count;
    size_t i;

    if (node == NULL)
        return;

    count = fibonacci_heap_list_count(node);
    current = node;
    for (i = 0; i < count; i++) {
        next = current->right;
        fibonacci_heap_destroy_nodes(current->child);
        free(current);
        current = next;
    }
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

        if (current->handle.item == item)
            return current;

        found = fibonacci_heap_find_node(current->child, item);
        if (found != NULL)
            return found;

        current = current->right;
    }

    return NULL;
}

struct priority_queue *fibonacci_heap_create(priority_queue_cmp_fn cmp)
{
    struct fibonacci_heap *heap;

    heap = malloc(sizeof(*heap));
    if (heap == NULL)
        return NULL;

    priority_queue_init(&heap->base, &fibonacci_heap_vtable, cmp);
    heap->minimum = NULL;
    heap->size = 0;

    return &heap->base;
}

/** @brief Release all Fibonacci-heap-owned nodes and the heap object. */
static void fibonacci_heap_destroy(struct priority_queue *queue)
{
    struct fibonacci_heap *heap = fibonacci_heap_from_queue(queue);

    fibonacci_heap_destroy_nodes(heap->minimum);
    free(heap);
}

/**
 * @brief Insert an item as a new singleton root.
 *
 * Fibonacci heap insertion does not consolidate. It just links the new node
 * into the root list and updates the minimum pointer if necessary.
 */
static int fibonacci_heap_push(struct priority_queue *queue, void *item)
{
    return fibonacci_heap_push_handle(queue, item) == NULL ? -1 : 0;
}

/**
 * @brief Insert an item as a new singleton root and return its handle.
 */
static struct priority_queue_handle *fibonacci_heap_push_handle(
    struct priority_queue *queue,
    void *item
)
{
    struct fibonacci_heap *heap = fibonacci_heap_from_queue(queue);
    struct fibonacci_heap_node *node;

    node = fibonacci_heap_node_create(item);
    if (node == NULL)
        return NULL;

    node->handle.queue = queue;
    fibonacci_heap_add_root(heap, node);
    heap->size++;

    return &node->handle;
}

/**
 * @brief Cut a decreased node to the root list when it violates parent order.
 *
 * This mirrors the paper's decrease-key repair after locating the node: cut the
 * violating node, then cascade through ancestors that have already lost a child.
 */
static int fibonacci_heap_decrease_key(
    struct priority_queue *queue,
    struct priority_queue_handle *handle
)
{
    struct fibonacci_heap *heap = fibonacci_heap_from_queue(queue);
    struct fibonacci_heap_node *node =
        fibonacci_heap_node_from_handle(handle);
    struct fibonacci_heap_node *parent;

    parent = node->parent;
    if (
        parent != NULL &&
        heap->base.cmp(node->handle.item, parent->handle.item) < 0
        ) {
        fibonacci_heap_cut(heap, node, parent);
        fibonacci_heap_cascading_cut(heap, parent);
    }

    if (heap->base.cmp(node->handle.item, heap->minimum->handle.item) < 0)
        heap->minimum = node;

    return 0;
}

/**
 * @brief Remove one item by the paper's delete operation.
 *
 * If the node is the current minimum, removal is delete-min. Otherwise its
 * children become roots, the node is cut from its parent or root list, and the
 * cascading-cut rule is applied to the parent that lost a child.
 */
static void *fibonacci_heap_remove(
    struct priority_queue *queue,
    struct priority_queue_handle *handle
)
{
    struct fibonacci_heap *heap = fibonacci_heap_from_queue(queue);
    struct fibonacci_heap_node *node =
        fibonacci_heap_node_from_handle(handle);
    struct fibonacci_heap_node *parent = node->parent;
    void *item = handle->item;

    handle->queue = NULL;

    if (node == heap->minimum) {
        (void)fibonacci_heap_pop(queue);
        return item;
    }

    fibonacci_heap_promote_children(heap, node);

    if (parent != NULL) {
        fibonacci_heap_remove_child(parent, node);
        fibonacci_heap_cascading_cut(heap, parent);
    } else {
        fibonacci_heap_list_remove(node);
    }

    heap->size--;
    free(node);
    return item;
}

/**
 * @brief Return whether item is stored by pointer identity.
 */
static int fibonacci_heap_contains(
    const struct priority_queue *queue,
    const void *item
)
{
    const struct fibonacci_heap *heap =
        fibonacci_heap_from_const_queue(queue);

    return fibonacci_heap_find_node(heap->minimum, item) != NULL;
}

/** @brief Return the current minimum item without removing it. */
static void *fibonacci_heap_peek(const struct priority_queue *queue)
{
    const struct fibonacci_heap *heap =
        fibonacci_heap_from_const_queue(queue);

    if (heap->minimum == NULL)
        return NULL;

    return heap->minimum->handle.item;
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
static void *fibonacci_heap_pop(struct priority_queue *queue)
{
    struct fibonacci_heap *heap = fibonacci_heap_from_queue(queue);
    struct fibonacci_heap_node *minimum = heap->minimum;
    void *item;
    size_t child_count;
    size_t i;

    if (minimum == NULL)
        return NULL;

    item = minimum->handle.item;
    minimum->handle.queue = NULL;

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

    free(minimum);
    return item;
}

/** @brief Return the current number of stored items. */
static size_t fibonacci_heap_size(const struct priority_queue *queue)
{
    return fibonacci_heap_from_const_queue(queue)->size;
}

/** @brief Return whether the heap contains no items. */
static int fibonacci_heap_empty(const struct priority_queue *queue)
{
    return fibonacci_heap_size(queue) == 0;
}
