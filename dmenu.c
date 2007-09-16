/* See LICENSE file for copyright and license details. */
#include <ctype.h>
#include <locale.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>

/* macros */
#define CLEANMASK(mask) (mask & ~(numlockmask | LockMask))

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

typedef struct Item Item;
struct Item {
	Item *next;		/* traverses all items */
	Item *left, *right;	/* traverses items matching current search pattern */
	char *text;
};

/* forward declarations */
static void *emalloc(unsigned int size);
static void eprint(const char *errstr, ...);
static char *estrdup(const char *str);
static void drawtext(const char *text, unsigned long col[ColLast]);
static unsigned int textw(const char *text);
static unsigned int textnw(const char *text, unsigned int len);
static void calcoffsets(void);
static void drawmenu(void);
static Bool grabkeyboard(void);
static unsigned long getcolor(const char *colstr);
static void initfont(const char *fontstr);
static int strido(const char *text, const char *pattern);
static void match(char *pattern);
static void kpress(XKeyEvent * e);
static char *readstdin(void);
static void usage(void);


/* variables */
static int screen;
static Display *dpy;
static DC dc = {0};
static char text[4096];
static char *prompt = NULL;
static int mw, mh;
static int ret = 0;
static int nitem = 0;
static unsigned int cmdw = 0;
static unsigned int promptw = 0;
static unsigned int numlockmask = 0;
static Bool running = True;
static Item *allitems = NULL;	/* first of all items */
static Item *item = NULL;	/* first of pattern matching items */
static Item *sel = NULL;
static Item *next = NULL;
static Item *prev = NULL;
static Item *curr = NULL;
static Window root;
static Window win;

#include "config.h"

static void *
emalloc(unsigned int size) {
	void *res = malloc(size);

	if(!res)
		eprint("fatal: could not malloc() %u bytes\n", size);
	return res;
}

static void
eprint(const char *errstr, ...) {
	va_list ap;

	va_start(ap, errstr);
	vfprintf(stderr, errstr, ap);
	va_end(ap);
	exit(EXIT_FAILURE);
}

static char *
estrdup(const char *str) {
	void *res = strdup(str);

	if(!res)
		eprint("fatal: could not malloc() %u bytes\n", strlen(str));
	return res;
}


static void
drawtext(const char *text, unsigned long col[ColLast]) {
	int x, y, w, h;
	static char buf[256];
	unsigned int len, olen;
	XRectangle r = { dc.x, dc.y, dc.w, dc.h };

	XSetForeground(dpy, dc.gc, col[ColBG]);
	XFillRectangles(dpy, dc.drawable, dc.gc, &r, 1);
	if(!text)
		return;
	w = 0;
	olen = len = strlen(text);
	if(len >= sizeof buf)
		len = sizeof buf - 1;
	memcpy(buf, text, len);
	buf[len] = 0;
	h = dc.font.ascent + dc.font.descent;
	y = dc.y + (dc.h / 2) - (h / 2) + dc.font.ascent;
	x = dc.x + (h / 2);
	/* shorten text if necessary */
	while(len && (w = textnw(buf, len)) > dc.w - h)
		buf[--len] = 0;
	if(len < olen) {
		if(len > 1)
			buf[len - 1] = '.';
		if(len > 2)
			buf[len - 2] = '.';
		if(len > 3)
			buf[len - 3] = '.';
	}
	if(w > dc.w)
		return; /* too long */
	XSetForeground(dpy, dc.gc, col[ColFG]);
	if(dc.font.set)
		XmbDrawString(dpy, dc.drawable, dc.font.set, dc.gc, x, y, buf, len);
	else
		XDrawString(dpy, dc.drawable, dc.gc, x, y, buf, len);
}

static unsigned int
textw(const char *text) {
	return textnw(text, strlen(text)) + dc.font.height;
}

