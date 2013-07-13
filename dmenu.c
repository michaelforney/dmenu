/* See LICENSE file for copyright and license details. */
#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/mman.h>
#include <unistd.h>
#include <wayland-client.h>
#include <wld/wayland.h>
#include <wld/wld.h>
#include <xkbcommon/xkbcommon.h>
#include "draw.h"

#define INTERSECT(x,y,w,h,r)  (MAX(0, MIN((x)+(w),(r).x_org+(r).width)  - MAX((x),(r).x_org)) \
                             * MAX(0, MIN((y)+(h),(r).y_org+(r).height) - MAX((y),(r).y_org)))
#define MIN(a,b)              ((a) < (b) ? (a) : (b))
#define MAX(a,b)              ((a) > (b) ? (a) : (b))

typedef struct Item Item;
struct Item {
	char *text;
	Item *left, *right;
	bool out;
};

typedef struct XKB XKB;
struct XKB {
	struct xkb_context *context;
	struct xkb_state *state;
	struct xkb_keymap *keymap;
	xkb_mod_index_t ctrl, alt, shift;
};

static void appenditem(Item *item, Item **list, Item **last);
static void calcoffsets(void);
static char *cistrstr(const char *s, const char *sub);
static void datadevoffer(void *data, struct wl_data_device *datadev,
			 struct wl_data_offer *offer);
static void datadeventer(void *data, struct wl_data_device *datadev,
			 uint32_t serial, struct wl_surface *surf, wl_fixed_t x,
			 wl_fixed_t y, struct wl_data_offer *offer);
static void datadevleave(void *data, struct wl_data_device *datadev);
static void datadevmotion(void *data, struct wl_data_device *datadev,
			  uint32_t time, wl_fixed_t x, wl_fixed_t y);
static void datadevdrop(void *data, struct wl_data_device *datadev);
static void datadevselection(void *data, struct wl_data_device *datadev,
			     struct wl_data_offer *offer);
static void dataofferoffer(void *data, struct wl_data_offer *offer,
			   const char * mimetype);
static void drawmenu(void);
static void insert(const char *str, ssize_t n);
static void kbdenter(void *data, struct wl_keyboard *kbd, uint32_t serial,
		     struct wl_surface *surf, struct wl_array * keys);
static void kbdkey(void *data, struct wl_keyboard *kbd, uint32_t serial,
		   uint32_t time, uint32_t key, uint32_t state);
static void kbdleave(void *data, struct wl_keyboard *kbd, uint32_t serial,
		     struct wl_surface *surf);
static void kbdmodifiers(void *data, struct wl_keyboard *kbd, uint32_t serial,
			 uint32_t dep, uint32_t lat, uint32_t lck, uint32_t grp);
static void kbdkeymap(void *data, struct wl_keyboard *kbd, uint32_t format,
		      int32_t fd, uint32_t size);
static void match(void);
static size_t nextrune(int inc);
static void outputgeometry(void *data, struct wl_output *output,
			   int32_t x, int32_t y, int32_t physw, int32_t physh,
			   int32_t subpixel, const char *make, const char *model,
			   int32_t transform);
static void outputmode(void *data, struct wl_output *output, uint32_t flags,
		       int32_t w, int32_t h, int32_t refresh);
static void outputdone(void *data, struct wl_output *output);
static void outputscale(void *data, struct wl_output *output, int32_t factor);
static void paste(void);
static void readstdin(void);
static void regglobal(void *data, struct wl_registry *reg, uint32_t name,
		      const char *interface, uint32_t version);
static void regglobalremove(void * data, struct wl_registry *reg, uint32_t name);
static void run(void);
static void setup(void);
static void surfenter(void *data, struct wl_surface *surf, struct wl_output *output);
static void surfleave(void *data, struct wl_surface *surf, struct wl_output *output);
static void usage(void);

