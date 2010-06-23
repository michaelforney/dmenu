/* See LICENSE file for copyright and license details. */

/* enums */
enum { ColFG, ColBG, ColLast };

/* typedefs */
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

/* forward declarations */
void drawcleanup(void);
void drawsetup(void);
void drawtext(const char *text, unsigned long col[ColLast]);
void eprint(const char *errstr, ...);
unsigned long getcolor(const char *colstr);
void initfont(const char *fontstr);
int textnw(const char *text, unsigned int len);
int textw(const char *text);

/* variables */
Display *dpy;
DC dc;
int screen;
unsigned int mw, mh;
unsigned int spaceitem;
Window parent;

/* style */
const char *font;
const char *normbgcolor;
const char *normfgcolor;
const char *selbgcolor;
const char *selfgcolor;
