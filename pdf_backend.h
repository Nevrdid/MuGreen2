/* MuGreen2 MuPDF backend interface.
 * SPDX-License-Identifier: AGPL-3.0-only
 * This interface is part of the combined MuGreen2 application; see LICENSE.
 * MuGreen2 backend/interface additions: Copyright (C) 2026 MuGreen2 contributors.
 */
/* pdf_backend.h - thin MuPDF wrapper for MuGreen2.
 * SPDX-License-Identifier: AGPL-3.0-only
 * The backend exposes page geometry, rendering and document outline operations.
 */
#ifndef PDF_BACKEND_H
#define PDF_BACKEND_H
#include <stddef.h>
#include <stdint.h>
typedef struct pb_doc pb_doc;
const char *pb_backend_name(void);
typedef enum { PB_FMT_XRGB8888 = 0, PB_FMT_RGB565 = 1 } pb_format;
typedef struct { float x0, y0, x1, y1; } pb_rect;
typedef struct {
	char *title;
	int page, level, is_open, has_children;
} pb_outline_item;
void pb_configure(size_t store_bytes, int aa_bits);
pb_doc *pb_open(const char *path);
void pb_close(pb_doc *d);
int pb_page_count(pb_doc *d);
int pb_page_size(pb_doc *d, int page, float *w, float *h);
int pb_device_size(pb_doc *d, int page, float scale, int rotation, int *w, int *h);
int pb_render(pb_doc *d, int page, float scale, int rotation, int mirrored, int night,
              int x, int y, int w, int h, pb_format fmt, void *dst, int pitch);
int pb_search(pb_doc *d, int page, const char *needle, pb_rect *out, int max);
int pb_outline_load(pb_doc *d, pb_outline_item **items);
void pb_outline_free(pb_outline_item *items, int count);
#endif
