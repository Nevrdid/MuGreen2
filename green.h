/* MuGreen2 - the PDF reader
 * Copyright (C) 2009 Florian Tobias Schandinat
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 3.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
/* This Green-derived header retains its original GPL-3.0 terms.
 * The combined MuGreen2 application is distributed under AGPL-3.0-only;
 * see LICENSE and README.md. The original copyright notice is preserved.
 * MuGreen2 modifications and adaptation: 2026; distributed as part of
 * the AGPL-3.0-only application. Original Green attribution is retained.
 */
#ifndef __GREEN_H__
#define __GREEN_H__
#include <stdbool.h>
#include "pdf_backend.h"

#define GREEN_FULLSCREEN 0x0001

typedef enum { NATURAL, WIDTH, HEIGHT, PAGE } Green_FitMethod;

/* Stable process exit codes; see README.md. */
#define GREEN_EXIT_USAGE 1
#define GREEN_EXIT_FILE_NOT_FOUND 2
#define GREEN_EXIT_FILE_UNREADABLE 3
#define GREEN_EXIT_INVALID_DOCUMENT 4
#define GREEN_EXIT_PASSWORD_REQUIRED 5
#define GREEN_EXIT_OPEN_ERROR 6
#define GREEN_EXIT_SDL_INIT 7
#define GREEN_EXIT_DISPLAY 8
#define GREEN_EXIT_RENDER 9
#define GREEN_EXIT_EVENT 10
#define GREEN_EXIT_RESIZE 11
typedef struct { unsigned char r, g, b, a; } Green_RGBA;
typedef struct {
	pb_doc *doc;
	char *uri;
	int page_count, page_cur, xoffset, yoffset;
	bool mirrored;
	int rotation;
	Green_FitMethod fit_method;
	double finescale;
	unsigned char bb;
} Green_Document;
typedef struct {
	unsigned short flags, width, height;
	Green_Document *doc;
	Green_RGBA c_background;
	Green_FitMethod fit_method;
	double step, zoomstep;
	unsigned char bb, side_by_side;
	bool night;
	bool right_to_left;
} Green_RTD;

pb_open_result Green_Open(Green_RTD *rtd, const char *filename, char *message, size_t message_size);
int Green_SDL_Main(Green_RTD *rtd);
void Green_Close(Green_RTD *rtd);
void Green_SaveAllLastPages(Green_RTD *rtd);
void Green_SaveDocumentState(Green_Document *doc);
void Green_LoadSettings(Green_RTD *rtd);
void Green_SaveSettings(Green_RTD *rtd);
double Green_Fit(Green_Document *doc, int width, int height);
void Green_ScrollRelative(Green_Document *doc, int x, int y, int w, int h, int bb_flag);
void Green_GetScrollRegion(Green_Document *doc, int w, int h, int *scroll_w, int *scroll_h);
void Green_Zoom(Green_Document *doc, int width, int height, double new_fs);

inline static int Green_IsDocValid(Green_RTD *rtd) { return rtd && rtd->doc; }
inline static void Green_PageSize(Green_Document *doc, int page, double *w, double *h)
{
	float fw = 1, fh = 1;
	if (pb_page_size(doc->doc, page, &fw, &fh)) { fw = 612; fh = 792; }
	*w = fw; *h = fh;
}
inline static void Green_GetDimension(Green_Document *doc, int page, int *w, int *h, double tscale, bool rotated)
{
	double pwidth, pheight;
	Green_PageSize(doc, page, &pwidth, &pheight);
	if (rotated) { *w = pheight * tscale; *h = pwidth * tscale; }
	else { *w = pwidth * tscale; *h = pheight * tscale; }
}
inline static void Green_ValidateOffset(Green_Document *doc, int width, int height)
{
	int doc_max_x, doc_max_y;
	if (doc->rotation % 2) Green_GetScrollRegion(doc, width, height, &doc_max_y, &doc_max_x);
	else Green_GetScrollRegion(doc, width, height, &doc_max_x, &doc_max_y);
	if (doc->xoffset < 0) doc->xoffset = 0;
	else if (doc->xoffset > doc_max_x) doc->xoffset = doc_max_x;
	if (doc->yoffset < 0) doc->yoffset = 0;
	else if (doc->yoffset > doc_max_y) doc->yoffset = doc_max_y;
}
inline static int Green_GotoPage(Green_Document *doc, int page, bool set_offset)
{
	if (page < 0 || page >= doc->page_count) return 0;
	doc->page_cur = page;
	if (set_offset) doc->xoffset = doc->yoffset = 0;
	return 1;
}
inline static void Green_RotateRight(Green_Document *doc)
{
	if (doc->mirrored) doc->rotation = (doc->rotation + 3) % 4;
	else doc->rotation = (doc->rotation + 1) % 4;
}
inline static void Green_RotateLeft(Green_Document *doc)
{
	if (doc->mirrored) doc->rotation = (doc->rotation + 1) % 4;
	else doc->rotation = (doc->rotation + 3) % 4;
}
inline static void Green_MirrorH(Green_Document *doc) { doc->mirrored = !doc->mirrored; }
inline static void Green_MirrorV(Green_Document *doc)
{
	doc->mirrored = !doc->mirrored;
	doc->rotation = (doc->rotation + 2) % 4;
}
#endif /* __GREEN_H__ */