static char text[BUFSIZ] = "";
static int bh, mw, mh;
static int inputw, promptw;
static size_t cursor = 0;
static unsigned long normcol[ColLast];
static unsigned long selcol[ColLast];
static unsigned long outcol[ColLast];
static DC *dc;
static Item *items = NULL;
static Item *matches, *matchend;
static Item *prev, *curr, *next, *sel;
static struct wl_compositor *comp;
static struct wl_keyboard *kbd;
static struct wl_seat *seat;
static struct wl_shell *shell;
static struct wl_surface *surf;
static struct wl_shell_surface *shellsurf;
static struct wl_data_device_manager *datadevman;
static struct wl_data_device *datadev;
static struct wl_data_offer *seloffer;
static XKB xkb;
static const struct wl_registry_listener reglistener
	= { regglobal, regglobalremove };
static const struct wl_surface_listener surflistener
	= { surfenter, surfleave };
static const struct wl_keyboard_listener kbdlistener
	= { kbdkeymap, kbdenter, kbdleave, kbdkey, kbdmodifiers };
static const struct wl_data_device_listener datadevlistener = {
	datadevoffer, datadeventer, datadevleave, datadevmotion, datadevdrop,
	datadevselection
};
static const struct wl_data_offer_listener dataofferlistener
	= { dataofferoffer };
static const struct wl_output_listener outputlistener
	= { outputgeometry, outputmode, outputdone, outputscale };

#include "config.h"

static int (*fstrncmp)(const char *, const char *, size_t) = strncmp;
static char *(*fstrstr)(const char *, const char *) = strstr;

int
main(int argc, char *argv[]) {
	bool __attribute__ ((unused)) fast = false;
	int i;

	for(i = 1; i < argc; i++)
		/* these options take no arguments */
		if(!strcmp(argv[i], "-v")) {      /* prints version information */
			puts("dmenu-"VERSION", © 2006-2012 dmenu engineers, see LICENSE for details");
			exit(EXIT_SUCCESS);
		}
		else if(!strcmp(argv[i], "-b"))   /* appears at the bottom of the screen */
			topbar = false;
		else if(!strcmp(argv[i], "-f"))   /* grabs keyboard before reading stdin */
			fast = true;
		else if(!strcmp(argv[i], "-i")) { /* case-insensitive item matching */
			fstrncmp = strncasecmp;
			fstrstr = cistrstr;
		}
		else if(i+1 == argc)
			usage();
		/* these options take one argument */
		else if(!strcmp(argv[i], "-l"))   /* number of lines in vertical list */
			lines = atoi(argv[++i]);
		else if(!strcmp(argv[i], "-p"))   /* adds prompt to left of input field */
			prompt = argv[++i];
		else if(!strcmp(argv[i], "-fn"))  /* font or font set */
			font = argv[++i];
		else if(!strcmp(argv[i], "-nb"))  /* normal background color */
			normbgcolor = argv[++i];
		else if(!strcmp(argv[i], "-nf"))  /* normal foreground color */
			normfgcolor = argv[++i];
		else if(!strcmp(argv[i], "-sb"))  /* selected background color */
			selbgcolor = argv[++i];
		else if(!strcmp(argv[i], "-sf"))  /* selected foreground color */
			selfgcolor = argv[++i];
		else
			usage();

	dc = initdc();
	initfont(dc, font);

	readstdin();
	setup();
	run();

	return 1; /* unreachable */
}

