/* See LICENSE file for copyright and license details. */
#include <string.h>
#include <X11/Xlib.h>
#include "draw.h"

#define MIN(a, b)               ((a) < (b) ? (a) : (b))

void
drawtext(DC *dc, const char *text, unsigned long col[ColLast]) {
	char buf[256];
	int i, x, y, h, len, olen;
	XRectangle r = { dc->x, dc->y, dc->w, dc->h };

	XSetForeground(dc->dpy, dc->gc, col[ColBG]);
	XFillRectangles(dc->dpy, dc->drawable, dc->gc, &r, 1);
	if(!text)
		return;
	olen = strlen(text);
	h = dc->font.height;
	y = dc->y + ((h+2) / 2) - (h / 2) + dc->font.ascent;
	x = dc->x + (h / 2);
	/* shorten text if necessary */
	for(len = MIN(olen, sizeof buf); len && textnw(dc, text, len) > dc->w - h; len--);
	if(!len)
		return;
	memcpy(buf, text, len);
	if(len < olen)
		for(i = len; i && i > len - 3; buf[--i] = '.');
	XSetForeground(dc->dpy, dc->gc, col[ColFG]);
	if(dc->font.set)
		XmbDrawString(dc->dpy, dc->drawable, dc->font.set, dc->gc, x, y, buf, len);
	else
		XDrawString(dc->dpy, dc->drawable, dc->gc, x, y, buf, len);
}
