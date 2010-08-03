/* See LICENSE file for copyright and license details. */
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#ifdef XINERAMA
#include <X11/extensions/Xinerama.h>
#endif
#include <draw.h>

#define INRECT(x,y,rx,ry,rw,rh) ((x) >= (rx) && (x) < (rx)+(rw) && (y) >= (ry) && (y) < (ry)+(rh))
#define MIN(a,b)                ((a) < (b) ? (a) : (b))
#define MAX(a,b)                ((a) > (b) ? (a) : (b))
#define UTF8_CODEPOINT(c)       (((c) & 0xc0) != 0x80)

typedef struct Item Item;
struct Item {
	char *text;
	Item *next;          /* traverses all items */
	Item *left, *right;  /* traverses matching items */
};

static void appenditem(Item *item, Item **list, Item **last);
static void calcoffsets(void);
static char *cistrstr(const char *s, const char *sub);
static void drawmenu(void);
static void grabkeyboard(void);
static void insert(const char *s, ssize_t n);
static void keypress(XKeyEvent *e);
static void match(void);
static void paste(void);
static void readstdin(void);
static void run(void);
static void setup(void);
static void usage(void);

static char text[4096];
static size_t cursor = 0;
static const char *prompt = NULL;
static const char *normbgcolor = "#cccccc";
static const char *normfgcolor = "#000000";
static const char *selbgcolor  = "#0066ff";
static const char *selfgcolor  = "#ffffff";
static unsigned int inputw = 0;
static unsigned int lines = 0;
static unsigned int mw, mh;
static unsigned int promptw;
static unsigned long normcol[ColLast];
static unsigned long selcol[ColLast];
static Atom utf8;
static Bool topbar = True;
static DC *dc;
static Item *allitems, *matches;
static Item *curr, *prev, *next, *sel;
static Window root, win;

static int (*fstrncmp)(const char *, const char *, size_t) = strncmp;
static char *(*fstrstr)(const char *, const char *) = strstr;

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
calcoffsets(void)
{
	unsigned int h, i, n;

	h = dc->font.height+2;
	if(lines > 0)
		n = lines * h;
	else
		n = mw - (promptw + inputw + textw(dc, "<") + textw(dc, ">"));

	prev = next = curr;
	for(i = 0; next; next = next->right)
		if((i += (lines > 0) ? h : MIN(textw(dc, next->text), mw/3)) > n)
			break;
	for(i = 0; prev && prev->left; prev = prev->left)
		if((i += (lines > 0) ? h : MIN(textw(dc, prev->left->text), mw/3)) > n)
			break;
}

char *
cistrstr(const char *s, const char *sub) {
	size_t len;

	for(len = strlen(sub); *s; s++)
		if(!strncasecmp(s, sub, len))
			return (char *)s;
	return NULL;
}

void
drawmenu(void) {
	Item *item;

	dc->x = 0;
	dc->y = 0;
	drawrect(dc, 0, 0, mw, mh, BG(dc, normcol));

	dc->h = dc->font.height + 2;
	dc->y = topbar ? 0 : mh - dc->h;
	/* print prompt? */
	if(prompt) {
		dc->w = promptw;
		drawtext(dc, prompt, selcol);
		dc->x = dc->w;
	}
	dc->w = mw - dc->x;
	/* print input area */
	if(matches && lines == 0 && textw(dc, text) <= inputw)
		dc->w = inputw;
	drawtext(dc, text, normcol);
	drawrect(dc, textnw(dc, text, cursor) + dc->h/2 - 2, 2, 1, dc->h - 4, FG(dc, normcol));

	if(lines > 0) {
		dc->y = topbar ? dc->h : 0;
		dc->w = mw - dc->x;
		for(item = curr; item != next; item = item->right) {
			drawtext(dc, item->text, (item == sel) ? selcol : normcol);
			dc->y += dc->h;
		}
	}
	else if(curr && (dc->w == inputw || curr->next)) {
		dc->x += inputw;
		dc->w = textw(dc, "<");
		if(prev)
			drawtext(dc, "<", normcol);
		for(item = curr; item != next; item = item->right) {
			dc->x += dc->w;
			dc->w = MIN(textw(dc, item->text), mw/3);
			drawtext(dc, item->text, (item == sel) ? selcol : normcol);
		}
		dc->w = textw(dc, ">");
		dc->x = mw - dc->w;
		if(next)
			drawtext(dc, ">", normcol);
	}
	commitdraw(dc, win);
}

