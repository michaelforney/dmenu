/* See LICENSE file for copyright and license details. */
#include <ctype.h>
#include <locale.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <X11/Xlib.h>
#include "draw.h"

/* macros */
#define MIN(a, b)               ((a) < (b) ? (a) : (b))
#define MAX(a, b)               ((a) > (b) ? (a) : (b))

/* variables */
const char *progname;

void
cleanupdraw(DC *dc) {
	if(dc->font.set)
		XFreeFontSet(dc->dpy, dc->font.set);
	else
		XFreeFont(dc->dpy, dc->font.xfont);
	XFreePixmap(dc->dpy, dc->drawable);
	XFreeGC(dc->dpy, dc->gc);
}

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

void
eprint(const char *fmt, ...) {
	va_list ap;

	fprintf(stderr, "%s: ", progname);
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	exit(EXIT_FAILURE);
}

unsigned long
getcolor(DC *dc, const char *colstr) {
	Colormap cmap = DefaultColormap(dc->dpy, DefaultScreen(dc->dpy));
	XColor color;

	if(!XAllocNamedColor(dc->dpy, cmap, colstr, &color, &color))
		eprint("cannot allocate color '%s'\n", colstr);
	return color.pixel;
}

void
initfont(DC *dc, const char *fontstr) {
	char *def, **missing = NULL;
	int i, n;

	if(!fontstr || !*fontstr)
		eprint("cannot load null font\n");
	dc->font.set = XCreateFontSet(dc->dpy, fontstr, &missing, &n, &def);
	if(missing)
		XFreeStringList(missing);
	if(dc->font.set) {
		XFontStruct **xfonts;
		char **font_names;
		dc->font.ascent = dc->font.descent = 0;
		n = XFontsOfFontSet(dc->font.set, &xfonts, &font_names);
		for(i = 0; i < n; i++) {
			dc->font.ascent = MAX(dc->font.ascent, (*xfonts)->ascent);
			dc->font.descent = MAX(dc->font.descent, (*xfonts)->descent);
			xfonts++;
		}
	}
	else {
		if(!(dc->font.xfont = XLoadQueryFont(dc->dpy, fontstr))
		&& !(dc->font.xfont = XLoadQueryFont(dc->dpy, "fixed")))
			eprint("cannot load font '%s'\n", fontstr);
		dc->font.ascent = dc->font.xfont->ascent;
		dc->font.descent = dc->font.xfont->descent;
	}
	dc->font.height = dc->font.ascent + dc->font.descent;
}

int
textnw(DC *dc, const char *text, unsigned int len) {
	XRectangle r;

	if(dc->font.set) {
		XmbTextExtents(dc->font.set, text, len, NULL, &r);
		return r.width;
	}
	return XTextWidth(dc->font.xfont, text, len);
}

int
textw(DC *dc, const char *text) {
	return textnw(dc, text, strlen(text)) + dc->font.height;
}
