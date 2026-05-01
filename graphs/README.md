# Graph Datasets

This directory is reserved for local DIMACS shortest-path graph files used by
manual benchmark runs. Dataset files are intentionally ignored by git.

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

The default smoke target uses `graphs/dimacs/USA-road-d.NY.gr`:

```sh
make benchmark-smoke
```
