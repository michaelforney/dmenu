/* See LICENSE file for copyright and license details. */
#include <ctype.h>
#include <locale.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <X11/keysym.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#ifdef XINERAMA
#include <X11/extensions/Xinerama.h>
#endif

/* macros */
#define CLEANMASK(mask)         (mask & ~(numlockmask | LockMask))
#define INRECT(X,Y,RX,RY,RW,RH) ((X) >= (RX) && (X) < (RX) + (RW) && (Y) >= (RY) && (Y) < (RY) + (RH))
#define MIN(a, b)               ((a) < (b) ? (a) : (b))
#define MAX(a, b)               ((a) > (b) ? (a) : (b))
#define IS_UTF8_1ST_CHAR(c)     ((((c) & 0xc0) == 0xc0) || !((c) & 0x80))

typedef struct Item Item;
struct Item {
	char *text;
	Item *next;         /* traverses all items */
	Item *left, *right; /* traverses items matching current search pattern */
};

/* forward declarations */
static void appenditem(Item *i, Item **list, Item **last);
static void calcoffsetsh(void);
static void calcoffsetsv(void);
static char *cistrstr(const char *s, const char *sub);
static void cleanup(void);
static void dinput(void);
static void drawmenu(void);
static void drawmenuh(void);
static void drawmenuv(void);
static Bool grabkeyboard(void);
static void kpress(XKeyEvent * e);
static void match(char *pattern);
static void readstdin(void);
static void run(void);
static void setup(Bool topbar);

#include "config.h"
#include "draw.h"

/* variables */
static char *maxname = NULL;
static char *prompt = NULL;
static char text[4096];
static int cmdw = 0;
static int promptw = 0;
static int ret = 0;
static unsigned int lines = 0;
static unsigned int numlockmask = 0;
static Bool running = True;
static Item *allitems = NULL;  /* first of all items */
static Item *item = NULL;      /* first of pattern matching items */
static Item *sel = NULL;
static Item *next = NULL;
static Item *prev = NULL;
static Item *curr = NULL;
static Window win;
static int (*fstrncmp)(const char *, const char *, size_t) = strncmp;
static char *(*fstrstr)(const char *, const char *) = strstr;
static void (*calcoffsets)(void) = calcoffsetsh;

Display *dpy;
DC dc;
int screen;
unsigned int mw, mh;
Window parent;

void
appenditem(Item *i, Item **list, Item **last) {
	if(!(*last))
		*list = i;
	else
		(*last)->right = i;
	i->left = *last;
	i->right = NULL;
	*last = i;
}

void
calcoffsetsh(void) {
	unsigned int w;

	w = promptw + cmdw + (2 * spaceitem);
	for(next = curr; next; next = next->right)
		if((w += MIN(textw(next->text), mw / 3)) > mw)
			break;
	w = promptw + cmdw + (2 * spaceitem);
	for(prev = curr; prev && prev->left; prev = prev->left)
		if((w += MIN(textw(prev->left->text), mw / 3)) > mw)
			break;
}

void
calcoffsetsv(void) {
	unsigned int i;

	next = prev = curr;
	for(i = 0; i < lines && next; i++)
		next = next->right;
	for(i = 0; i < lines && prev && prev->left; i++)
		prev = prev->left;
}

char *
cistrstr(const char *s, const char *sub) {
	int c, csub;
	unsigned int len;

	if(!sub)
		return (char *)s;
	if((c = tolower(*sub++)) != '\0') {
		len = strlen(sub);
		do {
			do {
				if((csub = *s++) == '\0')
					return NULL;
			}
			while(tolower(csub) != c);
		}
		while(strncasecmp(s, sub, len) != 0);
		s--;
	}
	return (char *)s;
}

void
cleanup(void) {
	Item *itm;

	while(allitems) {
		itm = allitems->next;
		free(allitems->text);
		free(allitems);
		allitems = itm;
	}
	drawcleanup();
	XDestroyWindow(dpy, win);
	XUngrabKeyboard(dpy, CurrentTime);
}

