/*
 * (C)opyright MMVI Anselm R. Garbe <garbeam at gmail dot com>
 * See LICENSE file for license details.
 */

#include "config.h"
#include <X11/Xlib.h>
#include <X11/Xlocale.h>

typedef struct Brush Brush;
typedef struct DC DC;
typedef struct Fnt Fnt;

struct Fnt {
	XFontStruct *xfont;
	XFontSet set;
	int ascent;
	int descent;
	int height;
};

struct DC { /* draw context */
	int x, y, w, h;
	unsigned long bg;
	unsigned long fg;
	unsigned long border;
	Drawable drawable;
	Fnt font;
	GC gc;
};

struct Brush {
	GC gc;
	Drawable drawable;
	int x, y, w, h;
	Fnt font;
	unsigned long bg;
	unsigned long fg;
	unsigned long border;
};



/* draw.c */
extern void draw(Display *dpy, Brush *b, Bool border, const char *text);
extern void loadcolors(Display *dpy, int screen, Brush *b,
		const char *bg, const char *fg, const char *bo);
extern void loadfont(Display *dpy, Fnt *font, const char *fontstr);
extern unsigned int textnw(Fnt *font, char *text, unsigned int len);
extern unsigned int textw(Fnt *font, char *text);
extern unsigned int texth(Fnt *font);

/* util.c */
extern void *emalloc(unsigned int size);
extern void *emallocz(unsigned int size);
extern void eprint(const char *errstr, ...);
extern char *estrdup(const char *str);
extern void swap(void **p1, void **p2);
