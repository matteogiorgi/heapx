#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "hpqlib/priority_queue.h"

struct csr_graph {
    size_t node_count;
    size_t arc_count;
    size_t loaded_arc_count;
    size_t *offsets;
    unsigned *heads;
    unsigned *weights;
};

struct arc {
    unsigned tail;
    unsigned head;
    unsigned weight;
};

struct dijkstra_node {
    unsigned id;
    uint64_t distance;
    int finalized;
    struct priority_queue_handle *handle;
};

struct dijkstra_result {
    double seconds;
    uint64_t checksum;
    uint64_t reachable_count;
    uint64_t relaxations;
    uint64_t pushes;
    uint64_t decrease_keys;
};

struct implementation_case {
    enum priority_queue_implementation implementation;
    const char *name;
};

static const uint64_t DIJKSTRA_INF = UINT64_MAX / 4;

static int dijkstra_node_cmp(const void *lhs, const void *rhs)
{
    const struct dijkstra_node *left = lhs;
    const struct dijkstra_node *right = rhs;

    if (left->distance < right->distance)
        return -1;
    if (left->distance > right->distance)
        return 1;
    if (left->id < right->id)
        return -1;
    if (left->id > right->id)
        return 1;
    return 0;
}

static double elapsed_seconds(struct timespec start, struct timespec end)
{
    return (double)(end.tv_sec - start.tv_sec) +
        (double)(end.tv_nsec - start.tv_nsec) / 1000000000.0;
}

static int parse_size(const char *text, size_t *value)
{
    char *end;
    unsigned long long parsed;

    errno = 0;
    parsed = strtoull(text, &end, 10);
    if (errno != 0 || *end != '\0' || parsed > (unsigned long long)SIZE_MAX)
        return -1;

    *value = (size_t)parsed;
    return 0;
}

static int parse_unsigned(const char *text, unsigned *value)
{
    char *end;
    unsigned long parsed;

    errno = 0;
    parsed = strtoul(text, &end, 10);
    if (errno != 0 || *end != '\0' || parsed > UINT_MAX)
        return -1;

    *value = (unsigned)parsed;
    return 0;
}

static void csr_graph_destroy(struct csr_graph *graph)
{
    free(graph->offsets);
    free(graph->heads);
    free(graph->weights);
    graph->offsets = NULL;
    graph->heads = NULL;
    graph->weights = NULL;
}

static int build_csr_graph(
    struct csr_graph *graph,
    const struct arc *arcs,
    size_t loaded_arc_count
)
{
    size_t *positions;
    size_t i;

    graph->offsets = calloc(graph->node_count + 2, sizeof(*graph->offsets));
    graph->heads = calloc(loaded_arc_count, sizeof(*graph->heads));
    graph->weights = calloc(loaded_arc_count, sizeof(*graph->weights));
    positions = NULL;

    if (
        graph->offsets == NULL ||
        graph->heads == NULL ||
        graph->weights == NULL
        )
        goto fail;

    for (i = 0; i < loaded_arc_count; i++)
        graph->offsets[arcs[i].tail + 1]++;

    for (i = 1; i <= graph->node_count + 1; i++)
        graph->offsets[i] += graph->offsets[i - 1];

    positions = malloc((graph->node_count + 2) * sizeof(*positions));
    if (positions == NULL)
        goto fail;

    memcpy(
        positions,
        graph->offsets,
        (graph->node_count + 2) * sizeof(*positions)
    );

    for (i = 0; i < loaded_arc_count; i++) {
        size_t index = positions[arcs[i].tail]++;

        graph->heads[index] = arcs[i].head;
        graph->weights[index] = arcs[i].weight;
    }

    free(positions);
    return 0;

fail:
    free(positions);
    csr_graph_destroy(graph);
    return -1;
}

