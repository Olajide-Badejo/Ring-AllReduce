.PHONY: all clean

all: main.pdf

main.pdf: main.tex sections/*.tex references.bib
	latexmk -pdf -interaction=nonstopmode -halt-on-error main.tex

clean:
	latexmk -C main.tex
	rm -f *.bbl *.run.xml
