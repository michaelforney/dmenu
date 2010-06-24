/* See LICENSE file for copyright and license details. */
#include <X11/Xlib.h>
#include "draw.h"

#define MAX(a, b)               ((a) > (b) ? (a) : (b))

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
