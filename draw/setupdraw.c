/* See LICENSE file for copyright and license details. */
#include <X11/Xlib.h>
#include "draw.h"

void
setupdraw(DC *dc, Window w) {
	XWindowAttributes wa;

	XGetWindowAttributes(dc->dpy, w, &wa);
	dc->drawable = XCreatePixmap(dc->dpy, w, wa.width, wa.height,
		DefaultDepth(dc->dpy, DefaultScreen(dc->dpy)));
	dc->gc = XCreateGC(dc->dpy, w, 0, NULL);
	XSetLineAttributes(dc->dpy, dc->gc, 1, LineSolid, CapButt, JoinMiter);
	if(!dc->font.set)
		XSetFont(dc->dpy, dc->gc, dc->font.xfont->fid);
}
