/* See LICENSE file for copyright and license details. */
#include <locale.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wayland-client.h>
#include <wld/wld.h>
#include <wld/wayland.h>
#include "draw.h"

#define MAX(a, b)  ((a) > (b) ? (a) : (b))
#define MIN(a, b)  ((a) < (b) ? (a) : (b))
#define DEFAULTFN  "fixed"

void
drawrect(DC *dc, int x, int y, unsigned int w, unsigned int h, bool fill, unsigned long color) {
	if (fill) {
		wld_fill_rectangle(dc->drawable, color, dc->x + x, dc->y + y, w, h);
	}
	else {
		wld_fill_rectangle(dc->drawable, color, dc->x + x, dc->y + y, w, 1);
		wld_fill_rectangle(dc->drawable, color, dc->x + x + w - 1, dc->y + y + 1, 1, h - 2);
		wld_fill_rectangle(dc->drawable, color, dc->x + x, dc->y + y + 1, 1, h - 2);
		wld_fill_rectangle(dc->drawable, color, dc->x + x, dc->y + y - 1, w, 1);
	}
}

void
drawtext(DC *dc, const char *text, unsigned long col[ColLast]) {
	char buf[BUFSIZ];
	size_t mn, n = strlen(text);

	/* shorten text if necessary */
	for(mn = MIN(n, sizeof buf); textnw(dc, text, mn) + dc->font->height/2 > dc->w; mn--)
		if(mn == 0)
			return;
	memcpy(buf, text, mn);
	if(mn < n)
		for(n = MAX(mn-3, 0); n < mn; buf[n++] = '.');

	drawrect(dc, 0, 0, dc->w, dc->h, true, BG(dc, col));
	drawtextn(dc, buf, mn, col);
}

void
drawtextn(DC *dc, const char *text, size_t n, unsigned long col[ColLast]) {
	int x = dc->x + dc->font->height/2;
	int y = dc->y + dc->font->ascent+1;

        wld_draw_text_utf8_n(dc->drawable, dc->font, FG(dc, col), x, y, text, n, NULL);
}

void
eprintf(const char *fmt, ...) {
	va_list ap;

	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);

	if(fmt[0] != '\0' && fmt[strlen(fmt)-1] == ':') {
		fputc(' ', stderr);
		perror(NULL);
	}
	exit(EXIT_FAILURE);
}

void
freedc(DC *dc) {
	wld_destroy_drawable(dc->drawable);
	wld_wayland_destroy_context(dc->ctx);
	wld_font_close(dc->font);
	wld_font_destroy_context(dc->fontctx);
	free(dc);
}

unsigned long
getcolor(DC *dc, const char *colstr) {
	uint32_t color;

	if(!wld_lookup_named_color(colstr, &color))
		eprintf("cannot allocate color '%s'\n", colstr);
	return color;
}

DC *
initdc(void) {
	DC *dc;

	if(!setlocale(LC_CTYPE, ""))
		fputs("no locale support\n", stderr);
	if(!(dc = calloc(1, sizeof *dc)))
		eprintf("cannot malloc %u bytes:", sizeof *dc);
	if(!(dc->dpy = wl_display_connect(NULL)))
		eprintf("cannot open display\n");

	dc->ctx = wld_wayland_create_context(dc->dpy, WLD_ANY);
	dc->fontctx = wld_font_create_context();
	return dc;
}

void
initfont(DC *dc, const char *fontstr) {
	dc->font = wld_font_open_name(dc->fontctx, fontstr ? fontstr : DEFAULTFN);

	if (!dc->font)
		eprintf("cannot load font '%s'\n", fontstr ? fontstr : DEFAULTFN);
}

void
mapdc(DC *dc, struct wl_surface *surface, unsigned int w, unsigned int h) {
	wl_surface_damage(surface, 0, 0, w, h);
	wld_flush(dc->drawable);
}

void
resizedc(DC *dc, struct wl_surface *surf, unsigned int w, unsigned int h) {
        if (dc->drawable->width == w && dc->drawable->height == h)
                return;
	wld_destroy_drawable(dc->drawable);
	dc->drawable = wld_wayland_create_drawable(dc->ctx, surf, w, h,
						   WLD_FORMAT_XRGB8888, 0);
}

int
textnw(DC *dc, const char *text, size_t len) {
	struct wld_extents extents;
	wld_font_text_extents_utf8_n(dc->font, text, len, &extents);
	return extents.advance;
}

int
textw(DC *dc, const char *text) {
	return textnw(dc, text, strlen(text)) + dc->font->height;
}
