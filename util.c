/*
 * (C)opyright MMVI Anselm R. Garbe <garbeam at gmail dot com>
 * See LICENSE file for license details.
 */
#include "dmenu.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

/* static */

static void
bad_malloc(unsigned int size)
{
	eprint("fatal: could not malloc() %u bytes\n", size);
}

/* extern */

void *
emalloc(unsigned int size)
{
	void *res = malloc(size);
	if(!res)
		bad_malloc(size);
	return res;
}

void *
emallocz(unsigned int size)
{
	void *res = calloc(1, size);

	if(!res)
		bad_malloc(size);
	return res;
}

void
eprint(const char *errstr, ...)
{
	va_list ap;

	va_start(ap, errstr);
	vfprintf(stderr, errstr, ap);
	va_end(ap);
	exit(EXIT_FAILURE);
}

char *
estrdup(const char *str)
{
	void *res = strdup(str);
	if(!res)
		bad_malloc(strlen(str));
	return res;
}

void
swap(void **p1, void **p2)
{
	void *tmp = *p1;
	*p1 = *p2;
	*p2 = tmp;
}
