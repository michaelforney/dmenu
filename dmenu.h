/* (C)opyright MMVI-MMVII Anselm R. Garbe <garbeam at gmail dot com>
 * See LICENSE file for license details.
 */

#include <X11/Xlib.h>

#define FONT			"-*-fixed-medium-r-normal-*-13-*-*-*-*-*-*-*"
#define NORMBGCOLOR		"#eeeeee"
#define NORMFGCOLOR		"#222222"
#define SELBGCOLOR		"#006699"
#define SELFGCOLOR		"#ffffff"
#define SPACE			30 /* px */

/* color */
enum { ColFG, ColBG, ColLast };

typedef struct {
	int x, y, w, h;
	unsigned long norm[ColLast];
	unsigned long sel[ColLast];
	Drawable drawable;
	GC gc;
	struct {
		XFontStruct *xfont;
		XFontSet set;
		int ascent;
		int descent;
		int height;
	} font;
} DC; /* draw context */

extern int screen;
extern Display *dpy;
extern DC dc;			/* global drawing context */

/* draw.c */
extern void drawtext(const char *text, unsigned long col[ColLast]);
extern unsigned int textw(const char *text);
extern unsigned int textnw(const char *text, unsigned int len);

/* util.c */
extern void *emalloc(unsigned int size);		/* allocates memory, exits on error */
extern void eprint(const char *errstr, ...);		/* prints errstr and exits with 1 */
extern char *estrdup(const char *str);			/* duplicates str, exits on allocation error */
