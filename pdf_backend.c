/* MuGreen2 MuPDF PDF backend.
 * SPDX-License-Identifier: AGPL-3.0-only
 * This backend is part of the combined MuGreen2 application; see LICENSE.
 * MuGreen2 backend/interface additions: Copyright (C) 2026 MuGreen2 contributors.
 */
/* pdf_backend.c - see pdf_backend.h. Tested against the MuPDF 1.2x API; not yet compiled
 * on the target: if a signature differs in your version, the compiler will point at it. */
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <mupdf/fitz.h>
#include "pdf_backend.h"

/* fz_search_page() gained a hit_mark argument around 1.23. Override with -DPB_SEARCH_HAS_MARK=0/1 */
#ifndef PB_SEARCH_HAS_MARK
# if defined(FZ_VERSION_MAJOR) && (FZ_VERSION_MAJOR > 1 || FZ_VERSION_MINOR >= 23)
#  define PB_SEARCH_HAS_MARK 1
# else
#  define PB_SEARCH_HAS_MARK 0
# endif
#endif

struct pb_doc {
	fz_context	*ctx;
	fz_document	*doc;
	int		page_count;
	/* cached page */
	fz_display_list	*list;
	int		list_page;
	fz_rect		bounds;		/* page bounds (may not start at 0,0) */
};

static fz_context *g_ctx;
static int   g_refs;
static size_t g_store = 32u << 20;
static int   g_aa = 8;
static uint8_t *g_scratch;	/* BGRA scratch for the RGB565 path */
static size_t g_scratch_sz;

const char *pb_backend_name(void)
{
	return "mupdf";
}

void pb_configure(size_t store_bytes, int aa_bits)
{
	if (store_bytes)
		g_store = store_bytes;
	if (aa_bits >= 0 && aa_bits <= 8)
		g_aa = aa_bits;
}

static fz_context *ctx_acquire(void)
{
	if (!g_ctx)
	{
		fz_context *c = fz_new_context(NULL, NULL, g_store);
		if (!c)
			return NULL;
		fz_try(c)
		{
			fz_register_document_handlers(c);
			fz_set_aa_level(c, g_aa);
		}
		fz_catch(c)
		{
			fz_drop_context(c);
			return NULL;
		}
		g_ctx = c;
	}
	g_refs++;
	return g_ctx;
}

static void ctx_release(void)
{
	if (--g_refs <= 0 && g_ctx)
	{
		fz_drop_context(g_ctx);
		g_ctx = NULL;
		g_refs = 0;
		free(g_scratch);
		g_scratch = NULL;
		g_scratch_sz = 0;
	}
}

static char *uri_to_path(const char *u)
{
	size_t n = strlen(u), i, o = 0;
	char *r = malloc(n + 1);
	if (!r)
		return NULL;
	if (!strncmp(u, "file://", 7))
		u += 7;
	else if (!strncmp(u, "file:", 5))
		u += 5;
	else
	{
		memcpy(r, u, n + 1);
		return r;
	}
	n = strlen(u);
	for (i = 0; i < n; i++)
	{
		if (u[i] == '%' && i + 2 < n)
		{
			char h[3] = { u[i+1], u[i+2], 0 };
			char *e;
			long v = strtol(h, &e, 16);
			if (!*e)
			{
				r[o++] = (char)v;
				i += 2;
				continue;
			}
		}
		r[o++] = u[i];
	}
	r[o] = 0;
	return r;
}

pb_doc *pb_open(const char *path)
{
	pb_doc *d;
	char *p;
	fz_context *ctx;

	p = uri_to_path(path);
	if (!p)
		return NULL;
	ctx = ctx_acquire();
	if (!ctx)
	{
		free(p);
		return NULL;
	}
	d = calloc(1, sizeof(*d));
	if (!d)
	{
		free(p);
		ctx_release();
		return NULL;
	}
	d->ctx = ctx;
	d->list_page = -1;
	fz_try(ctx)
	{
		d->doc = fz_open_document(ctx, p);
		if (fz_needs_password(ctx, d->doc))
			fz_throw(ctx, FZ_ERROR_GENERIC, "password protected");
		d->page_count = fz_count_pages(ctx, d->doc);
	}
	fz_catch(ctx)
	{
		if (d->doc)
			fz_drop_document(ctx, d->doc);
		free(d);
		free(p);
		ctx_release();
		return NULL;
	}
	free(p);
	return d;
}

static void drop_list(pb_doc *d)
{
	if (d->list)
		fz_drop_display_list(d->ctx, d->list);
	d->list = NULL;
	d->list_page = -1;
}

void pb_close(pb_doc *d)
{
	if (!d)
		return;
	drop_list(d);
	fz_drop_document(d->ctx, d->doc);
	free(d);
	ctx_release();
}

