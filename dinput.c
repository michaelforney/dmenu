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

/* forward declarations */
static void cleanup(void);
static void drawcursor(void);
static void drawinput(void);
static void eprint(const char *errstr, ...);
static Bool grabkeyboard(void);
static void kpress(XKeyEvent * e);
static void run(void);
static void setup(Bool topbar);

#include "config.h"

/* variables */
static char *prompt = NULL;
static char text[4096];
static int promptw = 0;
static int ret = 0;
static int screen;
static unsigned int mw, mh;
static unsigned int cursor = 0;
static unsigned int numlockmask = 0;
static Bool running = True;
static Display *dpy;
static Window parent, win;

#include "draw.c"

void
cleanup(void) {
	dccleanup();
	XDestroyWindow(dpy, win);
	XUngrabKeyboard(dpy, CurrentTime);
}

void
drawcursor(void) {
	XRectangle r = { dc.x, dc.y + 2, 1, dc.font.height - 2 };

	r.x += textnw(text, cursor) + dc.font.height / 2;

	XSetForeground(dpy, dc.gc, dc.norm[ColFG]);
	XFillRectangles(dpy, dc.drawable, dc.gc, &r, 1);
}

void
drawinput(void)
{
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
	drawtext(*text ? text : NULL, dc.norm);
	drawcursor();
	XCopyArea(dpy, dc.drawable, win, dc.gc, 0, 0, mw, mh, 0, 0);
	XFlush(dpy);
}

void
eprint(const char *errstr, ...) {
	va_list ap;

	va_start(ap, errstr);
	vfprintf(stderr, errstr, ap);
	va_end(ap);
	exit(EXIT_FAILURE);
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
		case XK_j:
			ksym = XK_Return;
			break;
		case XK_k:
			text[cursor] = '\0';
			break;
		case XK_u:
			memmove(text, text + cursor, sizeof text - cursor + 1);
			cursor = 0;
			break;
		case XK_w:
			if(cursor > 0) {
				i = cursor;
				while(i-- > 0 && text[i] == ' ');
				while(i-- > 0 && text[i] != ' ');
				memmove(text + i + 1, text + cursor, sizeof text - cursor + 1);
				cursor = i + 1;
			}
			break;
		case XK_y:
			{
				FILE *fp;
				char *s;
				if(!(fp = popen("sselp", "r")))
					eprint("dinput: cannot popen sselp\n");
				s = fgets(buf, sizeof buf, fp);
				pclose(fp);
				if(s == NULL)
					return;
			}
			num = strlen(buf);
			if(num && buf[num-1] == '\n')
				buf[--num] = '\0';
			break;
		}
	}
	switch(ksym) {
	default:
		num = MIN(num, sizeof text - cursor);
		if(num && !iscntrl((int) buf[0])) {
			memmove(text + cursor + num, text + cursor, sizeof text - cursor - num);
			memcpy(text + cursor, buf, num);
			cursor += num;
		}
		break;
	case XK_BackSpace:
		if(cursor == 0)
			return;
		for(i = 1; cursor - i > 0 && !IS_UTF8_1ST_CHAR(text[cursor - i]); i++);
		memmove(text + cursor - i, text + cursor, sizeof text - cursor + i);
		cursor -= i;
		break;
	case XK_Delete:
		if(cursor == len)
			return;
		for(i = 1; cursor + i < len && !IS_UTF8_1ST_CHAR(text[cursor + i]); i++);
		memmove(text + cursor, text + cursor + i, sizeof text - cursor);
		break;
	case XK_End:
		cursor = len;
		break;
	case XK_Escape:
		ret = 1;
		running = False;
		return;
	case XK_Home:
		cursor = 0;
		break;
	case XK_Left:
		if(cursor == 0)
			return;
		while(cursor-- > 0 && !IS_UTF8_1ST_CHAR(text[cursor]));
		break;
	case XK_Return:
		fprintf(stdout, "%s", text);
		fflush(stdout);
		running = False;
		return;
	case XK_Right:
		if(cursor == len)
			return;
		while(cursor++ < len && !IS_UTF8_1ST_CHAR(text[cursor]));
		break;
	}
	drawinput();
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
				drawinput();
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

	/* style */
	dc.norm[ColBG] = getcolor(normbgcolor);
	dc.norm[ColFG] = getcolor(normfgcolor);
	dc.sel[ColBG] = getcolor(selbgcolor);
	dc.sel[ColFG] = getcolor(selfgcolor);
	initfont(font);

	/* menu window */
	wa.override_redirect = True;
	wa.background_pixmap = ParentRelative;
	wa.event_mask = ExposureMask | ButtonPressMask | KeyPressMask | VisibilityChangeMask;

	/* menu window geometry */
	mh = (dc.font.height + 2);
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

	/* pixmap */
	dcsetup();
	if(prompt)
		promptw = MIN(textw(prompt), mw / 5);
	cursor = strlen(text);
	XMapRaised(dpy, win);
}

int
main(int argc, char *argv[]) {
	unsigned int i;
	Bool topbar = True;

	/* command line args */
	for(i = 1; i < argc; i++)
		if(!strcmp(argv[i], "-b"))
			topbar = False;
		else if(!strcmp(argv[i], "-e")) {
			if(++i < argc) parent = atoi(argv[i]);
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
			eprint("dinput-"VERSION", © 2006-2010 dinput engineers, see LICENSE for details\n");
		else if(!*text)
			strncpy(text, argv[i], sizeof text);
		else
			eprint("usage: dinput [-b] [-e <xid>] [-fn <font>] [-nb <color>] [-nf <color>]\n"
			       "              [-p <prompt>] [-sb <color>] [-sf <color>] [-v] [<text>]\n");
	if(!setlocale(LC_CTYPE, "") || !XSupportsLocale())
		fprintf(stderr, "dinput: warning: no locale support\n");
	if(!(dpy = XOpenDisplay(NULL)))
		eprint("dinput: cannot open display\n");
	screen = DefaultScreen(dpy);
	if(!parent)
		parent = RootWindow(dpy, screen);

	running = grabkeyboard();
	setup(topbar);
	drawinput();
	XSync(dpy, False);
	run();
	cleanup();
	XCloseDisplay(dpy);
	return ret;
}
