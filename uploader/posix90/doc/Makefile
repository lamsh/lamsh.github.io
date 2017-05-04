#
#
#

all : info html pdf

info:
	makeinfo posix90.texi

html:
	texi2html posix90.texi

pdf:
	texi2dvi posix90.texi
	dvipdf posix90.dvi

clean :
	rm -f *.dvi *.info *.html *.aux *.cp *.cps *.pdf *.log *.fn *.ky *.pg *.toc *.tm *.tp *.vr *.ilg *.ind *~