int pb_page_count(pb_doc *d)
{
	return d ? d->page_count : 0;
}

static int ensure_page(pb_doc *d, int pageno)
{
	fz_context *ctx = d->ctx;
	fz_page *page = NULL;
	int ok = 1;

	if (pageno < 0 || pageno >= d->page_count)
		return -1;
	if (d->list && d->list_page == pageno)
		return 0;
	drop_list(d);
	fz_var(page);
	fz_try(ctx)
	{
		fz_display_list *l;
		page = fz_load_page(ctx, d->doc, pageno);
		d->bounds = fz_bound_page(ctx, page);
		l = fz_new_display_list_from_page(ctx, page);
		d->list = l;
		d->list_page = pageno;
	}
	fz_always(ctx)
		fz_drop_page(ctx, page);
	fz_catch(ctx)
		ok = 0;
	return ok ? 0 : -1;
}

int pb_page_size(pb_doc *d, int page, float *w, float *h)
{
	if (!d || ensure_page(d, page))
		return -1;
	*w = d->bounds.x1 - d->bounds.x0;
	*h = d->bounds.y1 - d->bounds.y0;
	return 0;
}

/* page space -> device space (scale, rotate clockwise, origin at top-left, optional vertical flip) */
static fz_matrix make_ctm(pb_doc *d, float scale, int rot, int mirrored, int *W, int *H)
{
	float pw = d->bounds.x1 - d->bounds.x0, ph = d->bounds.y1 - d->bounds.y0;
	fz_matrix m = fz_translate(-d->bounds.x0, -d->bounds.y0);
	fz_rect r;
	float fw, fh;

	m = fz_concat(m, fz_scale(scale, scale));
	m = fz_concat(m, fz_rotate((float)(rot & 3) * 90.0f));
	r = fz_transform_rect(fz_make_rect(0, 0, pw, ph), m);
	m = fz_concat(m, fz_translate(-r.x0, -r.y0));
	fw = r.x1 - r.x0;
	fh = r.y1 - r.y0;
	if (mirrored)
	{
		m = fz_concat(m, fz_scale(1, -1));
		m = fz_concat(m, fz_translate(0, fh));
	}
	*W = (int)ceilf(fw);
	*H = (int)ceilf(fh);
	if (*W < 1) *W = 1;
	if (*H < 1) *H = 1;
	return m;
}

int pb_device_size(pb_doc *d, int page, float scale, int rotation, int *w, int *h)
{
	if (!d || ensure_page(d, page))
		return -1;
	(void)make_ctm(d, scale, rotation, 0, w, h);
	return 0;
}

static void bgra_to_565(const uint8_t *src, int sp, uint8_t *dst, int dp, int w, int h)
{
	int x, y;
	for (y = 0; y < h; y++)
	{
		const uint32_t *s = (const uint32_t *)(src + (size_t)y * sp);
		uint16_t *o = (uint16_t *)(dst + (size_t)y * dp);
		for (x = 0; x < w; x++)
		{
			uint32_t p = s[x];
			o[x] = (uint16_t)(((p >> 8) & 0xF800) | ((p >> 5) & 0x07E0) | ((p >> 3) & 0x001F));
		}
	}
}

int pb_render(pb_doc *d, int page, float scale, int rotation, int mirrored, int night,
              int x, int y, int w, int h, pb_format fmt, void *dst, int pitch)
{
	fz_context *ctx;
	fz_matrix ctm;
	fz_pixmap *pix = NULL;
	fz_device *dev = NULL;
	uint8_t *buf;
	int bpitch, W, H, ok = 1;

	if (!d || !dst || w <= 0 || h <= 0 || ensure_page(d, page))
		return -1;
	ctx = d->ctx;
	ctm = make_ctm(d, scale, rotation, mirrored, &W, &H);
	if (x < 0 || y < 0 || x + w > W || y + h > H)
		return -1;
	ctm = fz_concat(ctm, fz_translate((float)-x, (float)-y));

	if (fmt == PB_FMT_XRGB8888)
	{
		if (((uintptr_t)dst & 3) || (pitch & 3) || pitch < w * 4)
			return -1;
		buf = dst;
		bpitch = pitch;
	}
	else
	{
		size_t need = (size_t)w * h * 4;
		if (pitch < w * 2)
			return -1;
		if (need > g_scratch_sz)
		{
			uint8_t *n = realloc(g_scratch, need);
			if (!n)
				return -1;
			g_scratch = n;
			g_scratch_sz = need;
		}
		buf = g_scratch;
		bpitch = w * 4;
	}

	fz_var(pix);
	fz_var(dev);
	fz_try(ctx)
	{
		/* BGR + alpha = bytes B,G,R,A = little-endian 0xAARRGGBB */
		pix = fz_new_pixmap_with_data(ctx, fz_device_bgr(ctx), w, h, NULL, 1, bpitch, buf);
		fz_clear_pixmap_with_value(ctx, pix, 0xff);	/* opaque white paper */
		dev = fz_new_draw_device(ctx, fz_identity, pix);
		fz_run_display_list(ctx, d->list, dev, ctm, fz_make_rect(0, 0, (float)w, (float)h), NULL);
		fz_close_device(ctx, dev);
		if (night)
			fz_invert_pixmap(ctx, pix);
	}
	fz_always(ctx)
	{
		fz_drop_device(ctx, dev);
		fz_drop_pixmap(ctx, pix);
	}
	fz_catch(ctx)
		ok = 0;
	if (!ok)
		return -1;
	if (fmt == PB_FMT_RGB565)
		bgra_to_565(buf, bpitch, dst, pitch, w, h);
	return 0;
}

