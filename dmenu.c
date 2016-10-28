/* See LICENSE file for copyright and license details. */
#include <ctype.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

#include <wayland-client.h>
#include <wld/wayland.h>
#include <wld/wld.h>
#include <xkbcommon/xkbcommon.h>

#include "drw.h"
#include "util.h"
#include "swc-client-protocol.h"

/* macros */
#define INTERSECT(x,y,w,h,r)  (MAX(0, MIN((x)+(w),(r).x_org+(r).width)  - MAX((x),(r).x_org)) \
                             * MAX(0, MIN((y)+(h),(r).y_org+(r).height) - MAX((y),(r).y_org)))
#define LENGTH(X)             (sizeof X / sizeof X[0])
#define TEXTW(X)              (drw_fontset_getwidth(drw, (X)) + lrpad)

/* enums */
enum { SchemeNorm, SchemeSel, SchemeOut, SchemeLast }; /* color schemes */

struct item {
	char *text;
	struct item *left, *right;
	int out;
};

struct xkb {
	struct xkb_context *context;
	struct xkb_state *state;
	struct xkb_keymap *keymap;
	xkb_mod_index_t ctrl, alt, shift;
};

static void paste(void);

static char text[BUFSIZ] = "";
static int bh, mw, mh;
static int inputw = 0, promptw;
static int lrpad; /* sum of left and right padding */
static size_t cursor;
static struct item *items = NULL;
static struct item *matches, *matchend;
static struct item *prev, *curr, *next, *sel;
static int mon = -1;

static struct wl_display *dpy;
static struct wl_compositor *compositor;
static struct wl_keyboard *kbd;
static struct wl_seat *seat;
static struct wl_shell *shell;
static struct wl_surface *surface;
static struct wl_data_device_manager *datadevman;
static struct wl_data_device *datadev;
static struct wl_data_offer *seloffer;
static struct swc_screen *screen;
static struct swc_panel_manager *panelman;
static struct swc_panel *panel;
static struct xkb xkb;

static Drw *drw;
static Clr *scheme[SchemeLast];

#include "config.h"

static int (*fstrncmp)(const char *, const char *, size_t) = strncmp;
static char *(*fstrstr)(const char *, const char *) = strstr;

static void
appenditem(struct item *item, struct item **list, struct item **last)
{
	if (*last)
		(*last)->right = item;
	else
		*list = item;

	item->left = *last;
	item->right = NULL;
	*last = item;
}

static void
calcoffsets(void)
{
	int i, n;

	if (lines > 0)
		n = lines * bh;
	else
		n = mw - (promptw + inputw + TEXTW("<") + TEXTW(">"));
	/* calculate which items will begin the next page and previous page */
	for (i = 0, next = curr; next; next = next->right)
		if ((i += (lines > 0) ? bh : MIN(TEXTW(next->text), n)) > n)
			break;
	for (i = 0, prev = curr; prev && prev->left; prev = prev->left)
		if ((i += (lines > 0) ? bh : MIN(TEXTW(prev->left->text), n)) > n)
			break;
}

static void
cleanup(void)
{
	size_t i;

	for (i = 0; i < SchemeLast; i++)
		free(scheme[i]);
	drw_free(drw);
	wl_display_disconnect(dpy);
}

static char *
cistrstr(const char *s, const char *sub)
{
	size_t len;

	for (len = strlen(sub); *s; s++)
		if (!strncasecmp(s, sub, len))
			return (char *)s;
	return NULL;
}

static int
drawitem(struct item *item, int x, int y, int w)
{
	if (item == sel)
		drw_setscheme(drw, scheme[SchemeSel]);
	else if (item->out)
		drw_setscheme(drw, scheme[SchemeOut]);
	else
		drw_setscheme(drw, scheme[SchemeNorm]);

	return drw_text(drw, x, y, w, bh, lrpad / 2, item->text, 0);
}

