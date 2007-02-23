# dmenu - dynamic menu
#   (C)opyright MMVI-MMVII Anselm R. Garbe

include config.mk

SRC = draw.c main.c util.c
OBJ = ${SRC:.c=.o}

all: options dmenu

options:
	@echo dmenu build options:
	@echo "CFLAGS   = ${CFLAGS}"
	@echo "LDFLAGS  = ${LDFLAGS}"
	@echo "CC       = ${CC}"

.c.o:
	@echo CC $<
	@${CC} -c ${CFLAGS} $<

${OBJ}: dmenu.h config.mk

dmenu: ${OBJ}
	@echo CC -o $@
	@${CC} -o $@ ${OBJ} ${LDFLAGS}
	@strip $@

clean:
	@echo cleaning
	@rm -f dmenu ${OBJ} dmenu-${VERSION}.tar.gz

dist: clean
	@echo creating dist tarball
	@mkdir -p dmenu-${VERSION}
	@cp -R LICENSE Makefile README config.mk dmenu.1 dmenu.h dmenu_path ${SRC} dmenu-${VERSION}
	@tar -cf dmenu-${VERSION}.tar dmenu-${VERSION}
	@gzip dmenu-${VERSION}.tar
	@rm -rf dmenu-${VERSION}

install: all
	@echo installing executable file to ${DESTDIR}${PREFIX}/bin
	@mkdir -p ${DESTDIR}${PREFIX}/bin
	@cp -f dmenu dmenu_path ${DESTDIR}${PREFIX}/bin
	@chmod 755 ${DESTDIR}${PREFIX}/bin/dmenu
	@chmod 755 ${DESTDIR}${PREFIX}/bin/dmenu_path
	@echo installing manual page to ${DESTDIR}${MANPREFIX}/man1
	@mkdir -p ${DESTDIR}${MANPREFIX}/man1
	@sed "s/VERSION/${VERSION}/g" < dmenu.1 > ${DESTDIR}${MANPREFIX}/man1/dmenu.1
	@chmod 644 ${DESTDIR}${MANPREFIX}/man1/dmenu.1

uninstall:
	@echo removing executable file from ${DESTDIR}${PREFIX}/bin
	@rm -f ${DESTDIR}${PREFIX}/bin/dmenu ${DESTDIR}${PREFIX}/bin/dmenu_path
	@echo removing manual page from ${DESTDIR}${MANPREFIX}/man1
	@rm -f ${DESTDIR}${MANPREFIX}/man1/dmenu.1

.PHONY: all options clean dist install uninstall
