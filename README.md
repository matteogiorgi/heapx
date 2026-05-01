# heapx

heapx is a compact C99 heap library and experimental workspace. It focuses on
heap data structures and exposes them through one common heap API, so
different heap variants can be used, tested, and benchmarked behind the same
interface.

The repository contains:

- a public C API based on the opaque `struct heapx_heap` type;
- binary min-heap, Fibonacci heap, and Kaplan heap implementations;
- C tests and a DIMACS-backed Dijkstra benchmark;
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

Available implementations are:

| C enum | Heap backend | Status |
| --- | --- | --- |
| `HEAPX_BINARY_HEAP` | Binary min-heap | implemented |
| `HEAPX_FIBONACCI_HEAP` | Fibonacci heap | implemented |
| `HEAPX_KAPLAN_HEAP` | Simple Fibonacci heap from "Fibonacci Heaps Revisited" | implemented |

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

Build the generated C API reference:

```sh
make docs
```

The generated HTML is written to `build/docs/html/`.

Run the heap comparison benchmark on the default DIMACS graph:

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

DIMACS graph files are local benchmark inputs for comparing heap backends and
are ignored by git. Place `.gr` files under `graphs/dimacs/`.

The default smoke benchmark expects:

```text
graphs/dimacs/USA-road-d.NY.gr
```

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
known. `contains` remains a pointer-identity convenience query and is linear in
the current backends.

## Roadmap

The next planned work keeps the repository centered on heaps:

- add focused correctness and sanitizer targets to the build;
- add a small versioned DIMACS graph for reproducible benchmark smoke tests;
- broaden randomized tests that compare all heap backends against a simple
  reference model;
- evaluate whether a future heap-native public API should wrap or replace the
  current `heapx_*` API.