static int load_dimacs_graph(const char *path, struct csr_graph *graph)
{
    FILE *file;
    struct arc *arcs = NULL;
    size_t loaded_arc_count = 0;
    size_t line_number = 0;
    char line[512];
    int seen_problem = 0;

    memset(graph, 0, sizeof(*graph));

    file = fopen(path, "r");
    if (file == NULL) {
        fprintf(stderr, "cannot open %s: %s\n", path, strerror(errno));
        return -1;
    }

    while (fgets(line, sizeof(line), file) != NULL) {
        char tag[16];

        line_number++;
        if (line[0] == '\n' || line[0] == '\0' || line[0] == 'c')
            continue;

        if (sscanf(line, "%15s", tag) != 1)
            continue;

        if (strcmp(tag, "p") == 0) {
            char problem[16];
            char nodes_text[32];
            char arcs_text[32];

            if (
                sscanf(
                    line,
                    "%15s %15s %31s %31s",
                    tag,
                    problem,
                    nodes_text,
                    arcs_text
                ) != 4 ||
                strcmp(problem, "sp") != 0 ||
                parse_size(nodes_text, &graph->node_count) != 0 ||
                parse_size(arcs_text, &graph->arc_count) != 0
                ) {
                fprintf(stderr, "invalid problem line at %zu\n", line_number);
                goto fail;
            }

            arcs = calloc(graph->arc_count, sizeof(*arcs));
            if (arcs == NULL)
                goto fail;
            seen_problem = 1;
            continue;
        }

        if (strcmp(tag, "a") == 0) {
            char tail_text[32];
            char head_text[32];
            char weight_text[32];
            struct arc arc;

            if (!seen_problem || loaded_arc_count >= graph->arc_count) {
                fprintf(stderr, "unexpected arc line at %zu\n", line_number);
                goto fail;
            }

            if (
                sscanf(
                    line,
                    "%15s %31s %31s %31s",
                    tag,
                    tail_text,
                    head_text,
                    weight_text
                ) != 4 ||
                parse_unsigned(tail_text, &arc.tail) != 0 ||
                parse_unsigned(head_text, &arc.head) != 0 ||
                parse_unsigned(weight_text, &arc.weight) != 0 ||
                arc.tail == 0 ||
                arc.head == 0 ||
                arc.tail > graph->node_count ||
                arc.head > graph->node_count
                ) {
                fprintf(stderr, "invalid arc line at %zu\n", line_number);
                goto fail;
            }

            arcs[loaded_arc_count++] = arc;
            continue;
        }

        fprintf(stderr, "unsupported line tag at %zu: %s\n", line_number, tag);
        goto fail;
    }

    if (ferror(file)) {
        fprintf(stderr, "error while reading %s\n", path);
        goto fail;
    }

    fclose(file);
    file = NULL;

    if (!seen_problem) {
        fprintf(stderr, "missing DIMACS problem line\n");
        goto fail;
    }

    graph->loaded_arc_count = loaded_arc_count;
    if (build_csr_graph(graph, arcs, loaded_arc_count) != 0)
        goto fail;

    free(arcs);
    return 0;

fail:
    if (file != NULL)
        fclose(file);
    free(arcs);
    csr_graph_destroy(graph);
    return -1;
}

static void dijkstra_nodes_destroy(struct dijkstra_node *nodes)
{
    free(nodes);
}

static struct dijkstra_node *dijkstra_nodes_create(size_t node_count)
{
    struct dijkstra_node *nodes;
    size_t i;

    nodes = malloc((node_count + 1) * sizeof(*nodes));
    if (nodes == NULL)
        return NULL;

    for (i = 1; i <= node_count; i++) {
        nodes[i].id = (unsigned)i;
        nodes[i].distance = DIJKSTRA_INF;
        nodes[i].finalized = 0;
        nodes[i].handle = NULL;
    }

    return nodes;
}

static uint64_t dijkstra_checksum(
    const struct dijkstra_node *nodes,
    size_t node_count,
    uint64_t *reachable_count
)
{
    uint64_t checksum = 1469598103934665603ULL;
    size_t i;

    *reachable_count = 0;
    for (i = 1; i <= node_count; i++) {
        uint64_t value = nodes[i].distance;

        if (value != DIJKSTRA_INF)
            (*reachable_count)++;

        checksum ^= value + (uint64_t)i * 1099511628211ULL;
        checksum *= 1099511628211ULL;
    }

    return checksum;
}

