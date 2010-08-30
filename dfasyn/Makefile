# Makefile for NFA->DFA conversion utility
#
# Copyright (C) Richard P. Curnow  2000-2001,2003,2005,2006,2007
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
# 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
#

CC=gcc
#CFLAGS=-g -Wall
#CFLAGS=-O2 -pg
CFLAGS=-Wall
prefix?=/usr/local
bindir=$(prefix)/bin
mandir?=$(prefix)/man
man1dir=$(mandir)/man1
man5dir=$(mandir)/man5

OBJ = dfasyn.o parse.o scan.o \
	tokens.o abbrevs.o charclass.o \
	stimulus.o \
	blocks.o states.o \
	n2d.o expr.o evaluator.o \
	tabcompr.o compdfa.o

all : dfasyn

install : all
	[ -d $(bindir) ] || mkdir -p $(bindir)
	[ -d $(man1dir) ] || mkdir -p $(man1dir)
	[ -d $(man5dir) ] || mkdir -p $(man5dir)
	cp dfasyn $(bindir)
	cp dfasyn.1 $(man1dir)
	cp dfasyn.5 $(man5dir)

dfasyn : $(OBJ)
	$(CC) $(CFLAGS) -o dfasyn $(OBJ)

parse.c parse.h : parse.y
	bison -v -d -o parse.c parse.y

parse.o : parse.c dfasyn.h

scan.c : scan.l
	flex -t -s scan.l > scan.c

scan.o : scan.c parse.h dfasyn.h

$(OBJ) : dfasyn.h

clean:
	rm -f dfasyn *.o scan.c parse.c parse.h parse.output