static unsigned int
textnw(const char *text, unsigned int len) {
	XRectangle r;

	if(dc.font.set) {
		XmbTextExtents(dc.font.set, text, len, NULL, &r);
		return r.width;
	}
	return XTextWidth(dc.font.xfont, text, len);
}

static void
calcoffsets(void) {
	unsigned int tw, w;

	if(!curr)
		return;
	w = promptw + cmdw + 2 * SPACE;
	for(next = curr; next; next=next->right) {
		tw = textw(next->text);
		if(tw > mw / 3)
			tw = mw / 3;
		w += tw;
		if(w > mw)
			break;
	}
	w = promptw + cmdw + 2 * SPACE;
	for(prev = curr; prev && prev->left; prev=prev->left) {
		tw = textw(prev->left->text);
		if(tw > mw / 3)
			tw = mw / 3;
		w += tw;
		if(w > mw)
			break;
	}
}

static void
drawmenu(void) {
	Item *i;

	dc.x = 0;
	dc.y = 0;
	dc.w = mw;
	dc.h = mh;
	drawtext(NULL, dc.norm);
	/* print prompt? */
	if(promptw) {
		dc.w = promptw;
		drawtext(prompt, dc.sel);
	}
	dc.x += promptw;
	dc.w = mw - promptw;
	/* print command */
	if(cmdw && item)
		dc.w = cmdw;
	drawtext(text[0] ? text : NULL, dc.norm);
	dc.x += cmdw;
	if(curr) {
		dc.w = SPACE;
		drawtext((curr && curr->left) ? "<" : NULL, dc.norm);
		dc.x += dc.w;
		/* determine maximum items */
		for(i = curr; i != next; i=i->right) {
			dc.w = textw(i->text);
			if(dc.w > mw / 3)
				dc.w = mw / 3;
			drawtext(i->text, (sel == i) ? dc.sel : dc.norm);
			dc.x += dc.w;
		}
		dc.x = mw - SPACE;
		dc.w = SPACE;
		drawtext(next ? ">" : NULL, dc.norm);
	}
	XCopyArea(dpy, dc.drawable, win, dc.gc, 0, 0, mw, mh, 0, 0);
	XFlush(dpy);
}

static Bool
grabkeyboard(void) {
	unsigned int len;

	for(len = 1000; len; len--) {
		if(XGrabKeyboard(dpy, root, True, GrabModeAsync, GrabModeAsync, CurrentTime)
			== GrabSuccess)
			break;
		usleep(1000);
	}
	return len > 0;
}

static unsigned long
getcolor(const char *colstr) {
	Colormap cmap = DefaultColormap(dpy, screen);
	XColor color;

	if(!XAllocNamedColor(dpy, cmap, colstr, &color, &color))
		eprint("error, cannot allocate color '%s'\n", colstr);
	return color.pixel;
}

static void
initfont(const char *fontstr) {
	char *def, **missing;
	int i, n;

	if(!fontstr || fontstr[0] == '\0')
		eprint("error, cannot load font: '%s'\n", fontstr);
	missing = NULL;
	if(dc.font.set)
		XFreeFontSet(dpy, dc.font.set);
	dc.font.set = XCreateFontSet(dpy, fontstr, &missing, &n, &def);
	if(missing)
		XFreeStringList(missing);
	if(dc.font.set) {
		XFontSetExtents *font_extents;
		XFontStruct **xfonts;
		char **font_names;
		dc.font.ascent = dc.font.descent = 0;
		font_extents = XExtentsOfFontSet(dc.font.set);
		n = XFontsOfFontSet(dc.font.set, &xfonts, &font_names);
		for(i = 0, dc.font.ascent = 0, dc.font.descent = 0; i < n; i++) {
			if(dc.font.ascent < (*xfonts)->ascent)
				dc.font.ascent = (*xfonts)->ascent;
			if(dc.font.descent < (*xfonts)->descent)
				dc.font.descent = (*xfonts)->descent;
			xfonts++;
		}
	}
	else {
		if(dc.font.xfont)
			XFreeFont(dpy, dc.font.xfont);
		dc.font.xfont = NULL;
		if(!(dc.font.xfont = XLoadQueryFont(dpy, fontstr))) {
			if(!(dc.font.xfont = XLoadQueryFont(dpy, "fixed")))
				eprint("error, cannot load font: '%s'\n", fontstr);
		}
		dc.font.ascent = dc.font.xfont->ascent;
		dc.font.descent = dc.font.xfont->descent;
	}
	dc.font.height = dc.font.ascent + dc.font.descent;
}

