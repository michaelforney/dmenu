/* See LICENSE file for copyright and license details. */
#include <X11/Xlib.h>
#include "draw.h"

void
drawsquare(DC *dc, Bool filled, unsigned long col[ColLast], Bool invert) {
	int n;
	XRectangle r = { dc->x, dc->y, dc->w, dc->h };

	XSetForeground(dc->dpy, dc->gc, col[invert ? ColBG : ColFG]);
	n = ((dc->font.ascent + dc->font.descent + 2) / 4) + (filled ? 1 : 0);
	r.width = r.height = n;
	r.x = dc->x + 1;
	r.y = dc->y + 1;
	if(filled)
		XFillRectangles(dc->dpy, dc->drawable, dc->gc, &r, 1);
	else
		XDrawRectangles(dc->dpy, dc->drawable, dc->gc, &r, 1);
}
