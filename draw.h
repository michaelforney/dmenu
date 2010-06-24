/* See LICENSE file for copyright and license details. */

/* enums */
enum { ColFG, ColBG, ColLast };

/* typedefs */
typedef struct {
	int x, y, w, h;
	Drawable drawable;
	Display *dpy;
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
void cleanupdraw(DC *dc);
void setupdraw(DC *dc, Window w);
void drawtext(DC *dc, const char *text, unsigned long col[ColLast]);
void eprint(const char *fmt, ...);
unsigned long getcolor(DC *dc, const char *colstr);
void initfont(DC *dc, const char *fontstr);
int textnw(DC *dc, const char *text, unsigned int len);
int textw(DC *dc, const char *text);

/* variables */
extern const char *progname;