static int
strido(const char *text, const char *pattern) {
	for(; *text && *pattern; text++)
		if (*text == *pattern)
			pattern++;
	return !*pattern;
}                                  

static void
match(char *pattern) {
	unsigned int plen;
	Item *i, *j;

	if(!pattern)
		return;
	plen = strlen(pattern);
	item = j = NULL;
	nitem = 0;
	for(i = allitems; i; i=i->next)
		if(!plen || !strncmp(pattern, i->text, plen)) {
			if(!j)
				item = i;
			else
				j->right = i;
			i->left = j;
			i->right = NULL;
			j = i;
			nitem++;
		}
	for(i = allitems; i; i=i->next)
		if(plen && strncmp(pattern, i->text, plen)
				&& strstr(i->text, pattern)) {
			if(!j)                               
				item = i;                              
			else                                     
				j->right = i;                          
			i->left = j;      
			i->right = NULL;                         
			j = i;                                      
			nitem++;                                       
		}                                              
	for(i = allitems; i; i=i->next)                            
		if(plen && strncmp(pattern, i->text, plen)             
				&& !strstr(i->text, pattern)          
				&& strido(i->text,pattern)) { 
			if(!j)
				item = i;
			else
				j->right = i;
			i->left = j;
			i->right = NULL;
			j = i;
			nitem++;
		}
	curr = prev = next = sel = item;
	calcoffsets();
}