void
grabkeyboard(void) {
	int i;

	for(i = 0; i < 1000; i++) {
		if(!XGrabKeyboard(dc->dpy, root, True, GrabModeAsync, GrabModeAsync, CurrentTime))
			return;
		usleep(1000);
	}
	eprintf("cannot grab keyboard\n");
}

void
insert(const char *s, ssize_t n) {
	memmove(text + cursor + n, text + cursor, sizeof text - cursor - n);
	if(n > 0)
		memcpy(text + cursor, s, n);
	cursor += n;
	match();
}

void
keypress(XKeyEvent *e) {
	char buf[sizeof text];
	int n;
	size_t len;
	KeySym ksym;

	len = strlen(text);
	XLookupString(e, buf, sizeof buf, &ksym, NULL);
	if(e->state & ControlMask) {
		switch(tolower(ksym)) {
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
		case XK_m:
			ksym = XK_Return;
			break;
		case XK_k:  /* delete right */
			text[cursor] = '\0';
			match();
			break;
		case XK_n:
			ksym = XK_Down;
			break;
		case XK_p:
			ksym = XK_Up;
			break;
		case XK_u:  /* delete left */
			insert(NULL, -cursor);
			break;
		case XK_w:  /* delete word */
			if(cursor == 0)
				return;
			n = 0;
			while(cursor - n++ > 0 && text[cursor - n] == ' ');
			while(cursor - n++ > 0 && text[cursor - n] != ' ');
			insert(NULL, 1-n);
			break;
		case XK_y:  /* paste selection */
			XConvertSelection(dc->dpy, XA_PRIMARY, utf8, None, win, CurrentTime);
			/* causes SelectionNotify event */
			return;
		}
	}
	switch(ksym) {
	default:
		if(!iscntrl((int)*buf))
			insert(buf, MIN(strlen(buf), sizeof text - cursor));
		break;
	case XK_BackSpace:
		if(cursor == 0)
			return;
		for(n = 1; cursor - n > 0 && !UTF8_CODEPOINT(text[cursor - n]); n++);
		insert(NULL, -n);
		break;
	case XK_Delete:
		if(cursor == len)
			return;
		for(n = 1; cursor + n < len && !UTF8_CODEPOINT(text[cursor + n]); n++);
		cursor += n;
		insert(NULL, -n);
		break;
	case XK_End:
		if(cursor < len) {
			cursor = len;
			break;
		}
		while(next) {
			sel = curr = next;
			calcoffsets();
		}
		while(sel && sel->right)
			sel = sel->right;
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
			while(cursor-- > 0 && !UTF8_CODEPOINT(text[cursor]));
			break;
		}
		else if(lines > 0)
			return;
	case XK_Up:
		if(!sel || !sel->left)
			return;
		sel = sel->left;
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
	case XK_KP_Enter:
		fputs((sel && !(e->state & ShiftMask)) ? sel->text : text, stdout);
		fflush(stdout);
		exit(EXIT_SUCCESS);
	case XK_Right:
		if(cursor < len) {
			while(cursor++ < len && !UTF8_CODEPOINT(text[cursor]));
			break;
		}
		else if(lines > 0)
			return;
	case XK_Down:
		if(!sel || !sel->right)
			return;
		sel = sel->right;
		if(sel == next) {
			curr = next;
			calcoffsets();
		}
		break;
	case XK_Tab:
		if(!sel)
			return;
		strncpy(text, sel->text, sizeof text);
		cursor = strlen(text);
		match();
		break;
	}
	drawmenu();
}

void
match(void) {
	unsigned int len;
	Item *item, *itemend, *lexact, *lprefix, *lsubstr, *exactend, *prefixend, *substrend;

	len = strlen(text);
	matches = lexact = lprefix = lsubstr = itemend = exactend = prefixend = substrend = NULL;
	for(item = allitems; item; item = item->next)
		if(!fstrncmp(text, item->text, len + 1))
			appenditem(item, &lexact, &exactend);
		else if(!fstrncmp(text, item->text, len))
			appenditem(item, &lprefix, &prefixend);
		else if(fstrstr(item->text, text))
			appenditem(item, &lsubstr, &substrend);
	if(lexact) {
		matches = lexact;
		itemend = exactend;
	}
	if(lprefix) {
		if(itemend) {
			itemend->right = lprefix;
			lprefix->left = itemend;
		}
		else
			matches = lprefix;
		itemend = prefixend;
	}
	if(lsubstr) {
		if(itemend) {
			itemend->right = lsubstr;
			lsubstr->left = itemend;
		}
		else
			matches = lsubstr;
	}
	curr = prev = next = sel = matches;
	calcoffsets();
}

