/* See LICENSE file for copyright and license details. */

typedef void Cur;

typedef struct Fnt {
	struct wld_font *wld;
	FcPattern *pattern;
	struct Fnt *next;
} Fnt;

enum { ColFg, ColBg }; /* Clr scheme index */
typedef uint32_t Clr;

typedef struct {
	unsigned int w, h;
	struct wl_display *dpy;
	struct wld_context *ctx;
	struct wld_renderer *renderer;
	struct wld_surface *surface;
	struct wld_font_context *fontctx;
	Clr *scheme;
	Fnt *fonts;
} Drw;

/* Drawable abstraction */
Drw *drw_create(struct wl_display *dpy);
void drw_resize(Drw *drw, struct wl_surface *surface, unsigned int w, unsigned int h);
void drw_free(Drw *drw);

/* Fnt abstraction */
Fnt *drw_fontset_create(Drw* drw, const char *fonts[], size_t fontcount);
void drw_fontset_free(Fnt* set);
unsigned int drw_fontset_getwidth(Drw *drw, const char *text);
void drw_font_getexts(Fnt *font, const char *text, unsigned int len, unsigned int *w, unsigned int *h);

/* Colorscheme abstraction */
void drw_clr_create(Drw *drw, Clr *dest, const char *clrname);
Clr *drw_scm_create(Drw *drw, const char *clrnames[], size_t clrcount);

/* Cursor abstraction */
Cur *drw_cur_create(Drw *drw, int shape);
void drw_cur_free(Drw *drw, Cur *cursor);

/* Drawing context manipulation */
void drw_setfontset(Drw *drw, Fnt *set);
void drw_setscheme(Drw *drw, Clr *scm);

/* Drawing functions */
void drw_rect(Drw *drw, int x, int y, unsigned int w, unsigned int h, int filled, int invert);
int drw_text(Drw *drw, int x, int y, unsigned int w, unsigned int h, unsigned int lpad, const char *text, int invert);

/* Map functions */
void drw_map(Drw *drw, struct wl_surface *surface, int x, int y, unsigned int w, unsigned int h);
