# dmenu - dynamic menu
#   (C)opyright MMVI Anselm R. Garbe

include config.mk

SRC = draw.c main.c util.c
OBJ = ${SRC:.c=.o}

all: options dmenu
	@echo finished

options:
	@echo dmenu build options:
	@echo "CFLAGS   = ${CFLAGS}"
	@echo "LDFLAGS  = ${LDFLAGS}"
	@echo "CC       = ${CC}"

.c.o:
	@echo CC $<
	@${CC} -c ${CFLAGS} $<

${OBJ}: dmenu.h config.h

config.h:
	@echo creating $@ from config.default.h
	@cp config.default.h $@

dmenu: ${OBJ}
	@echo LD $@
	@${CC} -o $@ ${OBJ} ${LDFLAGS}

clean:
	@echo cleaning
	@rm -f dmenu ${OBJ} dmenu-${VERSION}.tar.gz

dist: clean
	@echo creating dist tarball
	@mkdir -p dmenu-${VERSION}
	@cp -R LICENSE Makefile README config.*.h config.mk \
		dmenu.1 dmenu.h ${SRC} dmenu-${VERSION}
	@tar -cf dmenu-${VERSION}.tar dmenu-${VERSION}
	@gzip dmenu-${VERSION}.tar
	@rm -rf dmenu-${VERSION}

install: all
	@echo installing executable file to ${DESTDIR}${PREFIX}/bin
	@mkdir -p ${DESTDIR}${PREFIX}/bin
	@cp -f dmenu ${DESTDIR}${PREFIX}/bin
	@chmod 755 ${DESTDIR}${PREFIX}/bin/dmenu
	@echo installing manual page to ${DESTDIR}${MANPREFIX}/man1
	@mkdir -p ${DESTDIR}${MANPREFIX}/man1
	@cp -f dmenu.1 ${DESTDIR}${MANPREFIX}/man1
	@chmod 644 ${DESTDIR}${MANPREFIX}/man1/dmenu.1

uninstall:
	@echo removing executable file from ${DESTDIR}${PREFIX}/bin
	@rm -f ${DESTDIR}${PREFIX}/bin/dmenu
	@echo removing manual page from ${DESTDIR}${MANPREFIX}/man1
	@rm -f ${DESTDIR}${MANPREFIX}/man1/dmenu.1

.PHONY: all options clean dist install uninstall
