/* See LICENSE file for copyright and license details. */
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>
#ifdef XINERAMA
#include <X11/extensions/Xinerama.h>
#endif
#include "draw.h"

#define INRECT(x,y,rx,ry,rw,rh) ((x) >= (rx) && (x) < (rx)+(rw) && (y) >= (ry) && (y) < (ry)+(rh))
#define MIN(a,b)                ((a) < (b) ? (a) : (b))
#define MAX(a,b)                ((a) > (b) ? (a) : (b))

typedef struct Item Item;
struct Item {
	char *text;
	Item *left, *right;
};

static void appenditem(Item *item, Item **list, Item **last);
static void calcoffsets(void);
static void drawmenu(void);
static char *fstrstr(const char *s, const char *sub);
static void grabkeyboard(void);
static void insert(const char *str, ssize_t n);
static void keypress(XKeyEvent *ev);
static void match(Bool sub);
static size_t nextrune(int inc);
static void paste(void);
static void readstdin(void);
static void run(void);
static void setup(void);
static void usage(void);

static char text[BUFSIZ] = "";
static int bh, mw, mh;
static int inputw, promptw;
static int lines = 0;
static size_t cursor = 0;
static const char *font = NULL;
static const char *prompt = NULL;
static const char *normbgcolor = "#cccccc";
static const char *normfgcolor = "#000000";
static const char *selbgcolor  = "#0066ff";
static const char *selfgcolor  = "#ffffff";
static unsigned long normcol[ColLast];
static unsigned long selcol[ColLast];
static Atom utf8;
static Bool topbar = True;
static DC *dc;
static Item *items = NULL;
static Item *matches, *matchend;
static Item *prev, *curr, *next, *sel;
static Window win;

static int (*fstrncmp)(const char *, const char *, size_t) = strncmp;

int
main(int argc, char *argv[]) {
	Bool fast = False;
	int i;

	for(i = 1; i < argc; i++)
		/* single flags */
		if(!strcmp(argv[i], "-v")) {
			puts("dmenu-"VERSION", © 2006-2011 dmenu engineers, see LICENSE for details");
			exit(EXIT_SUCCESS);
		}
		else if(!strcmp(argv[i], "-b"))
			topbar = False;
		else if(!strcmp(argv[i], "-f"))
			fast = True;
		else if(!strcmp(argv[i], "-i"))
			fstrncmp = strncasecmp;
		else if(i+1 == argc)
			usage();
		/* double flags */
		else if(!strcmp(argv[i], "-l"))
			lines = atoi(argv[++i]);
		else if(!strcmp(argv[i], "-p"))
			prompt = argv[++i];
		else if(!strcmp(argv[i], "-fn"))
			font = argv[++i];
		else if(!strcmp(argv[i], "-nb"))
			normbgcolor = argv[++i];
		else if(!strcmp(argv[i], "-nf"))
			normfgcolor = argv[++i];
		else if(!strcmp(argv[i], "-sb"))
			selbgcolor = argv[++i];
		else if(!strcmp(argv[i], "-sf"))
			selfgcolor = argv[++i];
		else
			usage();

	dc = initdc();
	initfont(dc, font);

	if(fast) {
		grabkeyboard();
		readstdin();
	}
	else {
		readstdin();
		grabkeyboard();
	}
	setup();
	run();

	return EXIT_FAILURE;  /* should not reach */
}

void
appenditem(Item *item, Item **list, Item **last) {
	if(!*last)
		*list = item;
	else
		(*last)->right = item;
	item->left = *last;
	item->right = NULL;
	*last = item;
}

void
calcoffsets(void) {
	int i, n;

	if(lines > 0)
		n = lines * bh;
	else
		n = mw - (promptw + inputw + textw(dc, "<") + textw(dc, ">"));

	for(i = 0, next = curr; next; next = next->right)
		if((i += (lines > 0) ? bh : MIN(textw(dc, next->text), n)) > n)
			break;
	for(i = 0, prev = curr; prev && prev->left; prev = prev->left)
		if((i += (lines > 0) ? bh : MIN(textw(dc, prev->left->text), n)) > n)
			break;
}