static int run_dijkstra(
    const struct csr_graph *graph,
    unsigned source,
    enum priority_queue_implementation implementation,
    struct dijkstra_result *result
)
{
    struct priority_queue *queue;
    struct dijkstra_node *nodes;
    struct timespec start;
    struct timespec end;
    int status = -1;

    if (source == 0 || source > graph->node_count)
        return -1;

    nodes = dijkstra_nodes_create(graph->node_count);
    if (nodes == NULL)
        return -1;

    queue = priority_queue_create(implementation, dijkstra_node_cmp);
    if (queue == NULL)
        goto done_nodes;

    memset(result, 0, sizeof(*result));
    nodes[source].distance = 0;
    nodes[source].handle = priority_queue_push_handle(queue, &nodes[source]);
    if (nodes[source].handle == NULL)
        goto done_queue;
    result->pushes++;

    if (clock_gettime(CLOCK_MONOTONIC, &start) != 0)
        goto done_queue;

    while (!priority_queue_empty(queue)) {
        struct dijkstra_node *node = priority_queue_pop(queue);
        size_t begin;
        size_t end_offset;
        size_t edge;

        node->handle = NULL;
        if (node->finalized)
            continue;

        node->finalized = 1;
        begin = graph->offsets[node->id];
        end_offset = graph->offsets[node->id + 1];

        for (edge = begin; edge < end_offset; edge++) {
            unsigned head = graph->heads[edge];
            uint64_t weight = graph->weights[edge];
            struct dijkstra_node *neighbor = &nodes[head];
            uint64_t candidate;

            result->relaxations++;
            if (neighbor->finalized)
                continue;
            if (node->distance > DIJKSTRA_INF - weight)
                continue;

            candidate = node->distance + weight;
            if (candidate >= neighbor->distance)
                continue;

            neighbor->distance = candidate;
            if (neighbor->handle == NULL) {
                neighbor->handle = priority_queue_push_handle(queue, neighbor);
                if (neighbor->handle == NULL)
                    goto done_queue;
                result->pushes++;
            } else {
                if (priority_queue_decrease_key(queue, neighbor->handle) != 0)
                    goto done_queue;
                result->decrease_keys++;
            }
        }
    }

    if (clock_gettime(CLOCK_MONOTONIC, &end) != 0)
        goto done_queue;

    result->seconds = elapsed_seconds(start, end);
    result->checksum = dijkstra_checksum(
        nodes,
        graph->node_count,
        &result->reachable_count
    );
    status = 0;

done_queue:
    priority_queue_destroy(queue);
done_nodes:
    dijkstra_nodes_destroy(nodes);
    return status;
}

static int run_all_implementations(
    const struct csr_graph *graph,
    unsigned source
)
{
    const struct implementation_case cases[] = {
        { PRIORITY_QUEUE_BINARY_HEAP, "binary_heap" },
        { PRIORITY_QUEUE_FIBONACCI_HEAP, "fibonacci_heap" },
        { PRIORITY_QUEUE_KAPLAN_HEAP, "kaplan_heap" }
    };
    struct dijkstra_result results[sizeof(cases) / sizeof(cases[0])];
    uint64_t expected_checksum = 0;
    uint64_t expected_reachable_count = 0;
    size_t i;

    printf(
        "graph nodes=%zu arcs=%zu loaded_arcs=%zu source=%u\n",
        graph->node_count,
        graph->arc_count,
        graph->loaded_arc_count,
        source
    );
    printf(
        "%-16s %12s %12s %12s %12s %12s %18s\n",
        "implementation",
        "seconds",
        "reachable",
        "relax",
        "pushes",
        "decrease",
        "checksum"
    );

    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        if (
            run_dijkstra(
                graph,
                source,
                cases[i].implementation,
                &results[i]
            ) != 0
            ) {
            fprintf(stderr, "dijkstra failed for %s\n", cases[i].name);
            return -1;
        }

        if (i == 0) {
            expected_checksum = results[i].checksum;
            expected_reachable_count = results[i].reachable_count;
        } else if (
            results[i].checksum != expected_checksum ||
            results[i].reachable_count != expected_reachable_count
            ) {
            fprintf(stderr, "result mismatch for %s\n", cases[i].name);
            return -1;
        }

        printf(
            "%-16s %12.6f %12" PRIu64 " %12" PRIu64
            " %12" PRIu64 " %12" PRIu64 " %18" PRIu64 "\n",
            cases[i].name,
            results[i].seconds,
            results[i].reachable_count,
            results[i].relaxations,
            results[i].pushes,
            results[i].decrease_keys,
            results[i].checksum
        );
    }

    return 0;
}

static void print_usage(const char *program)
{
    fprintf(stderr, "Usage: %s <graph.gr> [source-node]\n", program);
}

int main(int argc, char **argv)
{
    struct csr_graph graph;
    size_t source_size = 1;
    int status;

    if (argc < 2 || argc > 3) {
        print_usage(argv[0]);
        return 2;
    }

    if (argc == 3 && parse_size(argv[2], &source_size) != 0) {
        print_usage(argv[0]);
        return 2;
    }

    if (load_dimacs_graph(argv[1], &graph) != 0)
        return 1;

    if (source_size == 0 || source_size > graph.node_count) {
        fprintf(stderr, "source node out of range\n");
        csr_graph_destroy(&graph);
        return 2;
    }

    status = run_all_implementations(&graph, (unsigned)source_size);
    csr_graph_destroy(&graph);
    return status == 0 ? 0 : 1;
}
