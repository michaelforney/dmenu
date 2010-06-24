/* See LICENSE file for copyright and license details. */
#include <string.h>
#include <X11/Xlib.h>
#include "draw.h"

int
textw(DC *dc, const char *text) {
	return textnw(dc, text, strlen(text)) + dc->font.height;
}