void
drawmenu(void) {
	int curpos;
	Item *item;

	dc->x = 0;
	dc->y = 0;
	dc->h = bh;
	drawrect(dc, 0, 0, mw, mh, True, BG(dc, normcol));

	if(prompt) {
		dc->w = promptw;
		drawtext(dc, prompt, selcol);
		dc->x = dc->w;
	}
	dc->w = (lines > 0 || !matches) ? mw - dc->x : inputw;
	drawtext(dc, text, normcol);
	if((curpos = textnw(dc, text, cursor) + dc->h/2 - 2) < dc->w)
		drawrect(dc, curpos, 2, 1, dc->h - 4, True, FG(dc, normcol));

	if(lines > 0) {
		dc->w = mw - dc->x;
		for(item = curr; item != next; item = item->right) {
			dc->y += dc->h;
			drawtext(dc, item->text, (item == sel) ? selcol : normcol);
		}
	}
	else if(matches) {
		dc->x += inputw;
		dc->w = textw(dc, "<");
		if(curr->left)
			drawtext(dc, "<", normcol);
		for(item = curr; item != next; item = item->right) {
			dc->x += dc->w;
			dc->w = MIN(textw(dc, item->text), mw - dc->x - textw(dc, ">"));
			drawtext(dc, item->text, (item == sel) ? selcol : normcol);
		}
		dc->w = textw(dc, ">");
		dc->x = mw - dc->w;
		if(next)
			drawtext(dc, ">", normcol);
	}
	mapdc(dc, win, mw, mh);
}

char *
fstrstr(const char *s, const char *sub) {
	size_t len;

	for(len = strlen(sub); *s; s++)
		if(!fstrncmp(s, sub, len))
			return (char *)s;
	return NULL;
}

void
grabkeyboard(void) {
	int i;

	for(i = 0; i < 1000; i++) {
		if(XGrabKeyboard(dc->dpy, DefaultRootWindow(dc->dpy), True,
		                 GrabModeAsync, GrabModeAsync, CurrentTime) == GrabSuccess)
			return;
		usleep(1000);
	}
	eprintf("cannot grab keyboard\n");
}

void
insert(const char *str, ssize_t n) {
	if(strlen(text) + n > sizeof text - 1)
		return;
	memmove(&text[cursor + n], &text[cursor], sizeof text - cursor - MAX(n, 0));
	if(n > 0)
		memcpy(&text[cursor], str, n);
	cursor += n;
	match(n > 0 && text[cursor] == '\0');
}