static void
kpress(XKeyEvent * e) {
	char buf[32];
	int i, num;
	unsigned int len;
	KeySym ksym;

	len = strlen(text);
	buf[0] = 0;
	num = XLookupString(e, buf, sizeof buf, &ksym, 0);
	if(IsKeypadKey(ksym)) { 
		if(ksym == XK_KP_Enter) {
			ksym = XK_Return;
		} else if(ksym >= XK_KP_0 && ksym <= XK_KP_9) {
			ksym = (ksym - XK_KP_0) + XK_0;
		}
	}
	if(IsFunctionKey(ksym) || IsKeypadKey(ksym)
			|| IsMiscFunctionKey(ksym) || IsPFKey(ksym)
			|| IsPrivateKeypadKey(ksym))
		return;
	/* first check if a control mask is omitted */
	if(e->state & ControlMask) {
		switch (ksym) {
		default:	/* ignore other control sequences */
			return;
		case XK_bracketleft:
			ksym = XK_Escape;
			break;
		case XK_h:
		case XK_H:
			ksym = XK_BackSpace;
			break;
		case XK_i:
		case XK_I:
			ksym = XK_Tab;
			break;
		case XK_j:
		case XK_J:
			ksym = XK_Return;
			break;
		case XK_u:
		case XK_U:
			text[0] = 0;
			match(text);
			drawmenu();
			return;
		case XK_w:
		case XK_W:
			if(len) {
				i = len - 1;
				while(i >= 0 && text[i] == ' ')
					text[i--] = 0;
				while(i >= 0 && text[i] != ' ')
					text[i--] = 0;
				match(text);
				drawmenu();
			}
			return;
		}
	}
	if(CLEANMASK(e->state) & Mod1Mask) {
		switch(ksym) {
		default: return;
		case XK_h:
			ksym = XK_Left;
			break;
		case XK_l:
			ksym = XK_Right;
			break;
		case XK_j:
			ksym = XK_Next;
			break;
		case XK_k:
			ksym = XK_Prior;
			break;
		case XK_g:
			ksym = XK_Home;
			break;
		case XK_G:
			ksym = XK_End;
			break;
		}
	}
	switch(ksym) {
	default:
		if(num && !iscntrl((int) buf[0])) {
			buf[num] = 0;
			if(len > 0)
				strncat(text, buf, sizeof text);
			else
				strncpy(text, buf, sizeof text);
			match(text);
		}
		break;
	case XK_BackSpace:
		if(len) {
			text[--len] = 0;
			match(text);
		}
		break;
	case XK_End:
		if(!item)
			return;
		while(next) {
			sel = curr = next;
			calcoffsets();
		}
		while(sel && sel->right)
			sel = sel->right;
		break;
	case XK_Escape:
		ret = 1;
		running = False;
		break;
	case XK_Home:
		if(!item)
			return;
		sel = curr = item;
		calcoffsets();
		break;
	case XK_Left:
		if(!(sel && sel->left))
			return;
		sel=sel->left;
		if(sel->right == curr) {
			curr = prev;
			calcoffsets();
		}
		break;
	case XK_Next:
		if(!next)
			return;
		sel = curr = next;
		calcoffsets();
		break;
	case XK_Prior:
		if(!prev)
			return;
		sel = curr = prev;
		calcoffsets();
		break;
	case XK_Return:
		if((e->state & ShiftMask) && text)
			fprintf(stdout, "%s", text);
		else if(sel)
			fprintf(stdout, "%s", sel->text);
		else if(text)
			fprintf(stdout, "%s", text);
		fflush(stdout);
		running = False;
		break;
	case XK_Right:
		if(!(sel && sel->right))
			return;
		sel=sel->right;
		if(sel == next) {
			curr = next;
			calcoffsets();
		}
		break;
	case XK_Tab:
		if(!sel)
			return;
		strncpy(text, sel->text, sizeof text);
		match(text);
		break;
	}
	drawmenu();
}

static char *
readstdin(void) {
	static char *maxname = NULL;
	char *p, buf[1024];
	unsigned int len = 0, max = 0;
	Item *i, *new;

	i = 0;
	while(fgets(buf, sizeof buf, stdin)) {
		len = strlen(buf);
		if (buf[len - 1] == '\n')
			buf[len - 1] = 0;
		p = estrdup(buf);
		if(max < len) {
			maxname = p;
			max = len;
		}
		new = emalloc(sizeof(Item));
		new->next = new->left = new->right = NULL;
		new->text = p;
		if(!i)
			allitems = new;
		else 
			i->next = new;
		i = new;
	}

	return maxname;
}

static void
usage(void) {
	eprint("usage: dmenu [-b] [-fn <font>] [-nb <color>] [-nf <color>]\n"
		"             [-p <prompt>] [-sb <color>] [-sf <color>] [-v]\n");
}

