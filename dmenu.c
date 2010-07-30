/* See LICENSE file for copyright and license details. */
#include <ctype.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <X11/keysym.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include "dmenu.h"

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
static void drawitem(char *s, unsigned long col[ColLast]);
static void drawmenuh(void);
static void drawmenuv(void);
static void match(void);
static void readstdin(void);

/* variables */
static char **argp = NULL;
static char *maxname = NULL;
static unsigned int cmdw = 0;
static unsigned int lines = 0;
static Item *allitems = NULL;  /* first of all items */
static Item *item = NULL;      /* first of pattern matching items */
static Item *sel = NULL;
static Item *next = NULL;
static Item *prev = NULL;
static Item *curr = NULL;
static int (*fstrncmp)(const char *, const char *, size_t) = strncmp;
static char *(*fstrstr)(const char *, const char *) = strstr;
static void (*calcoffsets)(void) = calcoffsetsh;

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
	unsigned int w, x;

	w = promptw + cmdw + textw(&dc, "<") + textw(&dc, ">");
	for(x = w, next = curr; next; next = next->right)
		if((x += MIN(textw(&dc, next->text), mw / 3)) > mw)
			break;
	for(x = w, prev = curr; prev && prev->left; prev = prev->left)
		if((x += MIN(textw(&dc, prev->left->text), mw / 3)) > mw)
			break;
}

void
calcoffsetsv(void) {
	unsigned int i;

	next = prev = curr;
	for(i = 0; i < lines && next; i++)
		next = next->right;
	mh = (dc.font.height + 2) * (i + 1);
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
	cleanupdraw(&dc);
	XDestroyWindow(dpy, win);
	XUngrabKeyboard(dpy, CurrentTime);
	XCloseDisplay(dpy);
}

void
dinput(void) {
	cleanup();
	argp[0] = "dinput";
	argp[1] = text;
	execvp("dinput", argp);
	eprint("cannot exec dinput\n");
}

void
drawbar(void) {
	dc.x = 0;
	dc.y = 0;
	dc.w = mw;
	dc.h = mh;
	drawbox(&dc, normcol);
	dc.h = dc.font.height + 2;
	dc.y = topbar ? 0 : mh - dc.h;
	/* print prompt? */
	if(prompt) {
		dc.w = promptw;
		drawtext(&dc, prompt, selcol);
		dc.x += dc.w;
	}
	dc.w = mw - dc.x;
	/* print command */
	if(cmdw && item && lines == 0)
		dc.w = cmdw;
	drawtext(&dc, text, normcol);
	if(lines > 0)
		drawmenuv();
	else if(curr)
		drawmenuh();
	commitdraw(&dc, win);
}

void
drawitem(char *s, unsigned long col[ColLast]) {
	drawbox(&dc, col);
	drawtext(&dc, s, col);
}

void
drawmenuh(void) {
	Item *i;

	dc.x += cmdw;
	dc.w = textw(&dc, "<");
	drawtext(&dc, curr->left ? "<" : NULL, normcol);
	dc.x += dc.w;
	for(i = curr; i != next; i = i->right) {
		dc.w = MIN(textw(&dc, i->text), mw / 3);
		drawitem(i->text, (sel == i) ? selcol : normcol);
		dc.x += dc.w;
	}
	dc.w = textw(&dc, ">");
	dc.x = mw - dc.w;
	drawtext(&dc, next ? ">" : NULL, normcol);
}

void
drawmenuv(void) {
	Item *i;
	XWindowAttributes wa;

	dc.y = topbar ? dc.h : 0;
	dc.w = mw - dc.x;
	for(i = curr; i != next; i = i->right) {
		drawitem(i->text, (sel == i) ? selcol : normcol);
		dc.y += dc.h;
	}
	if(!XGetWindowAttributes(dpy, win, &wa))
		eprint("cannot get window attributes");
	XMoveResizeWindow(dpy, win, wa.x, wa.y + (topbar ? 0 : wa.height - mh), mw, mh);
}

void
kpress(XKeyEvent *e) {
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
		case XK_m:
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
			match();
			break;
		case XK_w:
			if(len == 0)
				return;
			i = len;
			while(i-- > 0 && text[i] == ' ');
			while(i-- > 0 && text[i] != ' ');
			text[++i] = '\0';
			match();
			break;
		}
	}
	switch(ksym) {
	default:
		num = MIN(num, sizeof text);
		if(num && !iscntrl((int) buf[0])) {
			memcpy(text + len, buf, num + 1);
			len += num;
			match();
		}
		break;
	case XK_BackSpace:
		if(len == 0)
			return;
		for(i = 1; len - i > 0 && !IS_UTF8_1ST_CHAR(text[len - i]); i++);
		len -= i;
		text[len] = '\0';
		match();
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
		exit(EXIT_FAILURE);
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
		if(e->state & ShiftMask)
			dinput();
		fprintf(stdout, "%s", sel ? sel->text : text);
		fflush(stdout);
		exit(EXIT_SUCCESS);
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
	drawbar();
}

void
match(void) {
	unsigned int len;
	Item *i, *itemend, *lexact, *lprefix, *lsubstr, *exactend, *prefixend, *substrend;

	len = strlen(text);
	item = lexact = lprefix = lsubstr = itemend = exactend = prefixend = substrend = NULL;
	for(i = allitems; i; i = i->next)
		if(!fstrncmp(text, i->text, len + 1))
			appenditem(i, &lexact, &exactend);
		else if(!fstrncmp(text, i->text, len))
			appenditem(i, &lprefix, &prefixend);
		else if(fstrstr(i->text, text))
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

int
main(int argc, char *argv[]) {
	unsigned int i;

	/* command line args */
	progname = "dmenu";
	for(i = 1; i < argc; i++)
		if(!strcmp(argv[i], "-i")) {
			fstrncmp = strncasecmp;
			fstrstr = cistrstr;
		}
		else if(!strcmp(argv[i], "-b"))
			topbar = False;
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
		else if(!strcmp(argv[i], "-v")) {
			printf("dmenu-"VERSION", © 2006-2010 dmenu engineers, see LICENSE for details\n");
			exit(EXIT_SUCCESS);
		}
		else {
			fputs("usage: dmenu [-i] [-b] [-l <lines>] [-fn <font>] [-nb <color>]\n"
			      "             [-nf <color>] [-p <prompt>] [-sb <color>] [-sf <color>] [-v]\n", stderr);
			exit(EXIT_FAILURE);
		}
	if(!setlocale(LC_CTYPE, "") || !XSupportsLocale())
		fprintf(stderr, "dmenu: warning: no locale support\n");
	if(!(dpy = XOpenDisplay(NULL)))
		eprint("cannot open display\n");
	if(atexit(&cleanup) != 0)
		eprint("cannot register cleanup\n");
	screen = DefaultScreen(dpy);
	root = RootWindow(dpy, screen);
	if(!(argp = malloc(sizeof *argp * (argc+2))))
		eprint("cannot malloc %u bytes\n", sizeof *argp * (argc+2));
	memcpy(argp + 2, argv + 1, sizeof *argp * argc);

	readstdin();
	grabkeyboard();
	setup(lines);
	if(maxname)
		cmdw = MIN(textw(&dc, maxname), mw / 3);
	match();
	run();
	return 0;
}
