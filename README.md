# heapx

heapx is a compact C99 heap library and experimental workspace. It focuses on
heap data structures and exposes them through one common heap API, so
different heap variants can be used, tested, and benchmarked behind the same
interface.

The repository contains:

- a public C API based on the opaque `struct heapx_heap` type;
- binary min-heap, Fibonacci heap, and Kaplan heap implementations;
- C tests, internal invariant checks, and benchmarks;
- generated C API documentation through Doxygen;
- reference papers under `docs/papers/`.

## Design Goals

heapx is built around a few explicit constraints:

- the library is written in portable C99;
- the repository remains focused on heap data structures;
- callers use one public heap API for heap operations;
- concrete heap implementations dispatch through an internal vtable;
- stored items are generic `void *` pointers and remain owned by the caller;
- targeted operations use insertion handles returned by
  `heapx_insert_handle()`.

The project is not intended to become a generic collection of unrelated
heap families. Current and planned implementations are heap-based;
the heap API is the stable interface used to exercise those heaps.

## Architecture

The public type is opaque:

```c
struct heapx_heap;
```

Client code creates a heap with:

```c
struct heapx_heap *heapx_create(
    enum heapx_implementation implementation,
    heapx_cmp_fn cmp
);
```

The selected heap backend embeds the common base object as its first field and
provides a private `heapx_vtable`. Public functions define common
edge-case behavior, such as `NULL` heap handling, and dispatch to the selected
heap implementation.

Pointer-heavy backends use an internal fixed-size node pool for node wrappers.
This keeps allocation behavior close to the heap algorithm being measured:
removed nodes are recycled inside the backend, while caller-owned items remain
outside heapx ownership.

heapx does not provide internal synchronization. Use one heap from one thread
at a time, or protect heap creation, mutation, and destruction with external
synchronization. The internal heap identifiers used for handle validation are
process-local and are not updated atomically.

Available implementations are:

| C enum | Heap backend | Insert | Decrease key | Remove | Peek min | Extract min |
| --- | --- | --- | --- | --- | --- | --- |
| `HEAPX_BINARY_HEAP` | Binary min-heap | O(log n) | O(log n) | O(log n) | O(1) | O(log n) |
| `HEAPX_FIBONACCI_HEAP` | Fibonacci heap | amortized O(1) | amortized O(1) | amortized O(log n) | O(1) | amortized O(log n) |
| `HEAPX_KAPLAN_HEAP` | Simple Fibonacci heap from "Fibonacci Heaps Revisited" | amortized O(1) | amortized O(1) | amortized O(log n) | O(1) | amortized O(log n) |

`contains` is not a heap-native performance operation: current backends
implement it as an O(n) pointer-identity search.

The amortized bounds above describe normal successful-operation paths. If an
internal temporary allocation used for consolidation fails, pointer-heavy
backends preserve heap correctness with a fallback that may postpone some
consolidation work.

## C Example

```c
#include <stdio.h>

#include "heapx/heap.h"

static int int_cmp(const void *lhs, const void *rhs)
{
    const int *left = lhs;
    const int *right = rhs;

    if (*left < *right)
        return -1;
    if (*left > *right)
        return 1;
    return 0;
}

int main(void)
{
    int values[] = { 7, 3, 9, 1 };
    struct heapx_heap *heap;
    size_t i;

    heap = heapx_create(HEAPX_BINARY_HEAP, int_cmp);
    if (heap == NULL)
        return 1;

    for (i = 0; i < sizeof(values) / sizeof(values[0]); i++)
        heapx_insert(heap, &values[i]);

    while (!heapx_empty(heap)) {
        int *value = heapx_extract_min(heap);
        printf("%d\n", *value);
    }

    heapx_destroy(heap);
    return 0;
}
```

## Build And Test

Build the static library:

```sh
make
```

Run the C tests:

```sh
make test
```

Randomized tests use a deterministic seed by default. Override it with:

```sh
HEAPX_TEST_SEED=123 make test
```

Longer randomized campaigns can also override the number of stored items and
operation steps:

```sh
HEAPX_TEST_ITEMS=512 HEAPX_TEST_STEPS=20000 make test
```

Run the C tests with AddressSanitizer and UndefinedBehaviorSanitizer:

```sh
make sanitize
```

Run tests with internal invariant assertions enabled after mutating public
operations:

```sh
make debug-checks
```

Build the generated C API reference:

```sh
make docs
```

The generated HTML is written to `build/docs/html/`.

Run the heap-only microbenchmark, which measures API and heap behavior without
graph traversal costs:

```sh
make benchmark-heap
```

The heap-only benchmark reports separate scenarios for insert-only, first
extract-min, full insert/extract drain, decrease-key without drain,
decrease-key plus drain, and mixed handle operations.

Pass a different item count with `N=...`:

```sh
make benchmark-heap N=500000
```

Pass a deterministic benchmark seed with `SEED=...`:

```sh
make benchmark-heap N=500000 SEED=123
```

Run the quick heap-only benchmark used by CI:

```sh
make benchmark-heap-smoke
```

Emit tab-separated benchmark output for comparing runs:

```sh
make benchmark-heap FORMAT=tsv N=500000 SEED=123
```

Save a TSV benchmark run under `benchmarks/`:

```sh
make benchmark-heap-tsv N=500000 SEED=123
```

The default output path is derived from the item count and seed. Override it
with `BENCHMARK_HEAP_TSV=...`:

```sh
make benchmark-heap-tsv N=500000 SEED=123 BENCHMARK_HEAP_TSV=benchmarks/before.tsv
```

Compare two saved heap benchmark TSV files:

```sh
make benchmark-heap-compare BASE=benchmarks/before.tsv HEAD=benchmarks/after.tsv
```

Run the Dijkstra heap comparison benchmark on the default DIMACS graph:

```sh
make benchmark-smoke
```

Run the benchmark on another local DIMACS `.gr` graph:

```sh
make benchmark GRAPH=graphs/dimacs/USA-road-d.USA.gr SOURCE=1
```

Clean generated artifacts:

```sh
make clean
```

## Graph Datasets

DIMACS graph files are benchmark inputs for comparing heap backends. The repo
includes a tiny versioned smoke-test graph:

```text
graphs/dimacs/tiny.gr
```

Downloaded `.gr` files are ignored by git. Place larger local datasets under
`graphs/dimacs/` and pass them through `GRAPH=...`.

Large datasets, such as `USA-road-d.USA.gr`, should remain local files rather
than committed repository content.

## Repository Layout

- `include/`: public headers consumed by client code.
- `include/heapx/heap.h`: public heap API.
- `src/`: private C implementation sources.
- `src/heap.c`: public API dispatch and heap factory.
- `src/heap_internal.h`: internal base object layout and vtable.
- `src/heaps/`: concrete heap implementations.
- `tests/`: C tests and benchmark source.
- `graphs/`: local graph datasets for manual benchmark runs.
- `docs/papers/`: reference papers used during development.
- `Doxyfile`: generated C API reference configuration.
- `Makefile`: C build, tests, benchmark, docs, and cleanup targets.

## Current Limitations

The public C API intentionally stays compact and heap-shaped. It is
the current API for all heap backends:

- `create`
- `destroy`
- `insert`
- `insert_handle`
- `decrease_key`
- `remove`
- `contains`
- `peek_min`
- `extract_min`
- `size`
- `empty`

The targeted operations `decrease_key` and `remove` use handles returned by
`insert_handle`, matching the usual heap assumption that the item's position is
known.

Handles are generational value tokens. A handle records the logical id of the
heap that created it, plus an internal slot and generation. A handle is live
while its item remains stored in that heap. After `heapx_extract_min()` or
`heapx_remove()` removes the item, the handle becomes non-live; passing it back
to `heapx_decrease_key()` or `heapx_remove()` fails cleanly. Internal slot
generations keep stale handles from becoming valid again after slot reuse.

`contains` remains a pointer-identity convenience query and is linear in the
current backends. It is intentionally separated from heap-native performance
claims: benchmark results for the heap backends should be read through
`insert`, `insert_handle`, `decrease_key`, `remove`, `peek_min`, and
`extract_min`, not through `contains`.

## Roadmap

The next planned work keeps the repository centered on heaps:

- broaden randomized tests with longer runs;
- evaluate whether a future heap-native public API should wrap or replace the
  current `heapx_*` API.
