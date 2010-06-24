/* See LICENSE file for copyright and license details. */
#include <X11/Xlib.h>
#include "draw.h"

unsigned long
getcolor(DC *dc, const char *colstr) {
	Colormap cmap = DefaultColormap(dc->dpy, DefaultScreen(dc->dpy));
	XColor color;

	if(!XAllocNamedColor(dc->dpy, cmap, colstr, &color, &color))
		eprint("cannot allocate color '%s'\n", colstr);
	return color.pixel;
}