static void
drawmenu(void)
{
	unsigned int curpos;
	struct item *item;
	int x = 0, y = 0, w;

	wld_set_target_surface(drw->renderer, drw->surface);
	drw_setscheme(drw, scheme[SchemeNorm]);
	drw_rect(drw, 0, 0, mw, mh, 1, 1);

	if (prompt && *prompt) {
		drw_setscheme(drw, scheme[SchemeSel]);
		x = drw_text(drw, x, 0, promptw, bh, lrpad / 2, prompt, 0);
	}
	/* draw input field */
	w = (lines > 0 || !matches) ? mw - x : inputw;
	drw_setscheme(drw, scheme[SchemeNorm]);
	drw_text(drw, x, 0, w, bh, lrpad / 2, text, 0);

	drw_font_getexts(drw->fonts, text, cursor, &curpos, NULL);
	if ((curpos += lrpad / 2 - 1) < w) {
		drw_setscheme(drw, scheme[SchemeNorm]);
		drw_rect(drw, x + curpos, 2, 2, bh - 4, 1, 0);
	}

	if (lines > 0) {
		/* draw vertical list */
		for (item = curr; item != next; item = item->right)
			drawitem(item, x, y += bh, mw - x);
	} else if (matches) {
		/* draw horizontal list */
		x += inputw;
		w = TEXTW("<");
		if (curr->left) {
			drw_setscheme(drw, scheme[SchemeNorm]);
			drw_text(drw, x, 0, w, bh, lrpad / 2, "<", 0);
		}
		x += w;
		for (item = curr; item != next; item = item->right)
			x = drawitem(item, x, 0, MIN(TEXTW(item->text), mw - x - TEXTW(">")));
		if (next) {
			w = TEXTW(">");
			drw_setscheme(drw, scheme[SchemeNorm]);
			drw_text(drw, mw - w, 0, w, bh, lrpad / 2, ">", 0);
		}
	}
	drw_map(drw, surface, 0, 0, mw, mh);
}

static void
match(void)
{
	static char **tokv = NULL;
	static int tokn = 0;

	char buf[sizeof text], *s;
	int i, tokc = 0;
	size_t len, textsize;
	struct item *item, *lprefix, *lsubstr, *prefixend, *substrend;

	strcpy(buf, text);
	/* separate input text into tokens to be matched individually */
	for (s = strtok(buf, " "); s; tokv[tokc - 1] = s, s = strtok(NULL, " "))
		if (++tokc > tokn && !(tokv = realloc(tokv, ++tokn * sizeof *tokv)))
			die("cannot realloc %u bytes:", tokn * sizeof *tokv);
	len = tokc ? strlen(tokv[0]) : 0;

	matches = lprefix = lsubstr = matchend = prefixend = substrend = NULL;
	textsize = strlen(text);
	for (item = items; item && item->text; item++) {
		for (i = 0; i < tokc; i++)
			if (!fstrstr(item->text, tokv[i]))
				break;
		if (i != tokc) /* not all tokens match */
			continue;
		/* exact matches go first, then prefixes, then substrings */
		if (!tokc || !fstrncmp(text, item->text, textsize))
			appenditem(item, &matches, &matchend);
		else if (!fstrncmp(tokv[0], item->text, len))
			appenditem(item, &lprefix, &prefixend);
		else
			appenditem(item, &lsubstr, &substrend);
	}
	if (lprefix) {
		if (matches) {
			matchend->right = lprefix;
			lprefix->left = matchend;
		} else
			matches = lprefix;
		matchend = prefixend;
	}
	if (lsubstr) {
		if (matches) {
			matchend->right = lsubstr;
			lsubstr->left = matchend;
		} else
			matches = lsubstr;
		matchend = substrend;
	}
	curr = sel = matches;
	calcoffsets();
}

static void
insert(const char *str, ssize_t n)
{
	if (strlen(text) + n > sizeof text - 1)
		return;
	/* move existing text out of the way, insert new text, and update cursor */
	memmove(&text[cursor + n], &text[cursor], sizeof text - cursor - MAX(n, 0));
	if (n > 0)
		memcpy(&text[cursor], str, n);
	cursor += n;
	match();
}

static size_t
nextrune(int inc)
{
	ssize_t n;

	/* return location of next utf8 rune in the given direction (+1 or -1) */
	for (n = cursor + inc; n + inc >= 0 && (text[n] & 0xc0) == 0x80; n += inc)
		;
	return n;
}

