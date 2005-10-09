# $Header: /cvs/src/dfasyn/Makefile,v 1.4 2003/03/03 00:05:41 richard Exp $
#
# Makefile for NFA->DFA conversion utility
#
# Copyright (C) Richard P. Curnow  2000-2001,2003
# LICENCE
#

CC=gcc
#CFLAGS=-g
#CFLAGS=-O2 -pg
CFLAGS=-O2 -Wall
prefix=/usr/local
bindir=$(prefix)/bin

OBJ = parse.o scan.o n2d.o expr.o tabcompr.o compdfa.o

all : dfasyn

install : all
	if [ ! -d $(bindir) ]; then mkdir -p $(bindir) ; fi
	cp dfasyn $(bindir)

dfasyn : $(OBJ)
	$(CC) $(CFLAGS) -o dfasyn $(OBJ)

parse.c parse.h : parse.y
	bison -v -d parse.y
	mv parse.tab.c parse.c
	mv parse.tab.h parse.h

parse.o : parse.c n2d.h

scan.c : scan.l
	flex -t -s scan.l > scan.c

scan.o : scan.c parse.h n2d.h

clean:
	rm dfasyn *.o scan.c parse.c parse.h parse.output

