#########################################################################
#
# $Header: /cvs/src/mairix/Attic/Makefile,v 1.7 2003/03/12 23:57:40 richard Exp $
#
# =======================================================================
#
# mairix - message index builder and finder for maildir folders.
#
# Copyright (C) Richard P. Curnow  2002, 2003
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of version 2 of the GNU General Public License as
# published by the Free Software Foundation.
# 
# This program is distributed in the hope that it will be useful, but
# WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# General Public License for more details.
# 
# You should have received a copy of the GNU General Public License along
# with this program; if not, write to the Free Software Foundation, Inc.,
# 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA
#
# =======================================================================

#########################################################################
# Edit the following variables as required
CC=gcc
#CFLAGS=-O2
#CFLAGS=-O2 -pg
CFLAGS=-Wall -g

prefix=/usr/local
bindir=$(prefix)/bin
mandir=$(prefix)/man
man1dir=$(mandir)/man1
infodir=$(prefix)/info
docdir=$(prefix)/docs

#########################################################################
# Things below this point shouldn't need to be edited.

OBJ = mairix.o db.o rfc822.o tok.o hash.o dirscan.o writer.o \
      reader.o search.o stats.o dates.o datescan.o

all : mairix

mairix : $(OBJ)
	$(CC) -o mairix $(CFLAGS) $(OBJ)

%.o : %.c memmac.h mairix.h reader.h Makefile
	$(CC) -c $(CFLAGS) $<

datescan.c : datescan.nfa dfasyn/dfasyn
	dfasyn/dfasyn -o datescan.c -v -u datescan.nfa

dfasyn/dfasyn:
	if [ -d dfasyn ]; then cd dfasyn ; make CC="$(CC)" CFLAGS="$(CFLAGS)" ; else echo "No dfasyn subdirectory?" ; exit 1 ; fi

clean:
	-rm -f *~ *.o mairix *.s core mairix.txt mairix.html mairix.dvi mairix.ps mairix.pdf mairix.info
	-rm -f mairix.cp mairix.fn mairix.aux mairix.log mairix.ky mairix.pg mairix.toc mairix.tp mairix.vr
	if [ -d dfasyn ]; then cd dfasyn ; make clean ; fi

install:
	[ -d $(prefix) ] || mkdir -p $(prefix)
	[ -d $(bindir) ] || mkdir -p $(bindir)
	[ -d $(mandir) ] || mkdir -p $(mandir)
	[ -d $(man1dir) ] || mkdir -p $(man1dir)
	cp -f mairix $(bindir)
	chmod 555 $(bindir)/mairix

install_docs:
	if [ -f mairix.info ]; then [ -d $(infodir) ] || mkdir -p $(infodir) ; cp -f mairix.info* $(infodir) ; chmod 444 $(infodir)/mairix.info* ; fi
	if [ -f mairix.txt ]; then [ -d $(docdir) ] || mkdir -p $(docdir) ; cp -f mairix.txt $(docdir) ; chmod 444 $(docdir)/mairix.txt ; fi
	if [ -f mairix.html ]; then [ -d $(docdir) ] || mkdir -p $(docdir) ; cp -f mairix.html $(docdir) ; chmod 444 $(docdir)/mairix.html ; fi
	if [ -f mairix.dvi ]; then [ -d $(docdir) ] || mkdir -p $(docdir) ; cp -f mairix.dvi $(docdir) ; chmod 444 $(docdir)/mairix.dvi ; fi
	if [ -f mairix.pdf ]; then [ -d $(docdir) ] || mkdir -p $(docdir) ; cp -f mairix.pdf $(docdir) ; chmod 444 $(docdir)/mairix.pdf ; fi

docs : mairix.info mairix.txt mairix.html mairix.dvi mairix.pdf

mairix.info : mairix.texi
	makeinfo mairix.texi

mairix.txt : mairix.texi
	makeinfo --no-split --number-sections --no-headers mairix.texi > mairix.txt

mairix.html : mairix.texi
	makeinfo --no-split --number-sections --html mairix.texi > mairix.html

mairix.dvi : mairix.texi
	tex mairix.texi
	tex mairix.texi

mairix.ps : mairix.dvi
	dvips mairix.dvi -o

mairix.pdf : mairix.texi
	pdftex mairix.texi
	pdftex mairix.texi

.PHONY : ChangeLog

ChangeLog:
	cvs2cl.pl -r -b

