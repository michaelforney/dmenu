/*
 * (C)opyright MMVI Anselm R. Garbe <garbeam at gmail dot com>
 * See LICENSE file for license details.
 */

#include "config.h"
#include <X11/Xlib.h>
#include <X11/Xlocale.h>

#define SPACE		30 /* px */

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

extern int screen;
extern Display *dpy;
extern DC dc;

/* draw.c */
extern void drawtext(const char *text, Bool invert, Bool border);
extern unsigned long getcolor(const char *colstr);
extern void setfont(const char *fontstr);
extern unsigned int textw(const char *text);

/* util.c */
extern void *emalloc(unsigned int size);
extern void eprint(const char *errstr, ...);
extern char *estrdup(const char *str);