void
dinput(void) {
	cleanup();
	execlp("dinput", "dinput", text, NULL); /* todo: argv */
	eprint("cannot exec dinput\n");
}

void
drawmenu(void) {
	dc.x = 0;
	dc.y = 0;
	dc.w = mw;
	dc.h = mh;
	drawtext(NULL, dc.norm);
	/* print prompt? */
	if(prompt) {
		dc.w = promptw;
		drawtext(prompt, dc.sel);
		dc.x += dc.w;
	}
	dc.w = mw - dc.x;
	/* print command */
	if(cmdw && item && lines == 0)
		dc.w = cmdw;
	drawtext(*text ? text : NULL, dc.norm);
	if(curr) {
		if(lines > 0)
			drawmenuv();
		else
			drawmenuh();
	}
	XCopyArea(dpy, dc.drawable, win, dc.gc, 0, 0, mw, mh, 0, 0);
	XFlush(dpy);
}

void
drawmenuh(void) {
	Item *i;

	dc.x += cmdw;
	dc.w = spaceitem;
	drawtext(curr->left ? "<" : NULL, dc.norm);
	dc.x += dc.w;
	for(i = curr; i != next; i=i->right) {
		dc.w = MIN(textw(i->text), mw / 3);
		drawtext(i->text, (sel == i) ? dc.sel : dc.norm);
		dc.x += dc.w;
	}
	dc.w = spaceitem;
	dc.x = mw - dc.w;
	drawtext(next ? ">" : NULL, dc.norm);
}

void
drawmenuv(void) {
	Item *i;

	dc.w = mw - dc.x;
	dc.h = dc.font.height + 2;
	dc.y = dc.h;
	for(i = curr; i != next; i=i->right) {
		drawtext(i->text, (sel == i) ? dc.sel : dc.norm);
		dc.y += dc.h;
	}
	dc.h = mh - dc.y;
	drawtext(NULL, dc.norm);
}

Bool
grabkeyboard(void) {
	unsigned int len;

	for(len = 1000; len; len--) {
		if(XGrabKeyboard(dpy, parent, True, GrabModeAsync, GrabModeAsync, CurrentTime)
		== GrabSuccess)
			break;
		usleep(1000);
	}
	return len > 0;
}

void
kpress(XKeyEvent * e) {
	char buf[sizeof text];
	int num;
	unsigned int i, len;
	KeySym ksym;

	len = strlen(text);
	num = XLookupString(e, buf, sizeof buf, &ksym, NULL);
	if(ksym == XK_KP_Enter)
		ksym = XK_Return;
	else if(ksym >= XK_KP_0 && ksym <= XK_KP_9)
		ksym = (ksym - XK_KP_0) + XK_0;
	else if(IsFunctionKey(ksym) || IsKeypadKey(ksym)
	|| IsMiscFunctionKey(ksym) || IsPFKey(ksym)
	|| IsPrivateKeypadKey(ksym))
		return;
	/* first check if a control mask is omitted */
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
			ksym = XK_Return;
			break;
		case XK_n:
			ksym = XK_Down;
			break;
		case XK_p:
			ksym = XK_Up;
			break;
		case XK_u:
			text[0] = '\0';
			match(text);
			break;
		case XK_w:
			if(len == 0)
				return;
			i = len;
			while(i-- > 0 && text[i] == ' ');
			while(i-- > 0 && text[i] != ' ');
			text[++i] = '\0';
			match(text);
			break;
		case XK_x:
			dinput();
			break;
		}
	}
	switch(ksym) {
	default:
		num = MIN(num, sizeof text);
		if(num && !iscntrl((int) buf[0])) {
			memcpy(text + len, buf, num + 1);
			len += num;
			match(text);
		}
		break;
	case XK_BackSpace:
		if(len == 0)
			return;
		for(i = 1; len - i > 0 && !IS_UTF8_1ST_CHAR(text[len - i]); i++);
		len -= i;
		text[len] = '\0';
		match(text);
		break;
	case XK_End:
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
		return;
	case XK_Home:
		sel = curr = item;
		calcoffsets();
		break;
	case XK_Left:
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
		if((e->state & ShiftMask) || !sel)
			fprintf(stdout, "%s", text);
		else
			fprintf(stdout, "%s", sel->text);
		fflush(stdout);
		running = False;
		return;
	case XK_Right:
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
		if(sel)
			strncpy(text, sel->text, sizeof text);
		dinput();
		break;
	}
	drawmenu();
}

