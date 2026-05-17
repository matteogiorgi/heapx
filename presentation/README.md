# heapx Presentation

This directory contains the Beamer slides for a short oral presentation of the
heapx project.

Build the presentation with XeLaTeX:

```bash
cd presentation
latexmk -xelatex main.tex
```

Clean temporary LaTeX artifacts with:

```bash
cd presentation
latexmk -c main.tex
```

The slides use `fontspec` and Cascadia Code, so they should be compiled with
XeLaTeX rather than pdfLaTeX.

Generated build products, including `main.pdf`, are ignored by
`presentation/.gitignore`.