static void
kbdkey(void *d, struct wl_keyboard *kbd, uint32_t serial, uint32_t time,
       uint32_t key, uint32_t state)
{
	char buf[32];
	int len;
	xkb_keysym_t ksym = XKB_KEY_NoSymbol;
	int ctrl = xkb_state_mod_index_is_active(xkb.state, xkb.ctrl, XKB_STATE_MODS_EFFECTIVE);
	int shift = xkb_state_mod_index_is_active(xkb.state, xkb.shift, XKB_STATE_MODS_EFFECTIVE);
	int alt = xkb_state_mod_index_is_active(xkb.state, xkb.alt, XKB_STATE_MODS_EFFECTIVE);

	if (state == WL_KEYBOARD_KEY_STATE_RELEASED)
		goto update_state;

	ksym = xkb_state_key_get_one_sym(xkb.state, key + 8);
	len = xkb_keysym_to_utf8(ksym, buf, sizeof buf) - 1;
	if (ctrl)
		switch(ksym) {
		case XKB_KEY_a: ksym = XKB_KEY_Home;      break;
		case XKB_KEY_b: ksym = XKB_KEY_Left;      break;
		case XKB_KEY_c: ksym = XKB_KEY_Escape;    break;
		case XKB_KEY_d: ksym = XKB_KEY_Delete;    break;
		case XKB_KEY_e: ksym = XKB_KEY_End;       break;
		case XKB_KEY_f: ksym = XKB_KEY_Right;     break;
		case XKB_KEY_g: ksym = XKB_KEY_Escape;    break;
		case XKB_KEY_h: ksym = XKB_KEY_BackSpace; break;
		case XKB_KEY_i: ksym = XKB_KEY_Tab;       break;
		case XKB_KEY_j: /* fallthrough */
		case XKB_KEY_J: /* fallthrough */
		case XKB_KEY_m: /* fallthrough */
		case XKB_KEY_M: ksym = XKB_KEY_Return; ctrl = 0; break;
		case XKB_KEY_n: ksym = XKB_KEY_Down;      break;
		case XKB_KEY_p: ksym = XKB_KEY_Up;        break;

		case XKB_KEY_k: /* delete right */
			text[cursor] = '\0';
			match();
			break;
		case XKB_KEY_u: /* delete left */
			insert(NULL, 0 - cursor);
			break;
		case XKB_KEY_w: /* delete word */
			while (cursor > 0 && strchr(worddelimiters, text[nextrune(-1)]))
				insert(NULL, nextrune(-1) - cursor);
			while (cursor > 0 && !strchr(worddelimiters, text[nextrune(-1)]))
				insert(NULL, nextrune(-1) - cursor);
			break;
		case XKB_KEY_y: /* paste selection */
		case XKB_KEY_Y:
			paste();
			return;
		case XKB_KEY_Return:
		case XKB_KEY_KP_Enter:
			break;
		case XKB_KEY_bracketleft:
			cleanup();
			exit(1);
		default:
			return;
		}
	else if (alt)
		switch(ksym) {
		case XKB_KEY_g: ksym = XKB_KEY_Home;  break;
		case XKB_KEY_G: ksym = XKB_KEY_End;   break;
		case XKB_KEY_h: ksym = XKB_KEY_Up;    break;
		case XKB_KEY_j: ksym = XKB_KEY_Next;  break;
		case XKB_KEY_k: ksym = XKB_KEY_Prior; break;
		case XKB_KEY_l: ksym = XKB_KEY_Down;  break;
		default:
			return;
		}
	switch (ksym) {
	default:
		if (!iscntrl(*buf))
			insert(buf, len);
		break;
	case XKB_KEY_Delete:
		if (text[cursor] == '\0')
			return;
		cursor = nextrune(+1);
		/* fallthrough */
	case XKB_KEY_BackSpace:
		if (cursor == 0)
			return;
		insert(NULL, nextrune(-1) - cursor);
		break;
	case XKB_KEY_End:
		if (text[cursor] != '\0') {
			cursor = strlen(text);
			break;
		}
		if (next) {
			/* jump to end of list and position items in reverse */
			curr = matchend;
			calcoffsets();
			curr = prev;
			calcoffsets();
			while (next && (curr = curr->right))
				calcoffsets();
		}
		sel = matchend;
		break;
	case XKB_KEY_Escape:
		cleanup();
		exit(1);
	case XKB_KEY_Home:
		if (sel == matches) {
			cursor = 0;
			break;
		}
		sel = curr = matches;
		calcoffsets();
		break;
	case XKB_KEY_Left:
		if (cursor > 0 && (!sel || !sel->left || lines > 0)) {
			cursor = nextrune(-1);
			break;
		}
		if (lines > 0)
			return;
		/* fallthrough */
	case XKB_KEY_Up:
		if (sel && sel->left && (sel = sel->left)->right == curr) {
			curr = prev;
			calcoffsets();
		}
		break;
	case XKB_KEY_Next:
		if (!next)
			return;
		sel = curr = next;
		calcoffsets();
		break;
	case XKB_KEY_Prior:
		if (!prev)
			return;
		sel = curr = prev;
		calcoffsets();
		break;
	case XKB_KEY_Return:
	case XKB_KEY_KP_Enter:
		puts((sel && !shift) ? sel->text : text);
		if (!ctrl) {
			cleanup();
			exit(0);
		}
		if(sel)
			sel->out = 1;
		break;
	case XKB_KEY_Right:
		if (text[cursor] != '\0') {
			cursor = nextrune(+1);
			break;
		}
		if (lines > 0)
			return;
		/* fallthrough */
	case XKB_KEY_Down:
		if (sel && sel->right && (sel = sel->right) == next) {
			curr = next;
			calcoffsets();
		}
		break;
	case XKB_KEY_Tab:
		if (!sel)
			return;
		strncpy(text, sel->text, sizeof text - 1);
		text[sizeof text - 1] = '\0';
		cursor = strlen(text);
		match();
		break;
	}
	drawmenu();

update_state:
	xkb_state_update_key(xkb.state, key + 8,
			     state == WL_KEYBOARD_KEY_STATE_PRESSED ? XKB_KEY_DOWN : XKB_KEY_UP);
}

