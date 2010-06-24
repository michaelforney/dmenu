/* See LICENSE file for copyright and license details. */
#include <X11/Xlib.h>
#include "draw.h"

void
cleanupdraw(DC *dc) {
	if(dc->font.set)
		XFreeFontSet(dc->dpy, dc->font.set);
	else
		XFreeFont(dc->dpy, dc->font.xfont);
	XFreePixmap(dc->dpy, dc->drawable);
	XFreeGC(dc->dpy, dc->gc);
}
