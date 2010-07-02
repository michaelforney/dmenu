/* See LICENSE file for copyright and license details. */
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <X11/keysym.h>
#ifdef XINERAMA
#include <X11/extensions/Xinerama.h>
#endif
#include "dmenu.h"

/* variables */
char *prompt = NULL;
char text[4096] = "";
int promptw = 0;
int screen;
unsigned int numlockmask = 0;
unsigned int mw, mh;
unsigned long normcol[ColLast];
unsigned long selcol[ColLast];
Bool topbar = True;
DC dc;
Display *dpy;
Window win, root;

void
grabkeyboard(void) {
	unsigned int len;

	for(len = 1000; len; len--) {
		if(XGrabKeyboard(dpy, root, True, GrabModeAsync, GrabModeAsync, CurrentTime)
		== GrabSuccess)
			return;
		usleep(1000);
	}
	exit(EXIT_FAILURE);
}

void
run(void) {
	XEvent ev;

	/* main event loop */
	XSync(dpy, False);
	while(!XNextEvent(dpy, &ev))
		switch(ev.type) {
		case KeyPress:
			kpress(&ev.xkey);
			break;
		case Expose:
			if(ev.xexpose.count == 0)
				drawbar();
			break;
		case VisibilityNotify:
			if(ev.xvisibility.state != VisibilityUnobscured)
				XRaiseWindow(dpy, win);
			break;
		}
	exit(EXIT_FAILURE);
}

void
setup(unsigned int lines) {
	int i, j, x, y;
#if XINERAMA
	int n;
	XineramaScreenInfo *info = NULL;
#endif
	XModifierKeymap *modmap;
	XSetWindowAttributes wa;

	/* init modifier map */
	modmap = XGetModifierMapping(dpy);
	for(i = 0; i < 8; i++)
		for(j = 0; j < modmap->max_keypermod; j++) {
			if(modmap->modifiermap[i * modmap->max_keypermod + j]
			== XKeysymToKeycode(dpy, XK_Num_Lock))
				numlockmask = (1 << i);
		}
	XFreeModifiermap(modmap);

	dc.dpy = dpy;
	normcol[ColBG] = getcolor(&dc, normbgcolor);
	normcol[ColFG] = getcolor(&dc, normfgcolor);
	selcol[ColBG] = getcolor(&dc, selbgcolor);
	selcol[ColFG] = getcolor(&dc, selfgcolor);
	initfont(&dc, font);

	/* input window */
	wa.override_redirect = True;
	wa.background_pixmap = ParentRelative;
	wa.event_mask = ExposureMask | KeyPressMask | VisibilityChangeMask;

	/* input window geometry */
	mh = (dc.font.height + 2) * (lines + 1);
#if XINERAMA
	if(XineramaIsActive(dpy) && (info = XineramaQueryScreens(dpy, &n))) {
		i = 0;
		if(n > 1) {
			int di;
			unsigned int dui;
			Window dummy;
			if(XQueryPointer(dpy, root, &dummy, &dummy, &x, &y, &di, &di, &dui))
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
		x = 0;
		y = topbar ? 0 : DisplayHeight(dpy, screen) - mh;
		mw = DisplayWidth(dpy, screen);
	}

	win = XCreateWindow(dpy, root, x, y, mw, mh, 0,
			DefaultDepth(dpy, screen), CopyFromParent,
			DefaultVisual(dpy, screen),
			CWOverrideRedirect | CWBackPixmap | CWEventMask, &wa);

	setupdraw(&dc, win);
	if(prompt)
		promptw = MIN(textw(&dc, prompt), mw / 5);
	XMapRaised(dpy, win);
}
