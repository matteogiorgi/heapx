# heapx LaTeX Report

The main report file is `main.tex`.

Recommended compilation command:

```bash
cd report
latexmk -xelatex main.tex
```

Temporary file cleanup:

```bash
cd report
latexmk -c main.tex
```

The report uses `fontspec` and Cascadia Code for monospaced code blocks, so it
must be compiled with XeLaTeX rather than pdfLaTeX.

The report is intentionally source-only in git. Build products such as `.aux`,
`.log`, `.toc`, `.xdv`, and generated PDFs are ignored by `report/.gitignore`.

Benchmark TSV files can be generated from the repository root with:

```bash
make benchmark-heap-tsv
```

The report currently describes the benchmark methodology and smoke-test
results. Large local DIMACS datasets and generated benchmark files should stay
outside version control.