void
keypress(XKeyEvent *ev) {
	char buf[32];
	KeySym ksym;

	XLookupString(ev, buf, sizeof buf, &ksym, NULL);
	if(ev->state & ControlMask) {
		KeySym lower, upper;

		XConvertCase(ksym, &lower, &upper);
		switch(lower) {
		default:
			return;
		case XK_a:
			ksym = XK_Home;
			break;
		case XK_b:
			ksym = XK_Left;
			break;
		case XK_c:
			ksym = XK_Escape;
			break;
		case XK_d:
			ksym = XK_Delete;
			break;
		case XK_e:
			ksym = XK_End;
			break;
		case XK_f:
			ksym = XK_Right;
			break;
		case XK_h:
			ksym = XK_BackSpace;
			break;
		case XK_i:
			ksym = XK_Tab;
			break;
		case XK_j:
			ksym = XK_Return;
			break;
		case XK_k:  /* delete right */
			text[cursor] = '\0';
			match(False);
			break;
		case XK_n:
			ksym = XK_Down;
			break;
		case XK_p:
			ksym = XK_Up;
			break;
		case XK_u:  /* delete left */
			insert(NULL, 0 - cursor);
			break;
		case XK_w:  /* delete word */
			while(cursor > 0 && text[nextrune(-1)] == ' ')
				insert(NULL, nextrune(-1) - cursor);
			while(cursor > 0 && text[nextrune(-1)] != ' ')
				insert(NULL, nextrune(-1) - cursor);
			break;
		case XK_y:  /* paste selection */
			XConvertSelection(dc->dpy, XA_PRIMARY, utf8, utf8, win, CurrentTime);
			return;
		}
	}
	switch(ksym) {
	default:
		if(!iscntrl(*buf))
			insert(buf, strlen(buf));
		break;
	case XK_Delete:
		if(text[cursor] == '\0')
			return;
		cursor = nextrune(+1);
	case XK_BackSpace:
		if(cursor > 0)
			insert(NULL, nextrune(-1) - cursor);
		break;
	case XK_End:
		if(text[cursor] != '\0') {
			cursor = strlen(text);
			break;
		}
		if(next) {
			curr = matchend;
			calcoffsets();
			curr = prev;
			calcoffsets();
			while(next && (curr = curr->right))
				calcoffsets();
		}
		sel = matchend;
		break;
	case XK_Escape:
		exit(EXIT_FAILURE);
	case XK_Home:
		if(sel == matches) {
			cursor = 0;
			break;
		}
		sel = curr = matches;
		calcoffsets();
		break;
	case XK_Left:
		if(cursor > 0 && (!sel || !sel->left || lines > 0)) {
			cursor = nextrune(-1);
			break;
		}
		else if(lines > 0)
			return;
	case XK_Up:
		if(sel && sel->left && (sel = sel->left)->right == curr) {
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
	case XK_KP_Enter:
		fputs((sel && !(ev->state & ShiftMask)) ? sel->text : text, stdout);
		exit(EXIT_SUCCESS);
	case XK_Right:
		if(text[cursor] != '\0') {
			cursor = nextrune(+1);
			break;
		}
		else if(lines > 0)
			return;
	case XK_Down:
		if(sel && sel->right && (sel = sel->right) == next) {
			curr = next;
			calcoffsets();
		}
		break;
	case XK_Tab:
		if(!sel)
			return;
		strncpy(text, sel->text, sizeof text);
		cursor = strlen(text);
		match(True);
		break;
	}
	drawmenu();
}

void
match(Bool sub) {
	size_t len = strlen(text);
	Item *lexact, *lprefix, *lsubstr, *exactend, *prefixend, *substrend;
	Item *item, *lnext;

	lexact = lprefix = lsubstr = exactend = prefixend = substrend = NULL;
	for(item = sub ? matches : items; item && item->text; item = lnext) {
		lnext = sub ? item->right : item + 1;
		if(!fstrncmp(text, item->text, len + 1))
			appenditem(item, &lexact, &exactend);
		else if(!fstrncmp(text, item->text, len))
			appenditem(item, &lprefix, &prefixend);
		else if(fstrstr(item->text, text))
			appenditem(item, &lsubstr, &substrend);
	}
	matches = lexact;
	matchend = exactend;

	if(lprefix) {
		if(matchend) {
			matchend->right = lprefix;
			lprefix->left = matchend;
		}
		else
			matches = lprefix;
		matchend = prefixend;
	}
	if(lsubstr) {
		if(matchend) {
			matchend->right = lsubstr;
			lsubstr->left = matchend;
		}
		else
			matches = lsubstr;
		matchend = substrend;
	}
	curr = sel = matches;
	calcoffsets();
}

size_t
nextrune(int inc) {
	ssize_t n;

	for(n = cursor + inc; n + inc >= 0 && (text[n] & 0xc0) == 0x80; n += inc);
	return n;
}

void
paste(void) {
	char *p, *q;
	int di;
	unsigned long dl;
	Atom da;

	XGetWindowProperty(dc->dpy, win, utf8, 0, (sizeof text / 4) + 1, False,
	                   utf8, &da, &di, &dl, &dl, (unsigned char **)&p);
	insert(p, (q = strchr(p, '\n')) ? q-p : (ssize_t)strlen(p));
	XFree(p);
	drawmenu();
}

void
readstdin(void) {
	char buf[sizeof text], *p, *maxstr = NULL;
	size_t i, max = 0, size = 0;

	for(i = 0; fgets(buf, sizeof buf, stdin); i++) {
		if(i+1 >= size / sizeof *items)
			if(!(items = realloc(items, (size += BUFSIZ))))
				eprintf("cannot realloc %u bytes:", size);
		if((p = strchr(buf, '\n')))
			*p = '\0';
		if(!(items[i].text = strdup(buf)))
			eprintf("cannot strdup %u bytes:", strlen(buf)+1);
		if(strlen(items[i].text) > max)
			max = strlen(maxstr = items[i].text);
	}
	if(items)
		items[i].text = NULL;
	inputw = maxstr ? textw(dc, maxstr) : 0;
}

void
run(void) {
	XEvent ev;

	while(!XNextEvent(dc->dpy, &ev))
		switch(ev.type) {
		case Expose:
			if(ev.xexpose.count == 0)
				drawmenu();
			break;
		case KeyPress:
			keypress(&ev.xkey);
			break;
		case SelectionNotify:
			if(ev.xselection.property == utf8)
				paste();
			break;
		case VisibilityNotify:
			if(ev.xvisibility.state != VisibilityUnobscured)
				XRaiseWindow(dc->dpy, win);
			break;
		}
}

void
setup(void) {
	int x, y, screen = DefaultScreen(dc->dpy);
	Window root = RootWindow(dc->dpy, screen);
	XSetWindowAttributes wa;
#ifdef XINERAMA
	int n;
	XineramaScreenInfo *info;
#endif

	normcol[ColBG] = getcolor(dc, normbgcolor);
	normcol[ColFG] = getcolor(dc, normfgcolor);
	selcol[ColBG]  = getcolor(dc, selbgcolor);
	selcol[ColFG]  = getcolor(dc, selfgcolor);

	utf8 = XInternAtom(dc->dpy, "UTF8_STRING", False);

	/* menu geometry */
	bh = dc->font.height + 2;
	lines = MAX(lines, 0);
	mh = (lines + 1) * bh;
#ifdef XINERAMA
	if((info = XineramaQueryScreens(dc->dpy, &n))) {
		int i, di;
		unsigned int du;
		Window dw;

		XQueryPointer(dc->dpy, root, &dw, &dw, &x, &y, &di, &di, &du);
		for(i = 0; i < n-1; i++)
			if(INRECT(x, y, info[i].x_org, info[i].y_org, info[i].width, info[i].height))
				break;
		x = info[i].x_org;
		y = info[i].y_org + (topbar ? 0 : info[i].height - mh);
		mw = info[i].width;
		XFree(info);
	}
	else
#endif
	{
		x = 0;
		y = topbar ? 0 : DisplayHeight(dc->dpy, screen) - mh;
		mw = DisplayWidth(dc->dpy, screen);
	}
	promptw = prompt ? textw(dc, prompt) : 0;
	inputw = MIN(inputw, mw/3);
	match(False);

	/* menu window */
	wa.override_redirect = True;
	wa.background_pixmap = ParentRelative;
	wa.event_mask = ExposureMask | KeyPressMask | VisibilityChangeMask;
	win = XCreateWindow(dc->dpy, root, x, y, mw, mh, 0,
	                    DefaultDepth(dc->dpy, screen), CopyFromParent,
	                    DefaultVisual(dc->dpy, screen),
	                    CWOverrideRedirect | CWBackPixmap | CWEventMask, &wa);

	XMapRaised(dc->dpy, win);
	resizedc(dc, mw, mh);
	drawmenu();
}

void
usage(void) {
	fputs("usage: dmenu [-b] [-f] [-i] [-l lines] [-p prompt] [-fn font]\n"
	      "             [-nb color] [-nf color] [-sb color] [-sf color] [-v]\n", stderr);
	exit(EXIT_FAILURE);
}