static void
paste(void)
{
	int fds[2], len;
	char buf[BUFSIZ], *nl;

	if (seloffer) {
		pipe(fds);
		wl_data_offer_receive(seloffer, "text/plain", fds[1]);
		wl_display_flush(dpy);
		close(fds[1]);
		while((len = read(fds[0], buf, sizeof buf)) > 0)
			insert(buf, (nl = strchr(buf, '\n')) ? nl - buf : len);
		close(fds[0]);
		drawmenu();
	}
}

static void
readstdin(void)
{
	char buf[sizeof text], *p;
	size_t i, imax = 0, size = 0;
	unsigned int tmpmax = 0;

	/* read each line from stdin and add it to the item list */
	for (i = 0; fgets(buf, sizeof buf, stdin); i++) {
		if (i + 1 >= size / sizeof *items)
			if (!(items = realloc(items, (size += BUFSIZ))))
				die("cannot realloc %u bytes:", size);
		if ((p = strchr(buf, '\n')))
			*p = '\0';
		if (!(items[i].text = strdup(buf)))
			die("cannot strdup %u bytes:", strlen(buf) + 1);
		items[i].out = 0;
		drw_font_getexts(drw->fonts, buf, strlen(buf), &tmpmax, NULL);
		if (tmpmax > inputw) {
			inputw = tmpmax;
			imax = i;
		}
	}
	if (items)
		items[i].text = NULL;
	inputw = items ? TEXTW(items[imax].text) : 0;
	lines = MIN(lines, i);
}

static void
run(void)
{
	while (wl_display_dispatch(dpy) != -1)
		;
}