int
main(int argc, char *argv[]) {
	Bool bottom = False;
	char *font = FONT;
	char *maxname;
	char *normbg = NORMBGCOLOR;
	char *normfg = NORMFGCOLOR;
	char *selbg = SELBGCOLOR;
	char *selfg = SELFGCOLOR;
	int i, j;
	Item *itm;
	XEvent ev;
	XModifierKeymap *modmap;
	XSetWindowAttributes wa;

	/* command line args */
	for(i = 1; i < argc; i++)
		if(!strcmp(argv[i], "-b")) {
			bottom = True;
		}
		else if(!strcmp(argv[i], "-fn")) {
			if(++i < argc) font = argv[i];
		}
		else if(!strcmp(argv[i], "-nb")) {
			if(++i < argc) normbg = argv[i];
		}
		else if(!strcmp(argv[i], "-nf")) {
			if(++i < argc) normfg = argv[i];
		}
		else if(!strcmp(argv[i], "-p")) {
			if(++i < argc) prompt = argv[i];
		}
		else if(!strcmp(argv[i], "-sb")) {
			if(++i < argc) selbg = argv[i];
		}
		else if(!strcmp(argv[i], "-sf")) {
			if(++i < argc) selfg = argv[i];
		}
		else if(!strcmp(argv[i], "-v"))
			eprint("dmenu-"VERSION", © 2006-2007 Anselm R. Garbe, Sander van Dijk\n");
		else
			usage();
	setlocale(LC_CTYPE, "");
	dpy = XOpenDisplay(0);
	if(!dpy)
		eprint("dmenu: cannot open display\n");
	screen = DefaultScreen(dpy);
	root = RootWindow(dpy, screen);
	if(isatty(STDIN_FILENO)) {
		maxname = readstdin();
		running = grabkeyboard();
	}
	else { /* prevent keypress loss */
		running = grabkeyboard();
		maxname = readstdin();
	}
	/* init modifier map */
	modmap = XGetModifierMapping(dpy);
	for (i = 0; i < 8; i++) {
		for (j = 0; j < modmap->max_keypermod; j++) {
			if(modmap->modifiermap[i * modmap->max_keypermod + j]
			== XKeysymToKeycode(dpy, XK_Num_Lock))
				numlockmask = (1 << i);
		}
	}
	XFreeModifiermap(modmap);
	/* style */
	dc.norm[ColBG] = getcolor(normbg);
	dc.norm[ColFG] = getcolor(normfg);
	dc.sel[ColBG] = getcolor(selbg);
	dc.sel[ColFG] = getcolor(selfg);
	initfont(font);
	/* menu window */
	wa.override_redirect = 1;
	wa.background_pixmap = ParentRelative;
	wa.event_mask = ExposureMask | ButtonPressMask | KeyPressMask;
	mw = DisplayWidth(dpy, screen);
	mh = dc.font.height + 2;
	win = XCreateWindow(dpy, root, 0,
			bottom ? DisplayHeight(dpy, screen) - mh : 0, mw, mh, 0,
			DefaultDepth(dpy, screen), CopyFromParent,
			DefaultVisual(dpy, screen),
			CWOverrideRedirect | CWBackPixmap | CWEventMask, &wa);
	/* pixmap */
	dc.drawable = XCreatePixmap(dpy, root, mw, mh, DefaultDepth(dpy, screen));
	dc.gc = XCreateGC(dpy, root, 0, 0);
	XSetLineAttributes(dpy, dc.gc, 1, LineSolid, CapButt, JoinMiter);
	if(!dc.font.set)
		XSetFont(dpy, dc.gc, dc.font.xfont->fid);
	if(maxname)
		cmdw = textw(maxname);
	if(cmdw > mw / 3)
		cmdw = mw / 3;
	if(prompt)
		promptw = textw(prompt);
	if(promptw > mw / 5)
		promptw = mw / 5;
	text[0] = 0;
	match(text);
	XMapRaised(dpy, win);
	drawmenu();
	XSync(dpy, False);

	/* main event loop */
	while(running && !XNextEvent(dpy, &ev))
		switch (ev.type) {
		default:	/* ignore all crap */
			break;
		case KeyPress:
			kpress(&ev.xkey);
			break;
		case Expose:
			if(ev.xexpose.count == 0)
				drawmenu();
			break;
		}

	/* cleanup */
	while(allitems) {
		itm = allitems->next;
		free(allitems->text);
		free(allitems);
		allitems = itm;
	}
	if(dc.font.set)
		XFreeFontSet(dpy, dc.font.set);
	else
		XFreeFont(dpy, dc.font.xfont);
	XFreePixmap(dpy, dc.drawable);
	XFreeGC(dpy, dc.gc);
	XDestroyWindow(dpy, win);
	XUngrabKeyboard(dpy, CurrentTime);
	XCloseDisplay(dpy);
	return ret;
}