void
appenditem(Item *item, Item **list, Item **last) {
	if(*last)
		(*last)->right = item;
	else
		*list = item;

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
	/* calculate which items will begin the next page and previous page */
	for(i = 0, next = curr; next; next = next->right)
		if((i += (lines > 0) ? bh : MIN(textw(dc, next->text), n)) > n)
			break;
	for(i = 0, prev = curr; prev && prev->left; prev = prev->left)
		if((i += (lines > 0) ? bh : MIN(textw(dc, prev->left->text), n)) > n)
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
dataofferoffer(void *data, struct wl_data_offer *offer, const char *mimetype) {
	if (strncmp(mimetype, "text/plain", 10) == 0)
		wl_data_offer_set_user_data(offer, (void *)(uintptr_t) 1);
}

void
datadevoffer(void *data, struct wl_data_device *datadev,
	     struct wl_data_offer *offer) {
	     wl_data_offer_add_listener(offer, &dataofferlistener, NULL);
}

void
datadeventer(void *data, struct wl_data_device *datadev, uint32_t serial,
	     struct wl_surface *surf, wl_fixed_t x, wl_fixed_t y,
	     struct wl_data_offer *offer) {
}

void
datadevleave(void *data, struct wl_data_device *datadev) {
}

void
datadevmotion(void *data, struct wl_data_device *datadev, uint32_t time,
	      wl_fixed_t x, wl_fixed_t y) {
}

void
datadevdrop(void *data, struct wl_data_device *datadev) {
}

void
datadevselection(void *data, struct wl_data_device *datadev,
		 struct wl_data_offer *offer) {
	if (offer && (uintptr_t) wl_data_offer_get_user_data(offer) == 1)
		seloffer = offer;
}

void
drawmenu(void) {
	int curpos;
	Item *item;

	dc->x = 0;
	dc->y = 0;
	dc->h = bh;
	drawrect(dc, 0, 0, mw, mh, true, BG(dc, normcol));

	if(prompt && *prompt) {
		dc->w = promptw;
		drawtext(dc, prompt, selcol);
		dc->x = dc->w;
	}
	/* draw input field */
	dc->w = (lines > 0 || !matches) ? mw - dc->x : inputw;
	drawtext(dc, text, normcol);
	if((curpos = textnw(dc, text, cursor) + dc->h/2 - 2) < dc->w)
		drawrect(dc, curpos, 2, 1, dc->h - 4, true, FG(dc, normcol));

	if(lines > 0) {
		/* draw vertical list */
		dc->w = mw - dc->x;
		for(item = curr; item != next; item = item->right) {
			dc->y += dc->h;
			drawtext(dc, item->text, (item == sel) ? selcol :
			                         (item->out)   ? outcol : normcol);
		}
	}
	else if(matches) {
		/* draw horizontal list */
		dc->x += inputw;
		dc->w = textw(dc, "<");
		if(curr->left)
			drawtext(dc, "<", normcol);
		for(item = curr; item != next; item = item->right) {
			dc->x += dc->w;
			dc->w = MIN(textw(dc, item->text), mw - dc->x - textw(dc, ">"));
			drawtext(dc, item->text, (item == sel) ? selcol :
			                         (item->out)   ? outcol : normcol);
		}
		dc->w = textw(dc, ">");
		dc->x = mw - dc->w;
		if(next)
			drawtext(dc, ">", normcol);
	}
	mapdc(dc, surf, mw, mh);
}

void
insert(const char *str, ssize_t n) {
	if(strlen(text) + n > sizeof text - 1)
		return;
	/* move existing text out of the way, insert new text, and update cursor */
	memmove(&text[cursor + n], &text[cursor], sizeof text - cursor - MAX(n, 0));
	if(n > 0)
		memcpy(&text[cursor], str, n);
	cursor += n;
	match();
}

void
kbdenter(void *data, struct wl_keyboard *kbd, uint32_t serial,
	 struct wl_surface *surf, struct wl_array * keys) {
}

void
kbdleave(void *data, struct wl_keyboard *kbd, uint32_t serial,
	 struct wl_surface *surf) {
}

void
kbdkey(void *data, struct wl_keyboard *kbd, uint32_t serial, uint32_t time,
       uint32_t key, uint32_t state) {
	char buf[32];
	int len;
	xkb_keysym_t ksym = XKB_KEY_NoSymbol;

	if (state == WL_KEYBOARD_KEY_STATE_RELEASED)
		goto update_state;

	ksym = xkb_state_key_get_one_sym(xkb.state, key + 8);
	len = xkb_keysym_to_utf8(ksym, buf, sizeof buf) - 1;
	if(xkb_state_mod_index_is_active(xkb.state, xkb.ctrl, XKB_STATE_MODS_EFFECTIVE))
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
		case XKB_KEY_J: ksym = XKB_KEY_Return;    break;
		case XKB_KEY_m: /* fallthrough */
		case XKB_KEY_M: ksym = XKB_KEY_Return;    break;
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
			while(cursor > 0 && text[nextrune(-1)] == ' ')
				insert(NULL, nextrune(-1) - cursor);
			while(cursor > 0 && text[nextrune(-1)] != ' ')
				insert(NULL, nextrune(-1) - cursor);
			break;
		case XKB_KEY_y: /* paste selection */
			paste();
			return;
		case XKB_KEY_Return:
		case XKB_KEY_KP_Enter:
			break;
		case XKB_KEY_bracketleft:
			exit(EXIT_FAILURE);
		default:
			return;
		}
	else if(xkb_state_mod_index_is_active(xkb.state, xkb.alt, XKB_STATE_MODS_EFFECTIVE))
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
	switch(ksym) {
	default:
		if(!iscntrl(*buf))
			insert(buf, len);
		break;
	case XKB_KEY_Delete:
		if(text[cursor] == '\0')
			return;
		cursor = nextrune(+1);
		/* fallthrough */
	case XKB_KEY_BackSpace:
		if(cursor == 0)
			return;
		insert(NULL, nextrune(-1) - cursor);
		break;
	case XKB_KEY_End:
		if(text[cursor] != '\0') {
			cursor = strlen(text);
			break;
		}
		if(next) {
			/* jump to end of list and position items in reverse */
			curr = matchend;
			calcoffsets();
			curr = prev;
			calcoffsets();
			while(next && (curr = curr->right))
				calcoffsets();
		}
		sel = matchend;
		break;
	case XKB_KEY_Escape:
		exit(EXIT_FAILURE);
	case XKB_KEY_Home:
		if(sel == matches) {
			cursor = 0;
			break;
		}
		sel = curr = matches;
		calcoffsets();
		break;
	case XKB_KEY_Left:
		if(cursor > 0 && (!sel || !sel->left || lines > 0)) {
			cursor = nextrune(-1);
			break;
		}
		if(lines > 0)
			return;
		/* fallthrough */
	case XKB_KEY_Up:
		if(sel && sel->left && (sel = sel->left)->right == curr) {
			curr = prev;
			calcoffsets();
		}
		break;
	case XKB_KEY_Next:
		if(!next)
			return;
		sel = curr = next;
		calcoffsets();
		break;
	case XKB_KEY_Prior:
		if(!prev)
			return;
		sel = curr = prev;
		calcoffsets();
		break;
	case XKB_KEY_Return:
	case XKB_KEY_KP_Enter:
		if(sel && !xkb_state_mod_index_is_active(xkb.state, xkb.shift, XKB_STATE_MODS_EFFECTIVE))
		    puts(sel->text);
		else
		    puts(text);
		if(!xkb_state_mod_index_is_active(xkb.state, xkb.ctrl, XKB_STATE_MODS_EFFECTIVE))
			exit(EXIT_SUCCESS);
		sel->out = true;
		break;
	case XKB_KEY_Right:
		if(text[cursor] != '\0') {
			cursor = nextrune(+1);
			break;
		}
		if(lines > 0)
			return;
		/* fallthrough */
	case XKB_KEY_Down:
		if(sel && sel->right && (sel = sel->right) == next) {
			curr = next;
			calcoffsets();
		}
		break;
	case XKB_KEY_Tab:
		if(!sel)
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

void
kbdkeymap(void *data, struct wl_keyboard *kbd, uint32_t format, int32_t fd, uint32_t size) {
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

void
kbdmodifiers(void *data, struct wl_keyboard *kbd, uint32_t serial,
		      uint32_t dep, uint32_t lat, uint32_t lck, uint32_t grp) {
	xkb_state_update_mask(xkb.state, dep, lat, lck, grp, 0, 0);
}

void
match(void) {
	static char **tokv = NULL;
	static int tokn = 0;

	char buf[sizeof text], *s;
	int i, tokc = 0;
	size_t len;
	Item *item, *lprefix, *lsubstr, *prefixend, *substrend;

	strcpy(buf, text);
	/* separate input text into tokens to be matched individually */
	for(s = strtok(buf, " "); s; tokv[tokc-1] = s, s = strtok(NULL, " "))
		if(++tokc > tokn && !(tokv = realloc(tokv, ++tokn * sizeof *tokv)))
			eprintf("cannot realloc %u bytes\n", tokn * sizeof *tokv);
	len = tokc ? strlen(tokv[0]) : 0;

	matches = lprefix = lsubstr = matchend = prefixend = substrend = NULL;
	for(item = items; item && item->text; item++) {
		for(i = 0; i < tokc; i++)
			if(!fstrstr(item->text, tokv[i]))
				break;
		if(i != tokc) /* not all tokens match */
			continue;
		/* exact matches go first, then prefixes, then substrings */
		if(!tokc || !fstrncmp(tokv[0], item->text, len+1))
			appenditem(item, &matches, &matchend);
		else if(!fstrncmp(tokv[0], item->text, len))
			appenditem(item, &lprefix, &prefixend);
		else
			appenditem(item, &lsubstr, &substrend);
	}
	if(lprefix) {
		if(matches) {
			matchend->right = lprefix;
			lprefix->left = matchend;
		}
		else
			matches = lprefix;
		matchend = prefixend;
	}
	if(lsubstr) {
		if(matches) {
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

	/* return location of next utf8 rune in the given direction (+1 or -1) */
	for(n = cursor + inc; n + inc >= 0 && (text[n] & 0xc0) == 0x80; n += inc);
	return n;
}

void
outputgeometry(void *data, struct wl_output *output, int32_t x, int32_t y,
	       int32_t physw, int32_t physh, int32_t subpixel,
	       const char *make, const char *model, int32_t transform) {
}

void
outputmode(void *data, struct wl_output *output, uint32_t flags,
	   int32_t w, int32_t h, int32_t refresh) {
	if(flags & WL_OUTPUT_MODE_CURRENT) {
		mw = mw ? MIN(mw, w) : w;
		wl_output_set_user_data(output, (void *)(uintptr_t) w);
	}
}

void
outputdone(void *data, struct wl_output *output) {
}

void
outputscale(void *data, struct wl_output *output, int32_t factor) {
}

void
paste(void) {
	int fds[2], len;
	char buf[BUFSIZ], *nl;

	if (seloffer) {
		pipe(fds);
		wl_data_offer_receive(seloffer, "text/plain", fds[1]);
		wl_display_flush(dc->dpy);
		close(fds[1]);
		while((len = read(fds[0], buf, sizeof buf)) > 0)
			insert(buf, (nl = strchr(buf, '\n')) ? nl-buf : len);
		close(fds[0]);
		drawmenu();
	}
}

void
readstdin(void) {
	char buf[sizeof text], *p, *maxstr = NULL;
	size_t i, max = 0, size = 0;

	/* read each line from stdin and add it to the item list */
	for(i = 0; fgets(buf, sizeof buf, stdin); i++) {
		if(i+1 >= size / sizeof *items)
			if(!(items = realloc(items, (size += BUFSIZ))))
				eprintf("cannot realloc %u bytes:", size);
		if((p = strchr(buf, '\n')))
			*p = '\0';
		if(!(items[i].text = strdup(buf)))
			eprintf("cannot strdup %u bytes:", strlen(buf)+1);
		items[i].out = false;
		if(strlen(items[i].text) > max)
			max = strlen(maxstr = items[i].text);
	}
	if(items)
		items[i].text = NULL;
	inputw = maxstr ? textw(dc, maxstr) : 0;
	lines = MIN(lines, i);
}

void
regglobal(void *data, struct wl_registry *reg, uint32_t name,
       const char *interface, uint32_t version) {
	if(strcmp(interface, "wl_compositor") == 0)
		comp = wl_registry_bind(reg, name, &wl_compositor_interface, 1);
	else if(strcmp(interface, "wl_shell") == 0)
		shell = wl_registry_bind(reg, name, &wl_shell_interface, 1);
	else if(strcmp(interface, "wl_seat") == 0)
		seat = wl_registry_bind(reg, name, &wl_seat_interface, 1);
	else if(strcmp(interface, "wl_data_device_manager") == 0)
		datadevman = wl_registry_bind(reg, name, &wl_data_device_manager_interface, 1);
	else if(strcmp(interface, "wl_output") == 0) {
		struct wl_output *output;
		output = wl_registry_bind(reg, name, &wl_output_interface, 1);
		wl_output_add_listener(output, &outputlistener, NULL);
	}
}

void
regglobalremove(void * data, struct wl_registry *reg, uint32_t name) {
}

void
run(void) {
	while(wl_display_dispatch(dc->dpy) != -1);
}

void
setup(void) {
	struct wl_registry *reg;

	reg = wl_display_get_registry(dc->dpy);
	wl_registry_add_listener(reg, &reglistener, NULL);
	wl_display_roundtrip(dc->dpy);

	if (!seat)
		exit(EXIT_FAILURE);

	kbd = wl_seat_get_keyboard(seat);
	wl_keyboard_add_listener(kbd, &kbdlistener, NULL);
	datadev = wl_data_device_manager_get_data_device(datadevman, seat);
	wl_data_device_add_listener(datadev, &datadevlistener, NULL);

	xkb.context = xkb_context_new(0);

	normcol[ColBG] = getcolor(dc, normbgcolor);
	normcol[ColFG] = getcolor(dc, normfgcolor);
	selcol[ColBG]  = getcolor(dc, selbgcolor);
	selcol[ColFG]  = getcolor(dc, selfgcolor);
	outcol[ColBG]  = getcolor(dc, outbgcolor);
	outcol[ColFG]  = getcolor(dc, outfgcolor);

	/* calculate menu geometry */
	bh = dc->font->height + 2;
	lines = MAX(lines, 0);
	mh = (lines + 1) * bh;

	/* wait for output modes */
	wl_display_roundtrip(dc->dpy);

	promptw = (prompt && *prompt) ? textw(dc, prompt) : 0;
	inputw = MIN(inputw, mw/3);
	match();

	/* create menu surface */
	surf = wl_compositor_create_surface(comp);
	wl_surface_add_listener(surf, &surflistener, NULL);

	dc->drawable = wld_wayland_create_drawable(dc->ctx, surf, mw, mh,
						   WLD_FORMAT_XRGB8888, 0);

	shellsurf = wl_shell_get_shell_surface(shell, surf);
	wl_shell_surface_set_toplevel(shellsurf);
	drawmenu();
}

void
surfenter(void *data, struct wl_surface *surf, struct wl_output *output) {
	int outputw;
	if ((outputw = (uintptr_t) wl_output_get_user_data(output))) {
		mw = outputw;
		resizedc(dc, surf, mw, mh);
		drawmenu();
	}
}

void
surfleave(void *data, struct wl_surface *surf, struct wl_output *output) {
}

void
usage(void) {
	fputs("usage: dmenu [-b] [-f] [-i] [-l lines] [-p prompt] [-fn font]\n"
	      "             [-nb color] [-nf color] [-sb color] [-sf color] [-v]\n", stderr);
	exit(EXIT_FAILURE);
}
