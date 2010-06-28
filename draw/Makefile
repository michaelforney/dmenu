# libdraw - dynamic drawing library
# See LICENSE file for copyright and license details.

include ../config.mk

SRC = cleanupdraw.c drawsquare.c drawtext.c eprint.c getcolor.c initfont.c \
setupdraw.c textnw.c textw.c
OBJ = ${SRC:.c=.o}

all: libdraw.a

.c.o:
	@echo CC $<
	@${CC} -c ${CFLAGS} $<

${OBJ}: ../config.mk draw.h

libdraw.a: ${OBJ}
	@echo AR $@
	@ar cr $@ $+

clean:
	@echo cleaning libdraw
	@rm -f libdraw.a ${OBJ}

.PHONY: all options clean
