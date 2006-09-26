/*
 * (C)opyright MMVI Anselm R. Garbe <garbeam at gmail dot com>
 * See LICENSE file for license details.
 */

#include <X11/Xlib.h>
#include <X11/Xlocale.h>

#define FONT			"fixed"
#define NORMBGCOLOR		"#333366"
#define NORMFGCOLOR		"#cccccc"
#define SELBGCOLOR		"#666699"
#define SELFGCOLOR		"#eeeeee"
#define SPACE		30 /* px */

/* color */
enum { ColFG, ColBG, ColLast };

typedef struct DC DC;
typedef struct Fnt Fnt;

struct Fnt {
	XFontStruct *xfont;
	XFontSet set;
	int ascent;
	int descent;
	int height;
};

struct DC {
	int x, y, w, h;
	unsigned long norm[ColLast];
	unsigned long sel[ColLast];
	Drawable drawable;
	Fnt font;
	GC gc;
}; /* draw context */

extern int screen;
extern Display *dpy;
extern DC dc;			/* global drawing context */

/* draw.c */
extern void drawtext(const char *text,
			unsigned long col[ColLast]);	/* draws text with the defined color tuple */
extern unsigned long getcolor(const char *colstr);	/* returns color of colstr */
extern void setfont(const char *fontstr);		/* sets global font */
extern unsigned int textw(const char *text);		/* returns width of text in px */

/* util.c */
extern void *emalloc(unsigned int size);		/* allocates memory, exits on error */
extern void eprint(const char *errstr, ...);		/* prints errstr and exits with 1 */
extern char *estrdup(const char *str);			/* duplicates str, exits on allocation error */
