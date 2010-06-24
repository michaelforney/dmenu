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
extern char *progname;
extern Display *dpy;
extern DC dc;
extern int screen;
extern unsigned int mw, mh;
extern Window parent;

extern const char *font;
extern const char *normbgcolor;
extern const char *normfgcolor;
extern const char *selbgcolor;
extern const char *selfgcolor;