int pb_search(pb_doc *d, int pageno, const char *needle, pb_rect *out, int max)
{
	fz_context *ctx;
	fz_page *page = NULL;
	fz_quad *q;
	fz_rect b = { 0, 0, 0, 0 };
	int n = 0, i;

	if (!d || !needle || !*needle || max <= 0 || pageno < 0 || pageno >= d->page_count)
		return 0;
	ctx = d->ctx;
	q = malloc(sizeof(*q) * (size_t)max);
	if (!q)
		return 0;
	fz_var(page);
	fz_try(ctx)
	{
		page = fz_load_page(ctx, d->doc, pageno);
		b = fz_bound_page(ctx, page);
#if PB_SEARCH_HAS_MARK
		n = fz_search_page(ctx, page, needle, NULL, q, max);
#else
		n = fz_search_page(ctx, page, needle, q, max);
#endif
	}
	fz_always(ctx)
		fz_drop_page(ctx, page);
	fz_catch(ctx)
		n = 0;
	for (i = 0; i < n; i++)
	{
		fz_rect r = fz_rect_from_quad(q[i]);
		out[i].x0 = r.x0 - b.x0;
		out[i].y0 = r.y0 - b.y0;
		out[i].x1 = r.x1 - b.x0;
		out[i].y1 = r.y1 - b.y0;
	}
	free(q);
	return n;
}

/* ---- outline ---- */

typedef struct { pb_outline_item *v; int n, cap; } vec_t;

static int vec_push(vec_t *v, const pb_outline_item *it)
{
	if (v->n == v->cap)
	{
		int nc = v->cap ? v->cap * 2 : 64;
		pb_outline_item *nv = realloc(v->v, sizeof(*nv) * (size_t)nc);
		if (!nv)
			return -1;
		v->v = nv;
		v->cap = nc;
	}
	v->v[v->n++] = *it;
	return 0;
}

static int resolve_page(pb_doc *d, const char *uri)
{
	fz_context *ctx = d->ctx;
	int pg = -1;
	float fx, fy;

	if (!uri)
		return -1;
	fz_try(ctx)
	{
		fz_location loc = fz_resolve_link(ctx, d->doc, uri, &fx, &fy);
		pg = fz_page_number_from_location(ctx, d->doc, loc);
	}
	fz_catch(ctx)
		pg = -1;
	return pg;
}

static int walk(pb_doc *d, fz_outline *o, int level, vec_t *v)
{
	for (; o; o = o->next)
	{
		pb_outline_item it;
		it.title = strdup(o->title ? o->title : "");
		it.page = resolve_page(d, o->uri);
		it.level = level;
		it.is_open = o->is_open ? 1 : 0;
		it.has_children = o->down ? 1 : 0;
		if (!it.title || vec_push(v, &it))
		{
			free(it.title);
			return -1;
		}
		if (o->down && walk(d, o->down, level + 1, v))
			return -1;
	}
	return 0;
}

int pb_outline_load(pb_doc *d, pb_outline_item **items)
{
	fz_context *ctx;
	fz_outline *o = NULL;
	vec_t v = { NULL, 0, 0 };
	int ok = 1;

	*items = NULL;
	if (!d)
		return 0;
	ctx = d->ctx;
	fz_var(o);
	fz_try(ctx)
		o = fz_load_outline(ctx, d->doc);
	fz_catch(ctx)
		ok = 0;
	if (!ok || !o)
		return 0;
	if (walk(d, o, 0, &v))
	{
		pb_outline_free(v.v, v.n);
		fz_drop_outline(ctx, o);
		return 0;
	}
	fz_drop_outline(ctx, o);
	*items = v.v;
	return v.n;
}

void pb_outline_free(pb_outline_item *items, int count)
{
	int i;
	for (i = 0; i < count; i++)
		free(items[i].title);
	free(items);
}