void
paste(void) {
	char *p, *q;
	int di;
	unsigned long dl;
	Atom da;

	XGetWindowProperty(dc->dpy, win, utf8, 0, sizeof text - cursor, True,
	                   utf8, &da, &di, &dl, &dl, (unsigned char **)&p);
	insert(p, (q = strchr(p, '\n')) ? q-p : strlen(p));
	XFree(p);
	drawmenu();
}

void
readstdin(void) {
	char buf[sizeof text], *p;
	Item *item, *new;

	allitems = NULL;
	for(item = NULL; fgets(buf, sizeof buf, stdin); item = new) {
		if((p = strchr(buf, '\n')))
			*p = '\0';
		if(!(new = malloc(sizeof *new)))
			eprintf("cannot malloc %u bytes\n", sizeof *new);
		if(!(new->text = strdup(buf)))
			eprintf("cannot strdup %u bytes\n", strlen(buf));
		inputw = MAX(inputw, textw(dc, new->text));
		new->next = new->left = new->right = NULL;
		if(item)
			item->next = new;
		else
			allitems = new;
	}
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
	int x, y, screen;
	XSetWindowAttributes wa;
#ifdef XINERAMA
	int n;
	XineramaScreenInfo *info;
#endif

	screen = DefaultScreen(dc->dpy);
	root = RootWindow(dc->dpy, screen);
	utf8 = XInternAtom(dc->dpy, "UTF8_STRING", False);

	normcol[ColBG] = getcolor(dc, normbgcolor);
	normcol[ColFG] = getcolor(dc, normfgcolor);
	selcol[ColBG] = getcolor(dc, selbgcolor);
	selcol[ColFG] = getcolor(dc, selfgcolor);

	/* input window geometry */
	mh = (dc->font.height + 2) * (lines + 1);
#ifdef XINERAMA
	if((info = XineramaQueryScreens(dc->dpy, &n))) {
		int i, di;
		unsigned int du;
		Window dw;

		XQueryPointer(dc->dpy, root, &dw, &dw, &x, &y, &di, &di, &du);
		for(i = 0; i < n; i++)
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
	/* input window */
	wa.override_redirect = True;
	wa.background_pixmap = ParentRelative;
	wa.event_mask = ExposureMask | KeyPressMask | VisibilityChangeMask;
	win = XCreateWindow(dc->dpy, root, x, y, mw, mh, 0,
			DefaultDepth(dc->dpy, screen), CopyFromParent,
			DefaultVisual(dc->dpy, screen),
			CWOverrideRedirect | CWBackPixmap | CWEventMask, &wa);

	grabkeyboard();
	setcanvas(dc, win, mw, mh);
	inputw = MIN(inputw, mw/3);
	promptw = prompt ? MIN(textw(dc, prompt), mw/5) : 0;
	XMapRaised(dc->dpy, win);
	text[0] = '\0';
	match();
}

void
usage(void) {
	fputs("usage: dmenu [-b] [-i] [-l lines] [-p prompt] [-fn font] [-nb color]\n"
	      "             [-nf color] [-sb color] [-sf color] [-v]\n", stderr);
	exit(EXIT_FAILURE);
}

int
main(int argc, char *argv[]) {
	int i;

	progname = "dmenu";
	dc = initdraw();

	for(i = 1; i < argc; i++)
		/* single flags */
		if(!strcmp(argv[i], "-v")) {
			fputs("dmenu-"VERSION", © 2006-2010 dmenu engineers, see LICENSE for details\n", stdout);
			exit(EXIT_SUCCESS);
		}
		else if(!strcmp(argv[i], "-b"))
			topbar = False;
		else if(!strcmp(argv[i], "-i")) {
			fstrncmp = strncasecmp;
			fstrstr = cistrstr;
		}
		else if(i == argc-1)
			usage();
		/* double flags */
		else if(!strcmp(argv[i], "-l"))
			lines = atoi(argv[++i]);
		else if(!strcmp(argv[i], "-p"))
			prompt = argv[++i];
		else if(!strcmp(argv[i], "-fn"))
			initfont(dc, argv[i++]);
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

	readstdin();
	setup();
	run();

	return EXIT_FAILURE;  /* should not reach */
}