/* wayland event handlers */
static void
regglobal(void *d, struct wl_registry *r, uint32_t name, const char *interface, uint32_t version)
{
	if(strcmp(interface, "wl_compositor") == 0)
		compositor = wl_registry_bind(r, name, &wl_compositor_interface, 1);
	else if(strcmp(interface, "wl_shell") == 0)
		shell = wl_registry_bind(r, name, &wl_shell_interface, 1);
	else if(strcmp(interface, "wl_seat") == 0)
		seat = wl_registry_bind(r, name, &wl_seat_interface, 1);
	else if(strcmp(interface, "wl_data_device_manager") == 0)
		datadevman = wl_registry_bind(r, name, &wl_data_device_manager_interface, 1);
	else if(strcmp(interface, "swc_panel_manager") == 0)
		panelman = wl_registry_bind(r, name, &swc_panel_manager_interface, 1);
	else if (strcmp(interface, "swc_screen") == 0) {
		if (mon != -1 && mon-- == 0)
			screen = wl_registry_bind(r, name, &swc_screen_interface, 1);
	}
}

static void
regglobalremove(void *d, struct wl_registry *reg, uint32_t name)
{
}

static const struct wl_registry_listener reglistener = { regglobal, regglobalremove };

static void
kbdenter(void *data, struct wl_keyboard *kbd, uint32_t serial,
         struct wl_surface *surface, struct wl_array *keys)
{
}

static void
kbdleave(void *d, struct wl_keyboard *kbd, uint32_t serial,
         struct wl_surface *surface)
{
}

/* kbdkey is defined above to reduce merge conflicts */

static void
kbdkeymap(void *d, struct wl_keyboard *kbd, uint32_t format, int32_t fd, uint32_t size)
{
	char *string;

	if (format != WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1) {
		close(fd);
		return;
	}

	string = mmap(NULL, size, PROT_READ, MAP_SHARED, fd, 0);

	if (string == MAP_FAILED) {
		close(fd);
		return;
	}

	xkb.keymap = xkb_keymap_new_from_string(xkb.context, string,
						XKB_KEYMAP_FORMAT_TEXT_V1, 0);
	munmap(string, size);
	close(fd);
	xkb.state = xkb_state_new(xkb.keymap);

	xkb.ctrl = xkb_keymap_mod_get_index(xkb.keymap, XKB_MOD_NAME_CTRL);
	xkb.alt = xkb_keymap_mod_get_index(xkb.keymap, XKB_MOD_NAME_ALT);
	xkb.shift = xkb_keymap_mod_get_index(xkb.keymap, XKB_MOD_NAME_SHIFT);
}

static void
kbdmodifiers(void *d, struct wl_keyboard *kbd, uint32_t serial, uint32_t dep,
             uint32_t lat, uint32_t lck, uint32_t grp)
{
	xkb_state_update_mask(xkb.state, dep, lat, lck, grp, 0, 0);
}

static const struct wl_keyboard_listener kbdlistener = {
	kbdkeymap, kbdenter, kbdleave, kbdkey, kbdmodifiers,
};

static void
dataofferoffer(void *d, struct wl_data_offer *offer, const char *mimetype)
{
	if (strncmp(mimetype, "text/plain", 10) == 0)
		wl_data_offer_set_user_data(offer, (void *)(uintptr_t) 1);
}

static const struct wl_data_offer_listener dataofferlistener = { dataofferoffer };

static void
datadevoffer(void *d, struct wl_data_device *datadev, struct wl_data_offer *offer)
{
	wl_data_offer_add_listener(offer, &dataofferlistener, NULL);
}

static void
datadeventer(void *d, struct wl_data_device *datadev, uint32_t serial,
             struct wl_surface *surface, wl_fixed_t x, wl_fixed_t y,
             struct wl_data_offer *offer)
{
}

static void
datadevleave(void *d, struct wl_data_device *datadev)
{
}

static void
datadevmotion(void *d, struct wl_data_device *datadev, uint32_t time,
              wl_fixed_t x, wl_fixed_t y)
{
}

static void
datadevdrop(void *d, struct wl_data_device *datadev)
{
}

static void
datadevselection(void *d, struct wl_data_device *datadev, struct wl_data_offer *offer)
{
	if (offer && (uintptr_t) wl_data_offer_get_user_data(offer) == 1)
		seloffer = offer;
}

static const struct wl_data_device_listener datadevlistener = {
	datadevoffer, datadeventer, datadevleave, datadevmotion, datadevdrop,
	datadevselection,
};

static void
paneldocked(void *d, struct swc_panel *panel, uint32_t length)
{
	mw = length;
}

static const struct swc_panel_listener panellistener = { paneldocked };

