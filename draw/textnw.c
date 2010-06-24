/* See LICENSE file for copyright and license details. */
#include <X11/Xlib.h>
#include "draw.h"

int
textnw(DC *dc, const char *text, unsigned int len) {
	XRectangle r;

	if(dc->font.set) {
		XmbTextExtents(dc->font.set, text, len, NULL, &r);
		return r.width;
	}
	return XTextWidth(dc->font.xfont, text, len);
}
