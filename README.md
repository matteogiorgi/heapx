# hpqlib

hpqlib is a compact C99 library for experimenting with heap-backed priority
queues behind one abstract API. The project intentionally stays focused on
heap-based implementations so comparisons remain small, direct, and easy to
reason about.

The repository contains:

- a public C API based on the opaque `struct priority_queue` type;
- binary min-heap, Fibonacci heap, and Kaplan heap implementations;
- C tests and a DIMACS-backed Dijkstra benchmark;
- generated C API documentation through Doxygen;
- reference papers under `docs/papers/`.

## Design Goals

hpqlib is built around a few explicit constraints:

- the library is written in portable C99;
- callers use one public abstract priority-queue API;
- concrete heap implementations dispatch through an internal vtable;
- stored items are generic `void *` pointers and remain owned by the caller;
- targeted operations use insertion handles returned by
  `priority_queue_push_handle()`.

The project is not intended to become a generic collection of unrelated
priority-queue families. Current and planned implementations are heap-based.

## Architecture

The public type is opaque:

```c
struct priority_queue;
```

Client code creates a queue with:

```c
struct priority_queue *priority_queue_create(
    enum priority_queue_implementation implementation,
    priority_queue_cmp_fn cmp
);
```

The selected backend embeds the common base object as its first field and
provides a private `priority_queue_vtable`. Public functions define common
edge-case behavior, such as `NULL` queue handling, and dispatch to the selected
backend.

Available implementations are:

| C enum | Backend | Status |
| --- | --- | --- |
| `PRIORITY_QUEUE_BINARY_HEAP` | Binary min-heap | implemented |
| `PRIORITY_QUEUE_FIBONACCI_HEAP` | Fibonacci heap | implemented |
| `PRIORITY_QUEUE_KAPLAN_HEAP` | Simple Fibonacci heap from "Fibonacci Heaps Revisited" | implemented |

## C Example

```c
#include <stdio.h>

#include "hpqlib/priority_queue.h"

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
    struct priority_queue *queue;
    size_t i;

    queue = priority_queue_create(PRIORITY_QUEUE_BINARY_HEAP, int_cmp);
    if (queue == NULL)
        return 1;

    for (i = 0; i < sizeof(values) / sizeof(values[0]); i++)
        priority_queue_push(queue, &values[i]);

    while (!priority_queue_empty(queue)) {
        int *value = priority_queue_pop(queue);
        printf("%d\n", *value);
    }

    priority_queue_destroy(queue);
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

Run the C Dijkstra benchmark on the default DIMACS graph:

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

DIMACS graph files are local benchmark inputs and are ignored by git. Place
`.gr` files under `graphs/dimacs/`.

The default smoke benchmark expects:

```text
graphs/dimacs/USA-road-d.NY.gr
```

Large datasets, such as `USA-road-d.USA.gr`, should remain local files rather
than committed repository content.

## Repository Layout

- `include/`: public headers consumed by client code.
- `include/hpqlib/priority_queue.h`: public priority-queue API.
- `src/`: private C implementation sources.
- `src/priority_queue.c`: public API dispatch and implementation factory.
- `src/priority_queue_internal.h`: internal base object layout and vtable.
- `src/heaps/`: heap-based implementations.
- `tests/`: C tests and benchmark source.
- `graphs/`: local graph datasets for manual benchmark runs.
- `docs/papers/`: reference papers used during development.
- `Doxyfile`: generated C API reference configuration.
- `Makefile`: C build, tests, benchmark, docs, and cleanup targets.

## Current Limitations

The public C API intentionally stays compact:

- `create`
- `destroy`
- `push`
- `push_handle`
- `decrease_key`
- `remove`
- `contains`
- `peek`
- `pop`
- `size`
- `empty`

The targeted operations `decrease_key` and `remove` use handles returned by
`push_handle`, matching the usual heap assumption that the item's position is
known. `contains` remains a pointer-identity convenience query and is linear in
the current backends.
