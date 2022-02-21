#
#  Two-kernel Monte for MIPS
#  Copyright (C) 2003 MuscleNerd <musclenerd2000@hotmail.com>
#
#  Based on the x86 version
#  Copyright (C) 2000 Erik Arjan Hendriks <hendriks@scyld.com>
#
#  This program is free software; you can redistribute it and/or modify
#  it under the terms of the GNU General Public License as published by
#  the Free Software Foundation; either version 2 of the License, or
#  (at your option) any later version.
#
#  This program is distributed in the hope that it will be useful,
#  but WITHOUT ANY WARRANTY; without even the implied warranty of
#  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#  GNU General Public License for more details.
#
#  You should have received a copy of the GNU General Public License
#  along with this program; if not, write to the Free Software
#  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
#
#  Makefile
#  $Revision: 1.2 $

VERSION="0.1"

#ifndef CC
CC=mips-TiVo-linux-gcc
#endif

CFLAGS=-Wall -s -DPACKAGE_VERSION='$(VERSION)'
KCFLAGS=-O3 -mips2 -G 0 -mno-abicalls -fno-pic -mlong-calls -D__KERNEL__ -DMODULE -DPACKAGE_VERSION='$(VERSION)'

all: monte kmonte.o

kmonte.o: kmonte.c
	$(CC) $(KCFLAGS) -c -o $@ $<

monte: monte.c
	$(CC) $(CFLAGS) -o $@ $<

clean:
	rm -f monte kmonte.o

