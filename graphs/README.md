# Graph Datasets

This directory contains DIMACS shortest-path graph files used by heap-backend
benchmark runs. Large downloaded dataset files are intentionally ignored by
git, while `graphs/dimacs/tiny.gr` is versioned for reproducible smoke tests.

The C Dijkstra benchmark reads `.gr` files with lines like:

```text
c optional comments
p sp <node-count> <arc-count>
a <tail> <head> <weight>
```

Put downloaded files under `graphs/dimacs/`, then run:

```sh
make benchmark GRAPH=graphs/dimacs/USA-road-d.NY.gr SOURCE=1
```

The default smoke target uses `graphs/dimacs/tiny.gr`:

```sh
make benchmark-smoke
```
