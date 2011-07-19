# dmenu - dynamic menu
# See LICENSE file for copyright and license details.

include config.mk

SRC = dmenu.c draw.c lsx.c
OBJ = ${SRC:.c=.o}

all: options dmenu lsx

options:
	@echo dmenu build options:
	@echo "CFLAGS   = ${CFLAGS}"
	@echo "LDFLAGS  = ${LDFLAGS}"
	@echo "CC       = ${CC}"

.c.o:
	@echo CC -c $<
	@${CC} -c $< ${CFLAGS}

${OBJ}: config.mk

dmenu: dmenu.o draw.o
	@echo CC -o $@
	@${CC} -o $@ dmenu.o draw.o ${LDFLAGS}

lsx: lsx.o
	@echo CC -o $@
	@${CC} -o $@ lsx.o ${LDFLAGS}

clean:
	@echo cleaning
	@rm -f dmenu lsx ${OBJ} dmenu-${VERSION}.tar.gz

dist: clean
	@echo creating dist tarball
	@mkdir -p dmenu-${VERSION}
	@cp LICENSE Makefile README config.mk dmenu.1 draw.h dmenu_run lsx.1 ${SRC} dmenu-${VERSION}
	@tar -cf dmenu-${VERSION}.tar dmenu-${VERSION}
	@gzip dmenu-${VERSION}.tar
	@rm -rf dmenu-${VERSION}

install: all
	@echo installing executables to ${DESTDIR}${PREFIX}/bin
	@mkdir -p ${DESTDIR}${PREFIX}/bin
	@cp -f dmenu dmenu_run lsx ${DESTDIR}${PREFIX}/bin
	@chmod 755 ${DESTDIR}${PREFIX}/bin/dmenu
	@chmod 755 ${DESTDIR}${PREFIX}/bin/dmenu_run
	@chmod 755 ${DESTDIR}${PREFIX}/bin/lsx
	@echo installing manual pages to ${DESTDIR}${MANPREFIX}/man1
	@mkdir -p ${DESTDIR}${MANPREFIX}/man1
	@sed "s/VERSION/${VERSION}/g" < dmenu.1 > ${DESTDIR}${MANPREFIX}/man1/dmenu.1
	@sed "s/VERSION/${VERSION}/g" < lsx.1 > ${DESTDIR}${MANPREFIX}/man1/lsx.1
	@chmod 644 ${DESTDIR}${MANPREFIX}/man1/dmenu.1
	@chmod 644 ${DESTDIR}${MANPREFIX}/man1/lsx.1

uninstall:
	@echo removing executables from ${DESTDIR}${PREFIX}/bin
	@rm -f ${DESTDIR}${PREFIX}/bin/dmenu
	@rm -f ${DESTDIR}${PREFIX}/bin/dmenu_run
	@rm -f ${DESTDIR}${PREFIX}/bin/lsx
	@echo removing manual page from ${DESTDIR}${MANPREFIX}/man1
	@rm -f ${DESTDIR}${MANPREFIX}/man1/dmenu.1
	@rm -f ${DESTDIR}${MANPREFIX}/man1/lsx.1

.PHONY: all options clean dist install uninstall
