/* See LICENSE file for copyright and license details. */

#define FG(dc, col)  ((col)[(dc)->invert ? ColBG : ColFG])
#define BG(dc, col)  ((col)[(dc)->invert ? ColFG : ColBG])

enum { ColBG, ColFG, ColBorder, ColLast };

typedef struct {
	int x, y, w, h;
	bool invert;
	struct wl_display *dpy;
	struct wld_wayland_context *ctx;
	struct wld_drawable *drawable;
	struct wld_font_context *fontctx;
	struct wld_font *font;
} DC;  /* draw context */

void drawrect(DC *dc, int x, int y, unsigned int w, unsigned int h, bool fill, unsigned long color);
void drawtext(DC *dc, const char *text, unsigned long col[ColLast]);
void drawtextn(DC *dc, const char *text, size_t n, unsigned long col[ColLast]);
void eprintf(const char *fmt, ...);
void freedc(DC *dc);
unsigned long getcolor(DC *dc, const char *colstr);
DC *initdc(void);
void initfont(DC *dc, const char *fontstr);
void mapdc(DC *dc, struct wl_surface *surface, unsigned int w, unsigned int h);
void resizedc(DC *dc, struct wl_surface *surface, unsigned int w, unsigned int h);
int textnw(DC *dc, const char *text, size_t len);
int textw(DC *dc, const char *text);
