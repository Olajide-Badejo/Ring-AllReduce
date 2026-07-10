.PHONY: all clean

FIGURES := $(wildcard figures/*.pdf)
TABLES := $(wildcard tables/*.tex)

all: main.pdf

main.pdf: main.tex sections/*.tex references.bib $(FIGURES) $(TABLES)
	latexmk -pdf -interaction=nonstopmode -halt-on-error main.tex

clean:
	latexmk -C main.tex
	rm -f *.bbl *.run.xml