static void
setup(void)
{
	if (!compositor || !seat || !panelman)
		exit(1);

	kbd = wl_seat_get_keyboard(seat);
	wl_keyboard_add_listener(kbd, &kbdlistener, NULL);
	datadev = wl_data_device_manager_get_data_device(datadevman, seat);
	wl_data_device_add_listener(datadev, &datadevlistener, NULL);

	xkb.context = xkb_context_new(0);

	/* init appearance */
	scheme[SchemeNorm] = drw_scm_create(drw, colors[SchemeNorm], 2);
	scheme[SchemeSel] = drw_scm_create(drw, colors[SchemeSel], 2);
	scheme[SchemeOut] = drw_scm_create(drw, colors[SchemeOut], 2);

	/* calculate menu geometry */
	bh = drw->fonts->wld->height + 2;
	lines = MAX(lines, 0);
	mh = (lines + 1) * bh;

	/* create menu surface */
	surface = wl_compositor_create_surface(compositor);

	panel = swc_panel_manager_create_panel(panelman, surface);
	swc_panel_add_listener(panel, &panellistener, NULL);
	swc_panel_dock(panel, topbar ? SWC_PANEL_EDGE_TOP : SWC_PANEL_EDGE_BOTTOM, screen, 1);

	wl_display_roundtrip(dpy);
	if (!mw)
		exit(1);

	promptw = (prompt && *prompt) ? TEXTW(prompt) - lrpad / 4 : 0;
	inputw = MIN(inputw, mw/3);
	match();

	drw_resize(drw, surface, mw, mh);
	drawmenu();
}

static void
usage(void)
{
	fputs("usage: dmenu [-biv] [-l lines] [-p prompt] [-fn font] [-m monitor]\n"
	      "             [-nb color] [-nf color] [-sb color] [-sf color]\n", stderr);
	exit(1);
}

int
main(int argc, char *argv[])
{
	struct wl_registry *reg;
	int i;

	for (i = 1; i < argc; i++)
		/* these options take no arguments */
		if (!strcmp(argv[i], "-v")) {      /* prints version information */
			puts("dmenu-"VERSION);
			exit(0);
		} else if (!strcmp(argv[i], "-b")) /* appears at the bottom of the screen */
			topbar = 0;
		else if (!strcmp(argv[i], "-i")) { /* case-insensitive item matching */
			fstrncmp = strncasecmp;
			fstrstr = cistrstr;
		} else if (i + 1 == argc)
			usage();
		/* these options take one argument */
		else if (!strcmp(argv[i], "-l"))   /* number of lines in vertical list */
			lines = atoi(argv[++i]);
		else if (!strcmp(argv[i], "-m"))
			mon = atoi(argv[++i]);
		else if (!strcmp(argv[i], "-p"))   /* adds prompt to left of input field */
			prompt = argv[++i];
		else if (!strcmp(argv[i], "-fn"))  /* font or font set */
			fonts[0] = argv[++i];
		else if (!strcmp(argv[i], "-nb"))  /* normal background color */
			colors[SchemeNorm][ColBg] = argv[++i];
		else if (!strcmp(argv[i], "-nf"))  /* normal foreground color */
			colors[SchemeNorm][ColFg] = argv[++i];
		else if (!strcmp(argv[i], "-sb"))  /* selected background color */
			colors[SchemeSel][ColBg] = argv[++i];
		else if (!strcmp(argv[i], "-sf"))  /* selected foreground color */
			colors[SchemeSel][ColFg] = argv[++i];
		else
			usage();

	if (!setlocale(LC_CTYPE, ""))
		fputs("warning: no locale support\n", stderr);
	if (!(dpy = wl_display_connect(NULL)))
		die("cannot open display");
	if (!(reg = wl_display_get_registry(dpy)))
		die("cannot get registry");
	wl_registry_add_listener(reg, &reglistener, NULL);
	wl_display_roundtrip(dpy);
	drw = drw_create(dpy);
	if (!drw_fontset_create(drw, fonts, LENGTH(fonts)))
		die("no fonts could be loaded.");
	lrpad = drw->fonts->wld->height;

	readstdin();
	setup();
	run();

	return 1; /* unreachable */
}