void
match(char *pattern) {
	unsigned int plen;
	Item *i, *itemend, *lexact, *lprefix, *lsubstr, *exactend, *prefixend, *substrend;

	if(!pattern)
		return;
	plen = strlen(pattern);
	item = lexact = lprefix = lsubstr = itemend = exactend = prefixend = substrend = NULL;
	for(i = allitems; i; i = i->next)
		if(!fstrncmp(pattern, i->text, plen + 1))
			appenditem(i, &lexact, &exactend);
		else if(!fstrncmp(pattern, i->text, plen))
			appenditem(i, &lprefix, &prefixend);
		else if(fstrstr(i->text, pattern))
			appenditem(i, &lsubstr, &substrend);
	if(lexact) {
		item = lexact;
		itemend = exactend;
	}
	if(lprefix) {
		if(itemend) {
			itemend->right = lprefix;
			lprefix->left = itemend;
		}
		else
			item = lprefix;
		itemend = prefixend;
	}
	if(lsubstr) {
		if(itemend) {
			itemend->right = lsubstr;
			lsubstr->left = itemend;
		}
		else
			item = lsubstr;
	}
	curr = prev = next = sel = item;
	calcoffsets();
}

void
readstdin(void) {
	char *p, buf[sizeof text];
	unsigned int len = 0, max = 0;
	Item *i, *new;

	i = NULL;
	while(fgets(buf, sizeof buf, stdin)) {
		len = strlen(buf);
		if(buf[len-1] == '\n')
			buf[--len] = '\0';
		if(!(p = strdup(buf)))
			eprint("cannot strdup %u bytes\n", len);
		if((max = MAX(max, len)) == len)
			maxname = p;
		if(!(new = malloc(sizeof *new)))
			eprint("cannot malloc %u bytes\n", sizeof *new);
		new->next = new->left = new->right = NULL;
		new->text = p;
		if(!i)
			allitems = new;
		else 
			i->next = new;
		i = new;
	}
}

void
run(void) {
	XEvent ev;

	/* main event loop */
	while(running && !XNextEvent(dpy, &ev))
		switch (ev.type) {
		case KeyPress:
			kpress(&ev.xkey);
			break;
		case Expose:
			if(ev.xexpose.count == 0)
				drawmenu();
			break;
		case VisibilityNotify:
			if (ev.xvisibility.state != VisibilityUnobscured)
				XRaiseWindow(dpy, win);
			break;
		}
}

