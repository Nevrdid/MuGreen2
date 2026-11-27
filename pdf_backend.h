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
typedef enum {
	PB_OPEN_OK = 0,
	PB_OPEN_INVALID_ARGUMENT,
	PB_OPEN_NOT_FOUND,
	PB_OPEN_NOT_READABLE,
	PB_OPEN_NOT_REGULAR,
	PB_OPEN_INVALID_DOCUMENT,
	PB_OPEN_PASSWORD_REQUIRED,
	PB_OPEN_NO_MEMORY,
	PB_OPEN_BACKEND_ERROR,
	PB_OPEN_IO_ERROR
} pb_open_result;
const char *pb_backend_name(void);
typedef enum { PB_FMT_XRGB8888 = 0, PB_FMT_RGB565 = 1 } pb_format;
typedef struct { float x0, y0, x1, y1; } pb_rect;
typedef struct {
	char *title;
	int page, level, is_open, has_children;
} pb_outline_item;
void pb_configure(size_t store_bytes, int aa_bits);
pb_open_result pb_open(const char *path, pb_doc **out, char *message, size_t message_size);
const char *pb_error_message(pb_doc *d);
int pb_page_has_warnings(pb_doc *d, int page);
int pb_take_page_warning(pb_doc *d, int page);
void pb_close(pb_doc *d);
int pb_page_count(pb_doc *d);
int pb_page_size(pb_doc *d, int page, float *w, float *h);
int pb_device_size(pb_doc *d, int page, float scale, int *w, int *h);
int pb_render(pb_doc *d, int page, float scale, int night,
              int x, int y, int w, int h, pb_format fmt, void *dst, int pitch);
int pb_search(pb_doc *d, int page, const char *needle, pb_rect *out, int max);
int pb_outline_load(pb_doc *d, pb_outline_item **items);
void pb_outline_free(pb_outline_item *items, int count);
#endif
