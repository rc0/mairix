#########################################################################
#
# $Header: /cvs/src/mairix/Attic/Makefile,v 1.1 2002/07/03 22:15:58 richard Exp $
#
# =======================================================================
#
# mairix - message index builder and finder for maildir folders.
#
# Copyright (C) Richard P. Curnow  2002
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

#########################################################################
# Things below this point shouldn't need to be edited.

OBJ = mairix.o db.o rfc822.o tok.o hash.o dirscan.o writer.o \
      reader.o search.o stats.o

all : mairix

mairix : $(OBJ)
	$(CC) -o mairix $(CFLAGS) $(OBJ)

%.o : %.c
	$(CC) -c $(CFLAGS) $<

clean:
	-rm -f *~ *.o mairix *.s core mairix.txt mairix.html mairix.dvi mairix.ps mairix.pdf mairix.info
	-rm -f mairix.cp mairix.fn mairix.aux mairix.log mairix.ky mairix.pg mairix.toc mairix.tp mairix.vr

install:
	[ -d $(prefix) ] || mkdir -p $(prefix)
	[ -d $(bindir) ] || mkdir -p $(bindir)
	[ -d $(mandir) ] || mkdir -p $(mandir)
	[ -d $(man1dir) ] || mkdir -p $(man1dir)
	cp -f mairix $(bindir)
	chmod 555 $(bindir)/mairix

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

