/*
 * (C)opyright MMVI Anselm R. Garbe <garbeam at gmail dot com>
 * (C)opyright MMVI Sander van Dijk <a dot h dot vandijk at gmail dot com>
 * See LICENSE file for license details.
 */

#include "dmenu.h"

#include <ctype.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <X11/cursorfont.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>

typedef struct Item Item;
struct Item {
	Item *next;		/* traverses all items */
	Item *left, *right;	/* traverses items matching current search pattern */
	char *text;
};

/* static */

static char text[4096];
static int mx, my, mw, mh;
static int ret = 0;
static int nitem = 0;
static unsigned int cmdw = 0;
static Bool done = False;
static Item *allitems = NULL;	/* first of all items */
static Item *item = NULL;	/* first of pattern matching items */
static Item *sel = NULL;
static Item *next = NULL;
static Item *prev = NULL;
static Item *curr = NULL;
static Window root;
static Window win;

static void
calcoffsets()
{
	unsigned int tw, w;

	if(!curr)
		return;

	w = cmdw + 2 * SPACE;
	for(next = curr; next; next=next->right) {
		tw = textw(next->text);
		if(tw > mw / 3)
			tw = mw / 3;
		w += tw;
		if(w > mw)
			break;
	}

	w = cmdw + 2 * SPACE;
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
drawmenu()
{
	Item *i;

	dc.x = 0;
	dc.y = 0;
	dc.w = mw;
	dc.h = mh;
	drawtext(NULL, False, False);

	/* print command */
	if(cmdw && item)
		dc.w = cmdw;
	drawtext(text[0] ? text : NULL, False, False);
	dc.x += cmdw;

	if(curr) {
		dc.w = SPACE;
		drawtext((curr && curr->left) ? "<" : NULL, False, False);
		dc.x += dc.w;

		/* determine maximum items */
		for(i = curr; i != next; i=i->right) {
			dc.w = textw(i->text);
			if(dc.w > mw / 3)
				dc.w = mw / 3;
			drawtext(i->text, sel == i, sel == i);
			dc.x += dc.w;
		}

		dc.x = mw - SPACE;
		dc.w = SPACE;
		drawtext(next ? ">" : NULL, False, False);
	}
	XCopyArea(dpy, dc.drawable, win, dc.gc, 0, 0, mw, mh, 0, 0);
	XFlush(dpy);
}

static void
match(char *pattern)
{
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

	curr = prev = next = sel = item;
	calcoffsets();
}

static void
kpress(XKeyEvent * e)
{
	char buf[32];
	int num, prev_nitem;
	unsigned int i, len;
	KeySym ksym;

	len = strlen(text);
	buf[0] = 0;
	num = XLookupString(e, buf, sizeof(buf), &ksym, 0);

	if(IsFunctionKey(ksym) || IsKeypadKey(ksym)
			|| IsMiscFunctionKey(ksym) || IsPFKey(ksym)
			|| IsPrivateKeypadKey(ksym))
		return;

	/* first check if a control mask is omitted */
	if(e->state & ControlMask) {
		switch (ksym) {
		default:	/* ignore other control sequences */
			return;
			break;
		case XK_h:
		case XK_H:
			ksym = XK_BackSpace;
			break;
		case XK_u:
		case XK_U:
			text[0] = 0;
			match(text);
			drawmenu();
			return;
			break;
		}
	}
	switch(ksym) {
	case XK_Left:
		if(!(sel && sel->left))
			return;
		sel=sel->left;
		if(sel->right == curr) {
			curr = prev;
			calcoffsets();
		}
		break;
	case XK_Tab:
		if(!sel)
			return;
		strncpy(text, sel->text, sizeof(text));
		match(text);
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
	case XK_Return:
		if(e->state & ShiftMask) {
			if(text)
				fprintf(stdout, "%s", text);
		}
		else if(sel)
			fprintf(stdout, "%s", sel->text);
		else if(text)
			fprintf(stdout, "%s", text);
		fflush(stdout);
		done = True;
		break;
	case XK_Escape:
		ret = 1;
		done = True;
		break;
	case XK_BackSpace:
		if((i = len)) {
			prev_nitem = nitem;
			do {
				text[--i] = 0;
				match(text);
			} while(i && nitem && prev_nitem == nitem);
			match(text);
		}
		break;
	default:
		if(num && !iscntrl((int) buf[0])) {
			buf[num] = 0;
			if(len > 0)
				strncat(text, buf, sizeof(text));
			else
				strncpy(text, buf, sizeof(text));
			match(text);
		}
	}
	drawmenu();
}

static char *
readstdin()
{
	static char *maxname = NULL;
	char *p, buf[1024];
	unsigned int len = 0, max = 0;
	Item *i, *new;

	i = 0;
	while(fgets(buf, sizeof(buf), stdin)) {
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

/* extern */

int screen;
Display *dpy;
DC dc = {0};

int
main(int argc, char *argv[])
{
	char *maxname;
	XEvent ev;
	XSetWindowAttributes wa;

	if(argc == 2 && !strncmp("-v", argv[1], 3)) {
		fputs("dmenu-"VERSION", (C)opyright MMVI Anselm R. Garbe\n", stdout);
		exit(EXIT_SUCCESS);
	}
	else if(argc != 1)
		eprint("usage: dmenu [-v]\n");

	dpy = XOpenDisplay(0);
	if(!dpy)
		eprint("dmenu: cannot open display\n");
	screen = DefaultScreen(dpy);
	root = RootWindow(dpy, screen);

	maxname = readstdin();

	/* grab as early as possible, but after reading all items!!! */
	while(XGrabKeyboard(dpy, root, True, GrabModeAsync,
			 GrabModeAsync, CurrentTime) != GrabSuccess)
		usleep(1000);

	/* style */
	dc.bg = getcolor(BGCOLOR);
	dc.fg = getcolor(FGCOLOR);
	dc.border = getcolor(BORDERCOLOR);
	setfont(FONT);

	wa.override_redirect = 1;
	wa.background_pixmap = ParentRelative;
	wa.event_mask = ExposureMask | ButtonPressMask | KeyPressMask;

	mx = my = 0;
	mw = DisplayWidth(dpy, screen);
	mh = dc.font.height + 4;

	win = XCreateWindow(dpy, root, mx, my, mw, mh, 0,
			DefaultDepth(dpy, screen), CopyFromParent,
			DefaultVisual(dpy, screen),
			CWOverrideRedirect | CWBackPixmap | CWEventMask, &wa);
	XDefineCursor(dpy, win, XCreateFontCursor(dpy, XC_xterm));

	/* pixmap */
	dc.drawable = XCreatePixmap(dpy, root, mw, mh, DefaultDepth(dpy, screen));
	dc.gc = XCreateGC(dpy, root, 0, 0);

	if(maxname)
		cmdw = textw(maxname);
	if(cmdw > mw / 3)
		cmdw = mw / 3;

	text[0] = 0;
	match(text);
	XMapRaised(dpy, win);
	drawmenu();
	XSync(dpy, False);

	/* main event loop */
	while(!done && !XNextEvent(dpy, &ev)) {
		switch (ev.type) {
		case KeyPress:
			kpress(&ev.xkey);
			break;
		case Expose:
			if(ev.xexpose.count == 0)
				drawmenu();
			break;
		default:
			break;
		}
	}

	XUngrabKeyboard(dpy, CurrentTime);
	XFreePixmap(dpy, dc.drawable);
	XFreeGC(dpy, dc.gc);
	XDestroyWindow(dpy, win);
	XCloseDisplay(dpy);

	return ret;
}