void
setup(Bool topbar) {
	int i, j, x, y;
#if XINERAMA
	int n;
	XineramaScreenInfo *info = NULL;
#endif
	XModifierKeymap *modmap;
	XSetWindowAttributes wa;
	XWindowAttributes pwa;

	/* init modifier map */
	modmap = XGetModifierMapping(dpy);
	for(i = 0; i < 8; i++)
		for(j = 0; j < modmap->max_keypermod; j++) {
			if(modmap->modifiermap[i * modmap->max_keypermod + j]
			== XKeysymToKeycode(dpy, XK_Num_Lock))
				numlockmask = (1 << i);
		}
	XFreeModifiermap(modmap);

	initfont(font);

	/* menu window */
	wa.override_redirect = True;
	wa.background_pixmap = ParentRelative;
	wa.event_mask = ExposureMask | ButtonPressMask | KeyPressMask | VisibilityChangeMask;

	/* menu window geometry */
	mh = (dc.font.height + 2) * (lines + 1);
#if XINERAMA
	if(parent == RootWindow(dpy, screen) && XineramaIsActive(dpy) && (info = XineramaQueryScreens(dpy, &n))) {
		i = 0;
		if(n > 1) {
			int di;
			unsigned int dui;
			Window dummy;
			if(XQueryPointer(dpy, parent, &dummy, &dummy, &x, &y, &di, &di, &dui))
				for(i = 0; i < n; i++)
					if(INRECT(x, y, info[i].x_org, info[i].y_org, info[i].width, info[i].height))
						break;
		}
		x = info[i].x_org;
		y = topbar ? info[i].y_org : info[i].y_org + info[i].height - mh;
		mw = info[i].width;
		XFree(info);
	}
	else
#endif
	{
		XGetWindowAttributes(dpy, parent, &pwa);
		x = 0;
		y = topbar ? 0 : pwa.height - mh;
		mw = pwa.width;
	}

	win = XCreateWindow(dpy, parent, x, y, mw, mh, 0,
			DefaultDepth(dpy, screen), CopyFromParent,
			DefaultVisual(dpy, screen),
			CWOverrideRedirect | CWBackPixmap | CWEventMask, &wa);

	drawsetup();
	if(maxname)
		cmdw = MIN(textw(maxname), mw / 3);
	if(prompt)
		promptw = MIN(textw(prompt), mw / 5);
	text[0] = '\0';
	match(text);
	XMapRaised(dpy, win);
}

int
main(int argc, char *argv[]) {
	unsigned int i;
	Bool topbar = True;

	/* command line args */
	progname = argv[0];
	for(i = 1; i < argc; i++)
		if(!strcmp(argv[i], "-i")) {
			fstrncmp = strncasecmp;
			fstrstr = cistrstr;
		}
		else if(!strcmp(argv[i], "-b"))
			topbar = False;
		else if(!strcmp(argv[i], "-e")) {
			if(++i < argc) parent = atoi(argv[i]);
		}
		else if(!strcmp(argv[i], "-l")) {
			if(++i < argc) lines = atoi(argv[i]);
			if(lines > 0)
				calcoffsets = calcoffsetsv;
		}
		else if(!strcmp(argv[i], "-fn")) {
			if(++i < argc) font = argv[i];
		}
		else if(!strcmp(argv[i], "-nb")) {
			if(++i < argc) normbgcolor = argv[i];
		}
		else if(!strcmp(argv[i], "-nf")) {
			if(++i < argc) normfgcolor = argv[i];
		}
		else if(!strcmp(argv[i], "-p")) {
			if(++i < argc) prompt = argv[i];
		}
		else if(!strcmp(argv[i], "-sb")) {
			if(++i < argc) selbgcolor = argv[i];
		}
		else if(!strcmp(argv[i], "-sf")) {
			if(++i < argc) selfgcolor = argv[i];
		}
		else if(!strcmp(argv[i], "-v"))
			eprint("dmenu-"VERSION", © 2006-2010 dmenu engineers, see LICENSE for details\n");
		else
			eprint("usage: dmenu [-i] [-b] [-e <xid>] [-l <lines>] [-fn <font>] [-nb <color>]\n"
			       "             [-nf <color>] [-p <prompt>] [-sb <color>] [-sf <color>] [-v]\n");
	if(!setlocale(LC_CTYPE, "") || !XSupportsLocale())
		fprintf(stderr, "dmenu: warning: no locale support\n");
	if(!(dpy = XOpenDisplay(NULL)))
		eprint("cannot open display\n");
	screen = DefaultScreen(dpy);
	if(!parent)
		parent = RootWindow(dpy, screen);

	readstdin();
	running = grabkeyboard();

	setup(topbar);
	drawmenu();
	XSync(dpy, False);
	run();
	cleanup();
	XCloseDisplay(dpy);
	return ret;
}
