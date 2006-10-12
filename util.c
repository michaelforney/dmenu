/* (C)opyright MMVI Anselm R. Garbe <garbeam at gmail dot com>
 * See LICENSE file for license details.
 */
#include "dmenu.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

void *
emalloc(unsigned int size) {
	void *res = malloc(size);

	if(!res)
		eprint("fatal: could not malloc() %u bytes\n", size);
	return res;
}

void
eprint(const char *errstr, ...) {
	va_list ap;

	va_start(ap, errstr);
	vfprintf(stderr, errstr, ap);
	va_end(ap);
	exit(EXIT_FAILURE);
}

char *
estrdup(const char *str) {
	void *res = strdup(str);

	if(!res)
		eprint("fatal: could not malloc() %u bytes\n", strlen(str));
	return res;
}
