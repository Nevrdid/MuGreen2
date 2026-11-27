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

/* This Green-derived file retains its original GPL-3.0 terms.
 * The combined MuGreen2 application is distributed under AGPL-3.0-only;
 * see LICENSE and README.md. The original copyright notice is preserved.
 * MuGreen2 modifications and adaptation: 2026; distributed as part of
 * the AGPL-3.0-only application. Original Green attribution is retained.
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <SDL.h>
#include <SDL_gamecontroller.h>
#include "green.h"

#ifndef MUGREEN2_VERSION
#define MUGREEN2_VERSION "unknown"
#endif


#define FLAG_QUIT	0x0001
#define FLAG_RENDER	0x0002


typedef enum
{
	NORMAL, TOC, HELP
	
}	RState;

static RState	g_state = NORMAL;

typedef struct TOCNode
{
	char	*title;
	int	page;
		// 0-based page index, -1 if none
	bool	open;
	int	n_children;
	struct TOCNode	**children;
	
}	TOCNode;

typedef struct
{
	TOCNode	*node;
	int	depth;
	
}	TOCEntry;

static TOCNode	*g_toc = NULL;
static TOCEntry	*g_toc_entries = NULL;
static int	g_toc_count = 0, g_toc_sel = 0;
static bool	g_toc_active = false;

/* Tap Select for help; hold it as the hotkey for reader shortcuts. */
static bool	g_select_button_held = false;
static bool	g_select_chord_used = false;
static bool	g_start_button_held = false;
static bool	g_exit_chord = false;
/* Face-button zoom is applied on release. */
static bool	g_x_held = false;
static bool	g_y_held = false;
/* L2/R2 are optional trigger-axis bindings. */
static bool	g_l2_held = false;
static bool	g_r2_held = false;

/* Panel mounted upside down: rotate the finished frame by 180° when presenting (GREEN_ROTATE180=1) */
static int	g_rotate180 = 0;

/* Hold D-pad Up/Down to scroll TOC / help. */
static int	g_list_nav_dir = 0;
static Uint32	g_list_nav_next = 0;
#define LIST_NAV_DELAY_MS	280
#define LIST_NAV_RATE_MS	50

static bool	g_help_active = false;
static bool	g_has_presented_frame = false;
static bool	g_rendered_content_this_frame = false;
static int	g_help_scroll = 0;

static const char	*g_help_lines[] = {
	"Game controller controls (Nintendo labels)",
	"",
	"D-pad          Scroll / navigate the TOC",
	"B              Fit page to width / select TOC entry",
	"A              Rotate right / close overlay",
	"X / Y          Zoom in / zoom out",
	"L1 / R1        Previous / next page",
	"L2 / R2        Jump -10 / +10 pages (optional)",
	"Start          Toggle PDF table of contents",
	"Select         Tap: help / hold: Hotkey",
	"Hotkey+Start   Exit reader (works without Guide)",
	"Guide          Exit reader (optional)",
	"",
	"Hotkey shortcuts (hold Select first)",
	"",
	"Hotkey+B       Toggle night mode",
	"Hotkey+X       Mirror horizontally",
	"Hotkey+Y       Mirror vertically",
	"Hotkey+L1      Toggle dual-page mode",
	"Hotkey+R1      Toggle right-to-left manga reading",
	"",
	"In document TOC",
	"",
	"Up/Down        Move selection",
	"Left/Right     Collapse / expand sections",
	"B              Jump to selected section",
	"A / Start      Close TOC",
	"",
	"Optional controls",
	"",
	"L2 / R2        Page jumps (if supported)",
	"",
	"About MuGreen2",
	"Version " MUGREEN2_VERSION,
	"License: AGPL-3.0-only",
	"github.com/Nevrdid/MuGreen2",
};
#define HELP_LINE_COUNT	((int)(sizeof( g_help_lines ) / sizeof( g_help_lines[0] )))


/* Combined-spread geometry: offsets are device pixels from the spread's
 * leading top-left corner, allowing either page to move across the viewport. */
static int GetSpreadMetrics(Green_RTD *rtd, int vw, int vh, double *scale,
	int *lp, int *rp, int *lw, int *lh, int *rw, int *rh)
{
	Green_Document *doc = rtd->doc;
	if (!rtd->side_by_side || doc->page_count < 2) return 0;
	if (doc->page_cur % 2 == 0) {
		if (rtd->right_to_left) { *lp = doc->page_cur + 1; *rp = doc->page_cur; }
		else { *lp = doc->page_cur; *rp = doc->page_cur + 1; }
	} else {
		if (rtd->right_to_left) { *lp = doc->page_cur; *rp = doc->page_cur - 1; }
		else { *lp = doc->page_cur - 1; *rp = doc->page_cur; }
	}
	if (*lp < 0 || *rp < 0 || *lp >= doc->page_count || *rp >= doc->page_count) return 0;
	*scale = Green_Fit(doc, vw / 2, vh) * doc->finescale;
	if (pb_device_size(doc->doc, *lp, (float)*scale, doc->rotation & 3, lw, lh)
		|| pb_device_size(doc->doc, *rp, (float)*scale, doc->rotation & 3, rw, rh)) return 0;
	return 1;
}

static void ClampSpreadOffset(Green_RTD *rtd, int vw, int vh)
{
	int lp, rp, lw, lh, rw, rh, sw, sh;
	double scale;
	Green_Document *doc = rtd->doc;
	if (!GetSpreadMetrics(rtd, vw, vh, &scale, &lp, &rp, &lw, &lh, &rw, &rh)) return;
	sw = lw + rw; sh = lh > rh ? lh : rh;
	if (doc->xoffset < 0) doc->xoffset = 0;
	if (doc->yoffset < 0) doc->yoffset = 0;
	if (sw <= vw) doc->xoffset = 0;
	else if (doc->xoffset > sw - vw) doc->xoffset = sw - vw;
	if (sh <= vh) doc->yoffset = 0;
	else if (doc->yoffset > sh - vh) doc->yoffset = sh - vh;
}

static void RenderSpreadPage(Green_RTD *rtd, int page, int px, int py,
	int pw, int ph, double scale, int *failed_page)
{
	Green_Document *doc = rtd->doc;
	SDL_Surface *display = DrawSurface();
	SDL_Rect clip;
	int x1 = px < 0 ? 0 : px, y1 = py < 0 ? 0 : py;
	int x2 = px + pw > display->w ? display->w : px + pw;
	int y2 = py + ph > display->h ? display->h : py + ph;
	int sx, sy;
	const char *error = NULL;
	if (x1 >= x2 || y1 >= y2) return;
	clip.x = x1; clip.y = y1; clip.w = x2 - x1; clip.h = y2 - y1;
	sx = x1 - px; sy = y1 - py;
	if (doc->mirrored) sy = ph - sy - clip.h;
	if (pb_render(doc->doc, page, (float)scale, doc->rotation & 3,
		doc->mirrored, rtd->night, sx, sy, clip.w, clip.h, PB_FMT_XRGB8888,
		DstPixel(display, clip.x, clip.y), display->pitch))
		error = pb_error_message(doc->doc);
	else g_rendered_content_this_frame = true;
	if (error) {
		SDL_FillRect(display, &clip, SDL_MapRGB(display->format, 0x28, 0x18, 0x18));
		DrawString(display, clip.x + 8, clip.y + 8, "Page cannot be rendered",
			SDL_MapRGB(display->format, 0xFF, 0x80, 0x80));
		SDL_SetClipRect(display, &clip);
		DrawString(display, clip.x + 8, clip.y + 30, error, SDL_MapRGB(display->format, 0xE0, 0xE0, 0xE0));
		SDL_SetClipRect(display, NULL);
		fprintf(stderr, "MuGreen2: rendering failed on page %d: %s\n", page + 1, error);
		if (*failed_page < 0) *failed_page = page;
	} else if (pb_page_has_warnings(doc->doc, page)) {
		SDL_Rect marker = { x2 - 18, y1 + 4, 14, 14 };
		if (marker.x >= x1) {
			SDL_FillRect(display, &marker, SDL_MapRGB(display->format, 0xD0, 0x90, 0x20));
			DrawString(display, marker.x + 3, marker.y + 1, "!", SDL_MapRGB(display->format, 0x10, 0x10, 0x10));
		}
		if (pb_take_page_warning(doc->doc, page)) {
			char msg[256];
			snprintf(msg, sizeof(msg), "Page %d contains content that MuPDF could not fully interpret", page + 1);
			Green_LogMessage(msg); fprintf(stderr, "MuGreen2: %s\n", msg);
		}
	}
}

static void ZoomSpread(Green_RTD *rtd, int vw, int vh, double new_fs)
{
	Green_Document *doc = rtd->doc;
	int lp, rp, lw, lh, rw, rh, old_sw, old_sh, new_sw, new_sh;
	double scale, anchor_x, anchor_y, old_fs = doc->finescale;
	int margin_x, margin_y;
	if (!GetSpreadMetrics(rtd, vw, vh, &scale, &lp, &rp, &lw, &lh, &rw, &rh)) return;
	old_sw = lw + rw; old_sh = lh > rh ? lh : rh;
	anchor_x = doc->xoffset + vw / 2.0 - (old_sw < vw ? (vw - old_sw) / 2.0 : 0);
	anchor_y = doc->yoffset + vh / 2.0 - (old_sh < vh ? (vh - old_sh) / 2.0 : 0);
	if (new_fs < 0.1) new_fs = 0.1;
	if (new_fs > 8.0) new_fs = 8.0;
	doc->finescale = new_fs;
	if (!GetSpreadMetrics(rtd, vw, vh, &scale, &lp, &rp, &lw, &lh, &rw, &rh)) {
		doc->finescale = old_fs; return;
	}
	new_sw = lw + rw; new_sh = lh > rh ? lh : rh;
	margin_x = new_sw < vw ? (vw - new_sw) / 2 : 0;
	margin_y = new_sh < vh ? (vh - new_sh) / 2 : 0;
	doc->xoffset = (int)(anchor_x - vw / 2.0 + margin_x + 0.5);
	doc->yoffset = (int)(anchor_y - vh / 2.0 + margin_y + 0.5);
	ClampSpreadOffset(rtd, vw, vh);
}

static int	ViewWidth( Green_RTD *rtd );
static RState	OpenHelpPanel( unsigned short *flags );
static void	RenderHelp( SDL_Surface *display );
static RState	HelpInput( Uint8 button, unsigned short *flags );
static void	HelpStepScroll( int dir, int max_scroll );
static int	HelpMaxScroll( SDL_Surface *display );

/* ---- SDL2 display ----
 * All drawing goes into g_fb, an off-screen XRGB8888 surface (so the PDF backend can render straight into it,
 * whatever pixel format the window/driver uses). Present() uploads it to a streaming texture and shows it.
 * GREEN_ROTATE180=1 rotates the final image by 180 degrees (panels mounted upside down), nothing else changes. */
static SDL_Window	*g_window = NULL;
static SDL_Renderer	*g_renderer = NULL;
static SDL_Texture	*g_texture = NULL;
static SDL_Surface	*g_fb = NULL;

static SDL_Surface	*DrawSurface( void )
{
	return g_fb;
}

static void	DestroyFramebuffer( void )
{
	if (g_texture)
		SDL_DestroyTexture( g_texture );
	if (g_fb)
		SDL_FreeSurface( g_fb );
	g_texture = NULL;
	g_fb = NULL;
}

/* (Re)create the framebuffer and its texture; 0 on success */
static int	CreateFramebuffer( int w, int h )
{
	DestroyFramebuffer();
	g_fb = SDL_CreateRGBSurfaceWithFormat( 0, w, h, 32, SDL_PIXELFORMAT_XRGB8888 );
	if (!g_fb)
		return -1;
	g_texture = SDL_CreateTexture( g_renderer, SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_STREAMING, w, h );
	if (!g_texture)
	{
		DestroyFramebuffer();
		return -1;
	}
	return 0;
}

static Uint8	*DstPixel( SDL_Surface *d, int lx, int ly )
{
	return (Uint8 *)d->pixels + ly * d->pitch + lx * d->format->BytesPerPixel;
}

static void	Present( void )
{
	if (!g_fb || !g_texture)
		return;
	SDL_UpdateTexture( g_texture, NULL, g_fb->pixels, g_fb->pitch );
	SDL_RenderClear( g_renderer );
	SDL_RenderCopyEx( g_renderer, g_texture, NULL, NULL, g_rotate180 ? 180.0 : 0.0, NULL, SDL_FLIP_NONE );
	SDL_RenderPresent( g_renderer );
}

// Simple 5x7 bitmap font for basic ASCII characters (32-126)
// Each character is represented by 7 bytes, one per row
static const unsigned char bitmap_font[][7] = {
	{0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // 32: space
	{0x04,0x04,0x04,0x04,0x00,0x04,0x00}, // 33: !
	{0x0A,0x0A,0x00,0x00,0x00,0x00,0x00}, // 34: "
	{0x0A,0x1F,0x0A,0x0A,0x1F,0x0A,0x00}, // 35: #
	{0x04,0x0F,0x14,0x0E,0x05,0x1E,0x04}, // 36: $
	{0x18,0x19,0x02,0x04,0x08,0x13,0x03}, // 37: %
	{0x0C,0x12,0x14,0x08,0x15,0x12,0x0D}, // 38: &
	{0x04,0x04,0x00,0x00,0x00,0x00,0x00}, // 39: '
	{0x02,0x04,0x08,0x08,0x08,0x04,0x02}, // 40: (
	{0x08,0x04,0x02,0x02,0x02,0x04,0x08}, // 41: )
	{0x00,0x04,0x15,0x0E,0x15,0x04,0x00}, // 42: *
	{0x00,0x04,0x04,0x1F,0x04,0x04,0x00}, // 43: +
	{0x00,0x00,0x00,0x00,0x04,0x04,0x08}, // 44: ,
	{0x00,0x00,0x00,0x1F,0x00,0x00,0x00}, // 45: -
	{0x00,0x00,0x00,0x00,0x00,0x04,0x00}, // 46: .
	{0x01,0x02,0x04,0x08,0x10,0x00,0x00}, // 47: /
	{0x0E,0x11,0x13,0x15,0x19,0x11,0x0E}, // 48: 0
	{0x04,0x0C,0x04,0x04,0x04,0x04,0x0E}, // 49: 1
	{0x0E,0x11,0x01,0x06,0x08,0x10,0x1F}, // 50: 2
	{0x0E,0x11,0x01,0x06,0x01,0x11,0x0E}, // 51: 3
	{0x02,0x06,0x0A,0x12,0x1F,0x02,0x02}, // 52: 4
	{0x1F,0x10,0x1E,0x01,0x01,0x11,0x0E}, // 53: 5
	{0x06,0x08,0x10,0x1E,0x11,0x11,0x0E}, // 54: 6
	{0x1F,0x01,0x02,0x04,0x08,0x08,0x08}, // 55: 7
	{0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E}, // 56: 8
	{0x0E,0x11,0x11,0x0F,0x01,0x02,0x0C}, // 57: 9
	{0x00,0x04,0x00,0x00,0x04,0x00,0x00}, // 58: :
	{0x00,0x04,0x00,0x00,0x04,0x04,0x08}, // 59: ;
	{0x02,0x04,0x08,0x10,0x08,0x04,0x02}, // 60: <
	{0x00,0x00,0x1F,0x00,0x1F,0x00,0x00}, // 61: =
	{0x08,0x04,0x02,0x01,0x02,0x04,0x08}, // 62: >
	{0x0E,0x11,0x01,0x02,0x04,0x00,0x04}, // 63: ?
	{0x0E,0x11,0x17,0x15,0x17,0x10,0x0E}, // 64: @
	{0x0E,0x11,0x11,0x1F,0x11,0x11,0x11}, // 65: A
	{0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E}, // 66: B
	{0x0E,0x11,0x10,0x10,0x10,0x11,0x0E}, // 67: C
	{0x1E,0x11,0x11,0x11,0x11,0x11,0x1E}, // 68: D
	{0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F}, // 69: E
	{0x1F,0x10,0x10,0x1E,0x10,0x10,0x10}, // 70: F
	{0x0E,0x11,0x10,0x17,0x11,0x11,0x0F}, // 71: G
	{0x11,0x11,0x11,0x1F,0x11,0x11,0x11}, // 72: H
	{0x0E,0x04,0x04,0x04,0x04,0x04,0x0E}, // 73: I
	{0x07,0x02,0x02,0x02,0x02,0x12,0x0C}, // 74: J
	{0x11,0x12,0x14,0x18,0x14,0x12,0x11}, // 75: K
	{0x10,0x10,0x10,0x10,0x10,0x10,0x1F}, // 76: L
	{0x11,0x1B,0x15,0x11,0x11,0x11,0x11}, // 77: M
	{0x11,0x19,0x15,0x13,0x11,0x11,0x11}, // 78: N
	{0x0E,0x11,0x11,0x11,0x11,0x11,0x0E}, // 79: O
	{0x1E,0x11,0x11,0x1E,0x10,0x10,0x10}, // 80: P
	{0x0E,0x11,0x11,0x11,0x15,0x12,0x0D}, // 81: Q
	{0x1E,0x11,0x11,0x1E,0x14,0x12,0x11}, // 82: R
	{0x0E,0x11,0x10,0x0E,0x01,0x11,0x0E}, // 83: S
	{0x1F,0x04,0x04,0x04,0x04,0x04,0x04}, // 84: T
	{0x11,0x11,0x11,0x11,0x11,0x11,0x0E}, // 85: U
	{0x11,0x11,0x11,0x11,0x11,0x0A,0x04}, // 86: V
	{0x11,0x11,0x11,0x15,0x15,0x1B,0x11}, // 87: W
	{0x11,0x11,0x0A,0x04,0x0A,0x11,0x11}, // 88: X
	{0x11,0x11,0x0A,0x04,0x04,0x04,0x04}, // 89: Y
	{0x1F,0x01,0x02,0x04,0x08,0x10,0x1F}, // 90: Z
	{0x0E,0x08,0x08,0x08,0x08,0x08,0x0E}, // 91: [
	{0x10,0x08,0x04,0x02,0x01,0x00,0x00}, // 92: backslash
	{0x0E,0x02,0x02,0x02,0x02,0x02,0x0E}, // 93: ]
	{0x04,0x0A,0x11,0x00,0x00,0x00,0x00}, // 94: ^
	{0x00,0x00,0x00,0x00,0x00,0x00,0x1F}, // 95: _
	{0x08,0x04,0x00,0x00,0x00,0x00,0x00}, // 96: `
	{0x00,0x00,0x0E,0x01,0x0F,0x11,0x0F}, // 97: a
	{0x10,0x10,0x16,0x19,0x11,0x11,0x1E}, // 98: b
	{0x00,0x00,0x0E,0x10,0x10,0x11,0x0E}, // 99: c
	{0x01,0x01,0x0D,0x13,0x11,0x11,0x0F}, // 100: d
	{0x00,0x00,0x0E,0x11,0x1F,0x10,0x0E}, // 101: e
	{0x06,0x09,0x08,0x1C,0x08,0x08,0x08}, // 102: f
	{0x00,0x0F,0x11,0x11,0x0F,0x01,0x0E}, // 103: g
	{0x10,0x10,0x16,0x19,0x11,0x11,0x11}, // 104: h
	{0x04,0x00,0x0C,0x04,0x04,0x04,0x0E}, // 105: i
	{0x02,0x00,0x06,0x02,0x02,0x12,0x0C}, // 106: j
	{0x10,0x10,0x12,0x14,0x18,0x14,0x12}, // 107: k
	{0x0C,0x04,0x04,0x04,0x04,0x04,0x0E}, // 108: l
	{0x00,0x00,0x1A,0x15,0x15,0x11,0x11}, // 109: m
	{0x00,0x00,0x16,0x19,0x11,0x11,0x11}, // 110: n
	{0x00,0x00,0x0E,0x11,0x11,0x11,0x0E}, // 111: o
	{0x00,0x00,0x1E,0x11,0x1E,0x10,0x10}, // 112: p
	{0x00,0x00,0x0D,0x13,0x0F,0x01,0x01}, // 113: q
	{0x00,0x00,0x16,0x19,0x10,0x10,0x10}, // 114: r
	{0x00,0x00,0x0E,0x10,0x0E,0x01,0x1E}, // 115: s
	{0x08,0x08,0x1C,0x08,0x08,0x09,0x06}, // 116: t
	{0x00,0x00,0x11,0x11,0x11,0x13,0x0D}, // 117: u
	{0x00,0x00,0x11,0x11,0x11,0x0A,0x04}, // 118: v
	{0x00,0x00,0x11,0x11,0x15,0x15,0x0A}, // 119: w
	{0x00,0x00,0x11,0x0A,0x04,0x0A,0x11}, // 120: x
	{0x00,0x00,0x11,0x11,0x0F,0x01,0x0E}, // 121: y
	{0x00,0x00,0x1F,0x02,0x04,0x08,0x1F}, // 122: z
	{0x06,0x08,0x08,0x10,0x08,0x08,0x06}, // 123: {
	{0x04,0x04,0x04,0x00,0x04,0x04,0x04}, // 124: |
	{0x0C,0x02,0x02,0x01,0x02,0x02,0x0C}, // 125: }
	{0x00,0x08,0x15,0x02,0x00,0x00,0x00}  // 126: ~
};


/* Draw the window [xoff,xoff+dest.w) x [yoff,yoff+dest.h) of page page_num into the display at dest.
 * green keeps its scroll offsets in UNROTATED page space, the backend works in device (rotated) space,
 * so the window origin is converted here. */
void DrawString( SDL_Surface *display, int x, int y, const char *str, Uint32 color );
static int	RenderPage( Green_RTD *rtd, SDL_Rect dest, int xoff, int yoff,
	int page_num, double tscale, int *failed_page )
{
	Green_Document *doc = rtd->doc;
	SDL_Surface *display = DrawSurface();
	int W, H, xd, yd, w, h, rot = doc->rotation & 3;
	const char *error = NULL;
	SDL_Rect clip = dest;
	if (dest.w <= 0 || dest.h <= 0) return 0;
	if (pb_device_size(doc->doc, page_num, (float)tscale, rot, &W, &H))
		error = pb_error_message(doc->doc);
	else {
		switch (rot) {
		case 1: xd = W - yoff - dest.w; yd = xoff; break;
		case 2: xd = W - xoff - dest.w; yd = H - yoff - dest.h; break;
		case 3: xd = yoff; yd = H - xoff - dest.h; break;
		default: xd = xoff; yd = yoff; break;
		}
		if (doc->mirrored) yd = H - yd - dest.h;
		w = dest.w; h = dest.h;
		if (xd < 0) xd = 0;
		if (yd < 0) yd = 0;
		if (xd + w > W) w = W - xd;
		if (yd + h > H) h = H - yd;
		if (w > 0 && h > 0 && pb_render(doc->doc, page_num, (float)tscale, rot,
			doc->mirrored, rtd->night, xd, yd, w, h, PB_FMT_XRGB8888,
			DstPixel(display, dest.x, dest.y), display->pitch))
			error = pb_error_message(doc->doc);
		else if (w > 0 && h > 0)
			g_rendered_content_this_frame = true;
		if (!error && pb_page_has_warnings(doc->doc, page_num))
		{
			SDL_Rect marker = { dest.x + dest.w - 18, dest.y + 4, 14, 14 };
			SDL_FillRect(display, &marker, SDL_MapRGB(display->format, 0xD0, 0x90, 0x20));
			DrawString(display, marker.x + 3, marker.y + 1, "!", SDL_MapRGB(display->format, 0x10, 0x10, 0x10));
			if (pb_take_page_warning(doc->doc, page_num))
				{
				char log_message[256];
				snprintf(log_message, sizeof(log_message), "Page %d contains content that MuPDF could not fully interpret", page_num + 1);
				Green_LogMessage(log_message);
				fprintf(stderr, "MuGreen2: %s\n", log_message);
			}
		}
	}
	if (error) {
		SDL_FillRect(display, &dest, SDL_MapRGB(display->format, 0x28, 0x18, 0x18));
		DrawString(display, dest.x + 8, dest.y + 8, "Page cannot be rendered",
			SDL_MapRGB(display->format, 0xFF, 0x80, 0x80));
		SDL_SetClipRect(display, &clip);
		DrawString(display, dest.x + 8, dest.y + 30, error,
			SDL_MapRGB(display->format, 0xE0, 0xE0, 0xE0));
		SDL_SetClipRect(display, NULL);
		fprintf(stderr, "MuGreen2: rendering failed on page %d: %s\n", page_num + 1, error);
		if (*failed_page < 0) *failed_page = page_num;
	}
	return 0;
}

// Draw a single character using bitmap font
void DrawChar( SDL_Surface *display, int x, int y, char c, Uint32 color )
{
	SDL_Rect pixel;
	int row, col;
	unsigned char bits;
	
	// Only handle printable ASCII characters
	if (c < 32 || c > 126)
		return;
	
	// Get the bitmap for this character
	const unsigned char *char_bitmap = bitmap_font[c - 32];
	
	// Draw the character pixel by pixel
	pixel.w = 2;  // Make pixels 2x2 for better visibility
	pixel.h = 2;
	
	for (row = 0; row < 7; row++)
	{
		bits = char_bitmap[row];
		for (col = 0; col < 5; col++)
		{
			if (bits & (1 << (4 - col)))
			{
				pixel.x = x + col * 2;
				pixel.y = y + row * 2;
				SDL_FillRect( display, &pixel, color );
			}
		}
	}
}

// Draw a string using bitmap font
void DrawString( SDL_Surface *display, int x, int y, const char *str, Uint32 color )
{
	int i;
	int char_width = 12; // 5 pixels * 2 + 2 pixel spacing
	
	for (i = 0; str[i] != '\0'; i++)
	{
		DrawChar( display, x + i * char_width, y, str[i], color );
	}
}

/* UTF-8 text with the built-in 5x7 bitmap font: ASCII only, other characters show as '?', long text gets "...".
 * (The previous Pango/cairo path handled Arabic; shaped non-Latin text needs a TTF renderer, e.g. SDL2_ttf.) */
static int	DrawUtf8( SDL_Surface *display, int x, int y, int max_w, int line_h,
	const char *utf8, Uint8 r, Uint8 g, Uint8 b )
{
	const unsigned char	*p = (const unsigned char *)utf8;
	char	buf[256];
	int	char_width = 12, max_chars, n = 0;
	
	if (!utf8 || !*utf8 || max_w < char_width || line_h < 8)
		return 0;
	
	max_chars = max_w / char_width;
	if (max_chars > (int)sizeof( buf ) - 1)
		max_chars = (int)sizeof( buf ) - 1;
	
	while (*p && n < max_chars)
	{
		if (*p < 0x80)
		{
			buf[n++] = (*p >= 32 && *p < 127) ? (char)*p : ' ';
			p++;
		}
		else
		{
			buf[n++] = '?';
			p++;
			while ((*p & 0xC0) == 0x80)
				p++;
		}
	}
	if (*p && n >= 3)
		buf[n-1] = buf[n-2] = buf[n-3] = '.';
	buf[n] = 0;
	
	DrawString( display, x, y, buf, SDL_MapRGB( display->format, r, g, b ) );
	return line_h;
}

static int	ViewWidth( Green_RTD *rtd )
{
	(void)rtd;
	/* Facing-page mode scrolls and zooms one continuous spread across the
	 * complete display, not two independently clipped half-width viewports. */
	return DrawSurface()->w;
}

static void	FreeTOC( TOCNode *node )
{
	int	i;
	
	if (!node)
		return;
	
	for (i = 0; i < node->n_children; i++)
		FreeTOC( node->children[i] );
	free( node->children );
	free( node->title );
	free( node );
	return;
}

static void	RebuildTOCFlat( void );

static void	BuildTOC( Green_RTD *rtd )
{
	pb_outline_item	*items = NULL;
	TOCNode		**last = NULL;
	int		n, i, maxlevel = 0;
	
	FreeTOC( g_toc );
	g_toc = NULL;
	if (!Green_IsDocValid( rtd))
		return;
	
	n = pb_outline_load( rtd->doc->doc, &items );
	g_toc = calloc( 1, sizeof( *g_toc ) );
	if (!g_toc)
	{
		pb_outline_free( items, n );
		return;
	}
	g_toc->page = -1;
	
	for (i = 0; i < n; i++)
		if (items[i].level > maxlevel)
			maxlevel = items[i].level;
	last = calloc( (size_t)maxlevel + 1, sizeof( *last ) );
	if (!last)
	{
		pb_outline_free( items, n );
		return;
	}
	
	/* the backend gives a flat list in document order with a depth: rebuild the tree */
	for (i = 0; i < n; i++)
	{
		TOCNode	*node = calloc( 1, sizeof( *node ) ), *parent, **arr;
		int	level = items[i].level;
		
		if (!node)
			break;
		node->title = strdup( items[i].title ? items[i].title : "" );
		node->page = items[i].page;
		node->open = items[i].is_open ? true : false;
		
		parent = (level > 0 && last[level - 1]) ? last[level - 1] : g_toc;
		arr = realloc( parent->children, (parent->n_children + 1) * sizeof( *arr ) );
		if (!arr || !node->title)
		{
			free( node->title );
			free( node );
			break;
		}
		parent->children = arr;
		arr[parent->n_children++] = node;
		last[level] = node;
	}
	
	free( last );
	pb_outline_free( items, n );
	g_toc_sel = 0;
	RebuildTOCFlat();
	return;
}

static void	EnsureTOC( Green_RTD *rtd )
{
	if (!Green_IsDocValid( rtd))
		return;
	
	if (!g_toc)
		BuildTOC( rtd );
	return;
}

static void	CollectVisible( TOCNode *node, int depth, TOCEntry **arr, int *count, int *cap )
{
	int	i;
	
	for (i = 0; i < node->n_children; i++)
	{
		TOCNode	*c = node->children[i];
		
		if (*count >= *cap)
		{
			*cap = *cap ? *cap * 2 : 32;
			*arr = realloc( *arr, *cap * sizeof( TOCEntry ) );
			if (!*arr)
				return;
		}
		
		(*arr)[*count].node = c;
		(*arr)[*count].depth = depth;
		(*count)++;
		if (c->open)
			CollectVisible( c, depth + 1, arr, count, cap );
	}
	
	return;
}

static void	RebuildTOCFlat( void )
{
	TOCEntry	*arr = NULL;
	int		count = 0, cap = 0;
	
	free( g_toc_entries );
	g_toc_entries = NULL;
	g_toc_count = 0;
	if (g_toc)
		CollectVisible( g_toc, 0, &arr, &count, &cap );
	g_toc_entries = arr;
	g_toc_count = count;
	if (g_toc_sel >= g_toc_count)
		g_toc_sel = g_toc_count ? g_toc_count - 1 : 0;
	return;
}

static void	RenderTOC( SDL_Surface *display, Green_RTD *rtd )
{
	SDL_Rect	panel, hl;
	Uint32		title_col, entry_col, sel_col, sel_bg;
	int		panel_w, x0, y0, line_h, max_lines, start, i, indent, text_w;
	Uint8		er, eg, eb;
	
	(void)rtd;
	if (!g_toc_active)
		return;
	
	panel_w = (display->w * 3) / 4;
	if (panel_w < 200)
		panel_w = display->w < 200 ? display->w : 200;
	
	panel.x = 0;
	panel.y = 0;
	panel.w = panel_w;
	panel.h = display->h;
	SDL_FillRect( display, &panel, SDL_MapRGB( display->format, 0x14, 0x14, 0x14 ) );
	
	title_col = SDL_MapRGB( display->format, 0xE8, 0xE8, 0xE8 );
	entry_col = SDL_MapRGB( display->format, 0xB8, 0xB8, 0xB8 );
	sel_col = SDL_MapRGB( display->format, 0xFF, 0xFF, 0x00 );
	sel_bg = SDL_MapRGB( display->format, 0x2E, 0x2E, 0x5E );
	
	x0 = 8;
	y0 = 8;
	DrawString( display, x0, y0, "Table of Contents", title_col );
	y0 += 22;
	line_h = 18;
	max_lines = (display->h - y0) / line_h;
	if (max_lines <= 0)
		return;
	
	if (g_toc_count <= 0)
	{
		DrawString( display, x0, y0, "No table of contents", entry_col );
		return;
	}
	
	start = 0;
	if (g_toc_count > max_lines)
	{
		start = g_toc_sel - max_lines + 1;
		if (start < 0)
			start = 0;
		if (start + max_lines > g_toc_count)
			start = g_toc_count - max_lines;
	}
	
	for (i = start; i < g_toc_count && i < start + max_lines; i++)
	{
		TOCNode	*n = g_toc_entries[i].node;
		int	y = y0 + (i - start) * line_h;
		
		if (i == g_toc_sel)
		{
			hl.x = 0;
			hl.y = y;
			hl.w = panel_w;
			hl.h = line_h;
			SDL_FillRect( display, &hl, sel_bg );
			er = 0xFF;
			eg = 0xFF;
			eb = 0x00;
		}
		else
		{
			er = 0xB8;
			eg = 0xB8;
			eb = 0xB8;
		}
		
		indent = x0 + g_toc_entries[i].depth * 14;
		if (n->n_children > 0)
		{
			if (n->open)
				DrawString( display, indent, y, "-", i == g_toc_sel ? sel_col : entry_col );
			else
				DrawString( display, indent, y, "+", i == g_toc_sel ? sel_col : entry_col );
		}
		
		text_w = panel_w - indent - 28;
		if (text_w < 8)
			text_w = 8;
		DrawUtf8( display, indent + 26, y, text_w, line_h, n->title ? n->title : "", er, eg, eb );
	}
	
	return;
}

static int	FindParentIndex( TOCNode *target )
{
	int	i, j;
	
	for (i = 0; i < g_toc_count; i++)
		for (j = 0; j < g_toc_entries[i].node->n_children; j++)
			if (g_toc_entries[i].node->children[j] == target)
				return i;
	
	return -1;
}

static int	TOCGoToPage( Green_RTD *rtd, TOCNode *node, unsigned short *flags )
{
	Green_Document	*doc;
	
	if (!node || node->page < 0 || !Green_IsDocValid( rtd))
		return 0;
	
	doc = rtd->doc;
	if (Green_GotoPage( doc, node->page, true ))
	{
		*flags |= FLAG_RENDER;
		return 1;
	}
	return 0;
}

static void	ListNavStop( void )
{
	g_list_nav_dir = 0;
	g_list_nav_next = 0;
}

static void	ListNavStart( int dir )
{
	g_list_nav_dir = dir;
	g_list_nav_next = SDL_GetTicks() + LIST_NAV_DELAY_MS;
}

static void	TOCStepSel( int dir )
{
	if (g_toc_count <= 0)
		return;
	if (dir > 0 && g_toc_sel < g_toc_count - 1)
		g_toc_sel++;
	else if (dir < 0 && g_toc_sel > 0)
		g_toc_sel--;
}


static RState	TOCInput( Green_RTD *rtd, Uint8 button, unsigned short *flags )
{
	switch (button)
	{
		case SDL_CONTROLLER_BUTTON_B:
		case SDL_CONTROLLER_BUTTON_START:
			ListNavStop(); g_toc_active = false; *flags |= FLAG_RENDER; return NORMAL;
		case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
			TOCStepSel( 1 ); ListNavStart( 1 ); *flags |= FLAG_RENDER; break;
		case SDL_CONTROLLER_BUTTON_DPAD_UP:
			TOCStepSel( -1 ); ListNavStart( -1 ); *flags |= FLAG_RENDER; break;
		case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:
			if (g_toc_count > 0) { TOCNode *n = g_toc_entries[g_toc_sel].node; if (n->n_children > 0 && !n->open) { n->open = true; RebuildTOCFlat(); } }
			*flags |= FLAG_RENDER; break;
		case SDL_CONTROLLER_BUTTON_DPAD_LEFT:
			if (g_toc_count > 0) { TOCNode *n = g_toc_entries[g_toc_sel].node; if (n->n_children > 0 && n->open) { n->open = false; RebuildTOCFlat(); } else { int idx = FindParentIndex( n ); if (idx >= 0) g_toc_sel = idx; } }
			*flags |= FLAG_RENDER; break;
		case SDL_CONTROLLER_BUTTON_A:
			if (g_toc_count > 0 && TOCGoToPage( rtd, g_toc_entries[g_toc_sel].node, flags )) { ListNavStop(); g_toc_active = false; return NORMAL; }
			break;
		default: break;
	}
	return TOC;
}

static int	Render( Green_RTD *rtd, int *failed_page )
{
	Green_Document *doc;
	SDL_Surface *display = DrawSurface();
	SDL_Rect rect;
	double tscale;
	int w, h;
	*failed_page = -1;
	g_rendered_content_this_frame = false;
	rect.x = rect.y = 0; rect.w = display->w; rect.h = display->h;
	if (rtd->night) SDL_FillRect(display, &rect, SDL_MapRGB(display->format, 0x10, 0x10, 0x10));
	else SDL_FillRect(display, &rect, SDL_MapRGB(display->format, rtd->c_background.r, rtd->c_background.g, rtd->c_background.b));
	if (!Green_IsDocValid(rtd)) { Present(); return 0; }
	doc = rtd->doc;
	if (rtd->side_by_side && doc->page_count > 1) {
		int lp, rp, lw, lh, rw, rh;
		if (GetSpreadMetrics(rtd, display->w, display->h, &tscale, &lp, &rp, &lw, &lh, &rw, &rh)) {
			int sw = lw + rw, sh = lh > rh ? lh : rh;
			int ox, oy;
			ClampSpreadOffset(rtd, display->w, display->h);
			ox = (sw < display->w ? (display->w - sw) / 2 : 0) - doc->xoffset;
			oy = (sh < display->h ? (display->h - sh) / 2 : 0) - doc->yoffset;
			RenderSpreadPage(rtd, lp, ox, oy + (sh - lh) / 2, lw, lh, tscale, failed_page);
			RenderSpreadPage(rtd, rp, ox + lw, oy + (sh - rh) / 2, rw, rh, tscale, failed_page);
		} else {
			tscale = Green_Fit(doc, display->w, display->h) * doc->finescale;
			Green_GetDimension(doc, doc->page_cur, &w, &h, tscale, doc->rotation % 2);
			rect.w = w > display->w ? display->w : w;
			rect.h = h > display->h ? display->h : h;
			rect.x = (display->w - rect.w) / 2;
			rect.y = (display->h - rect.h) / 2;
			RenderPage(rtd, rect, doc->xoffset, doc->yoffset, doc->page_cur, tscale, failed_page);
		}
	} else {
		tscale = Green_Fit(doc, display->w, display->h) * doc->finescale;
		Green_GetDimension(doc, doc->page_cur, &w, &h, tscale, doc->rotation % 2);
		rect.w = w > display->w ? display->w : w;
		rect.h = h > display->h ? display->h : h;
		rect.x = (display->w - rect.w) / 2;
		rect.y = (display->h - rect.h) / 2;
		RenderPage(rtd, rect, doc->xoffset, doc->yoffset, doc->page_cur, tscale, failed_page);
	}
	RenderTOC(display, rtd); RenderHelp(display); Present();
	return *failed_page >= 0 ? -1 : 0;
}

static void	CloseCurrentFile( Green_RTD *rtd, unsigned short *flags )
{
	if (Green_IsDocValid( rtd))
		Green_Close( rtd );

	/* There is exactly one document: closing it exits the reader. */
	*flags |= FLAG_QUIT;
	*flags &= ~FLAG_RENDER;
}

static void	SetPageStart( Green_Document *doc, bool right_to_left, int w, int h );

/* Page turn keeps finescale + pan in normal mode; manga mode starts at top-right. */
static int	GotoPageKeepView( Green_Document *doc, int page, int w, int h, bool right_to_left )
{
	if (!Green_GotoPage( doc, page, false ))
		return 0;
	if (right_to_left)
		SetPageStart( doc, true, w, h );
	else
		Green_ValidateOffset( doc, w, h );
	return 1;
}

/* Position a newly selected page at the leading corner. Offsets are stored in
 * unrotated document space, so this remains the original top-right corner
 * regardless of the current display rotation. */
static void	SetPageStart( Green_Document *doc, bool right_to_left, int w, int h )
{
	int max_x, max_y;
	Green_GetScrollRegion( doc, w, h, &max_x, &max_y );
	if (doc->rotation & 1)
		max_x = max_y;
	doc->xoffset = right_to_left ? max_x : 0;
	doc->yoffset = 0;
	Green_ValidateOffset( doc, w, h );
}

/* Scroll in screen directions and change pages at the corresponding edges. */
static int	ScrollDocument( Green_RTD *rtd, int sx, int sy, int w, int h )
{
	Green_Document *doc = rtd->doc;
	int max_x, max_y, doc_max_x, doc_max_y, doc_dy, page_step, next_page = 0;
	int rotation = doc->rotation & 3;

	if (rtd->side_by_side && doc->page_count > 1)
	{
		int lp, rp, lw, lh, rw, rh, old_x = doc->xoffset, old_y = doc->yoffset;
		double scale;
		if (GetSpreadMetrics(rtd, DrawSurface()->w, DrawSurface()->h, &scale, &lp, &rp, &lw, &lh, &rw, &rh))
		{
			doc->xoffset += sx; doc->yoffset += sy;
			ClampSpreadOffset(rtd, DrawSurface()->w, DrawSurface()->h);
			if (doc->xoffset != old_x || doc->yoffset != old_y) return 1;
			if (sx) {
				next_page = sx < 0 ? ((rotation == 1 || rotation == 2) ? 1 : -1)
					: ((rotation == 1 || rotation == 2) ? -1 : 1);
				if (rtd->right_to_left) next_page = -next_page;
				if (!Green_GotoPage(doc, doc->page_cur + next_page * 2, false)) return 0;
				doc->xoffset = doc->yoffset = 0;
				return 1;
			}
			return 0;
		}
	}

	Green_GetScrollRegion( doc, w, h, &max_x, &max_y );
	doc_max_x = (rotation & 1) ? max_y : max_x;
	doc_max_y = (rotation & 1) ? max_x : max_y;

	if (sx)
	{
		int old_x = doc->xoffset, old_y = doc->yoffset;

		/* Let Green apply the rotated/mirrored scroll transform and clamp the
		 * offsets. A horizontal edge is reached when that scroll cannot move. */
		Green_ScrollRelative( doc, sx, 0, w, h, 0 );
		if (doc->xoffset != old_x || doc->yoffset != old_y)
			return 1;

		/* Convert the screen edge into document order, then apply reading direction. */
		next_page = sx < 0
			? ((rotation == 1 || rotation == 2) ? 1 : -1)
			: ((rotation == 1 || rotation == 2) ? -1 : 1);
		if (rtd->right_to_left)
			next_page = -next_page;
	}
	else if (rotation & 1)
	{
		int old_x = doc->xoffset, old_y = doc->yoffset;
		int top_border, bottom_border;

		/* At 90/270 degrees, Up/Down moves the document's X offset. */
		Green_ScrollRelative( doc, 0, sy, w, h, 0 );
		if (doc->xoffset != old_x || doc->yoffset != old_y)
			return 1;

		if (rotation == 1)
		{
			top_border = doc->mirrored
				? doc->xoffset == doc_max_x : doc->xoffset == 0;
			bottom_border = doc->mirrored
				? doc->xoffset == 0 : doc->xoffset == doc_max_x;
		}
		else
		{
			top_border = doc->mirrored
				? doc->xoffset == 0 : doc->xoffset == doc_max_x;
			bottom_border = doc->mirrored
				? doc->xoffset == doc_max_x : doc->xoffset == 0;
		}

		/* Match the document reading direction at the rotated top/bottom
		 * edges. Rotation 270 reverses the page order relative to 90. */
		if (sy < 0 && top_border)
			next_page = rotation == 1 ? -1 : 1;
		else if (sy > 0 && bottom_border)
			next_page = rotation == 1 ? 1 : -1;
	}
	else
	{
		switch (rotation)
		{
			case 2: doc_dy = doc->mirrored ? sy : -sy; break;
			default: doc_dy = doc->mirrored ? -sy : sy; break;
		}

		/* Vertical movement follows the document's reading-order axis. */
		if (doc_dy > 0 && doc->yoffset >= doc_max_y)
			next_page = 1;
		else if (doc_dy < 0 && doc->yoffset <= 0)
			next_page = -1;
		else
		{
			Green_ScrollRelative( doc, 0, sy, w, h, 0 );
			return 1;
		}
	}

	if (next_page)
	{
		page_step = rtd->side_by_side && doc->page_count > 1 ? 2 : 1;
		if (!Green_GotoPage( doc, doc->page_cur + next_page * page_step, false ))
			return 0;

		/* Start at the leading corner: top-left normally, top-right for manga. */
		SetPageStart( doc, rtd->right_to_left, w, h );
		return 1;
	}

	return 1;
}

static void	SkipPages( Green_Document *doc, Green_RTD *rtd, int delta, int w, int h, unsigned short *flags )
{
	int	page;
	
	if (!doc)
		return;
	
	page = doc->page_cur + (rtd->right_to_left ? -delta : delta);
	if (page < 0)
		page = 0;
	else if (page >= doc->page_count)
		page = doc->page_count - 1;
	
	if (page == doc->page_cur)
		return;
	
	if (GotoPageKeepView( doc, page, w, h, rtd->right_to_left ))
		*flags |= FLAG_RENDER;
	return;
}

static void	HelpStepScroll( int dir, int max_scroll )
{
	if (max_scroll < 0)
		max_scroll = 0;
	if (dir > 0 && g_help_scroll < max_scroll)
		g_help_scroll++;
	else if (dir < 0 && g_help_scroll > 0)
		g_help_scroll--;
}

static int	HelpMaxScroll( SDL_Surface *display )
{
	int	line_h = 16, y0 = 48, visible;
	
	visible = (display->h - y0 - 8) / line_h;
	if (visible < 1)
		visible = 1;
	if (HELP_LINE_COUNT <= visible)
		return 0;
	return HELP_LINE_COUNT - visible;
}

static RState	OpenHelpPanel( unsigned short *flags )
{
	if (g_help_active)
	{
		ListNavStop();
		g_help_active = false;
		*flags |= FLAG_RENDER;
		return NORMAL;
	}
	
	g_toc_active = false;
	ListNavStop();
	g_help_active = true;
	g_help_scroll = 0;
	*flags |= FLAG_RENDER;
	return HELP;
}

static void	RenderHelp( SDL_Surface *display )
{
	SDL_Rect	panel;
	int		x0, y0, line_h, visible, i, max_scroll;
	Uint32		title_col, text_col, dim_col;
	
	if (!g_help_active)
		return;
	
	panel.x = 0;
	panel.y = 0;
	panel.w = display->w;
	panel.h = display->h;
	SDL_FillRect( display, &panel, SDL_MapRGB( display->format, 0x10, 0x10, 0x18 ) );
	
	title_col = SDL_MapRGB( display->format, 0xFF, 0xFF, 0xCC );
	text_col = SDL_MapRGB( display->format, 0xD0, 0xD0, 0xD0 );
	dim_col = SDL_MapRGB( display->format, 0x80, 0x80, 0x90 );
	
	x0 = 10;
	y0 = 8;
	DrawString( display, x0, y0, "Controls  (A / Select: close)", title_col );
	y0 = 28;
	DrawString( display, x0, y0, "D-pad scroll", dim_col );
	y0 = 48;
	line_h = 16;
	visible = (display->h - y0 - 8) / line_h;
	if (visible < 1)
		return;
	
	max_scroll = HelpMaxScroll( display );
	if (g_help_scroll > max_scroll)
		g_help_scroll = max_scroll;
	
	for (i = 0; i < visible && g_help_scroll + i < HELP_LINE_COUNT; i++)
	{
		const char	*line = g_help_lines[g_help_scroll + i];
		int		y = y0 + i * line_h;
		
		if (!line[0])
			continue;
		if (line[0] != ' ' && strchr( line, ' ' ) == NULL)
			DrawString( display, x0, y, line, title_col );
		else if (strncmp( line, "In ", 3 ) == 0
			|| !strncmp( line, "Game controller controls", 24 )
			|| !strncmp( line, "Hotkey shortcuts", 16 )
			|| !strcmp( line, "Optional / unbound (NULL)" )
			|| !strcmp( line, "About MuGreen2" ))
			DrawString( display, x0, y, line, title_col );
		else
			DrawString( display, x0, y, line, text_col );
	}
	
	if (max_scroll > 0 && g_help_scroll < max_scroll)
		DrawString( display, display->w - 50, display->h - 18, "more", dim_col );
}

static RState	HelpInput( Uint8 button, unsigned short *flags )
{
	SDL_Surface *display = DrawSurface();
	int max_scroll = HelpMaxScroll( display );
	switch (button)
	{
		case SDL_CONTROLLER_BUTTON_B:
			ListNavStop(); g_help_active = false; *flags |= FLAG_RENDER; return NORMAL;
		case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
			HelpStepScroll( 1, max_scroll ); ListNavStart( 1 ); *flags |= FLAG_RENDER; break;
		case SDL_CONTROLLER_BUTTON_DPAD_UP:
			HelpStepScroll( -1, max_scroll ); ListNavStart( -1 ); *flags |= FLAG_RENDER; break;
		default: break;
	}
	return HELP;
}

static RState	OpenTOCPanel( Green_RTD *rtd, unsigned short *flags )
{
	/* Start / o: toggle TOC */
	if (g_toc_active)
	{
		g_toc_active = false;
		*flags |= FLAG_RENDER;
		return NORMAL;
	}
	
	if (!Green_IsDocValid( rtd))
		return NORMAL;
	
	g_help_active = false;
	EnsureTOC( rtd );
	g_toc_active = true;
	*flags |= FLAG_RENDER;
	return TOC;
}

static RState	NormalInput( Green_RTD *rtd, Uint8 button, unsigned short *flags )
{
	Green_Document *doc = Green_IsDocValid( rtd ) ? rtd->doc : NULL;
	SDL_Surface *display = DrawSurface();
	int vw = ViewWidth( rtd ), hstep = (int)(vw * rtd->step);
	switch (button)
	{
		case SDL_CONTROLLER_BUTTON_DPAD_UP:
			if (!doc) break;
			if (ScrollDocument( rtd, 0, -(int)(display->h * rtd->step), vw, display->h ))
				*flags |= FLAG_RENDER;
			break;
		case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
			if (!doc) break;
			if (ScrollDocument( rtd, 0, (int)(display->h * rtd->step), vw, display->h ))
				*flags |= FLAG_RENDER;
			break;
		case SDL_CONTROLLER_BUTTON_DPAD_LEFT:
			if (!doc) break;
			if (ScrollDocument( rtd, -hstep, 0, vw, display->h ))
				*flags |= FLAG_RENDER;
			break;
		case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:
			if (!doc) break;
			if (ScrollDocument( rtd, hstep, 0, vw, display->h ))
				*flags |= FLAG_RENDER;
			break;
		case SDL_CONTROLLER_BUTTON_A:
			if (!doc)
				break;
			doc->fit_method = WIDTH;
			doc->finescale = 1;
			doc->xoffset = doc->yoffset = 0;
			*flags |= FLAG_RENDER;
			break;
		case SDL_CONTROLLER_BUTTON_B:
			if (!doc)
				break;
			Green_RotateRight( doc );
			if (rtd->side_by_side && doc->page_count > 1) ClampSpreadOffset(rtd, display->w, display->h);
			else Green_ValidateOffset(doc, display->w, display->h);
			*flags |= FLAG_RENDER;
			break;
		case SDL_CONTROLLER_BUTTON_LEFTSHOULDER:
			if (!doc) break;
			if (rtd->side_by_side && doc->page_count > 1) {
				if (!Green_GotoPage(doc, doc->page_cur + (rtd->right_to_left ? 2 : -2), false)) break;
				ClampSpreadOffset(rtd, display->w, display->h);
			} else if (!GotoPageKeepView(doc, doc->page_cur + (rtd->right_to_left ? 1 : -1), vw, display->h, rtd->right_to_left)) break;
			*flags |= FLAG_RENDER; break;
		case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER:
			if (!doc) break;
			if (rtd->side_by_side && doc->page_count > 1) {
				if (!Green_GotoPage(doc, doc->page_cur + (rtd->right_to_left ? -2 : 2), false)) break;
				ClampSpreadOffset(rtd, display->w, display->h);
			} else if (!GotoPageKeepView(doc, doc->page_cur + (rtd->right_to_left ? -1 : 1), vw, display->h, rtd->right_to_left)) break;
			*flags |= FLAG_RENDER; break;
		case SDL_CONTROLLER_BUTTON_START: return OpenTOCPanel( rtd, flags );
		default: break;
	}
	return NORMAL;
}

/* Apply Select+button shortcuts directly from controller input. */
static void	SelectChord( Green_RTD *rtd, Uint8 button, unsigned short *flags )
{
	Green_Document *doc = Green_IsDocValid( rtd ) ? rtd->doc : NULL;
	SDL_Surface *display = DrawSurface();
	switch (button)
	{
		case SDL_CONTROLLER_BUTTON_A:
			rtd->night = !rtd->night; Green_SaveSettings( rtd ); *flags |= FLAG_RENDER; break;
		case SDL_CONTROLLER_BUTTON_Y:
			if (doc) Green_MirrorH( doc );
			*flags |= FLAG_RENDER;
			break;
		case SDL_CONTROLLER_BUTTON_X:
			if (doc) Green_MirrorV( doc );
			*flags |= FLAG_RENDER;
			break;
		case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER:
			rtd->right_to_left = !rtd->right_to_left;
			if (doc) { if (doc->page_count > 1) doc->xoffset = doc->yoffset = 0; else SetPageStart(doc, rtd->right_to_left, ViewWidth(rtd), display->h); }
			Green_SaveSettings( rtd );
			*flags |= FLAG_RENDER;
			break;
		case SDL_CONTROLLER_BUTTON_LEFTSHOULDER:
			rtd->side_by_side = !rtd->side_by_side;
			if (doc) { doc->xoffset = doc->yoffset = 0; if (rtd->side_by_side && doc->page_count > 1) ClampSpreadOffset(rtd, display->w, display->h); else Green_ValidateOffset(doc, display->w, display->h); }
			Green_SaveSettings( rtd );
			*flags |= FLAG_RENDER; break;
		default: break;
	}
}
static SDL_GameController *OpenFirstController( void )
{
	int i;

	for (i = 0; i < SDL_NumJoysticks(); i++)
	{
		SDL_GameController *controller;

		if (!SDL_IsGameController( i ))
			continue;

		controller = SDL_GameControllerOpen( i );
		if (controller)
			return controller;
	}

	return NULL;
}

static bool DocumentStateEqual( const Green_Document *a, const Green_Document *b )
{
	return a->page_cur == b->page_cur && a->xoffset == b->xoffset
		&& a->yoffset == b->yoffset && a->rotation == b->rotation
		&& a->mirrored == b->mirrored && a->fit_method == b->fit_method
		&& a->finescale == b->finescale;
}

int	Green_SDL_Main( Green_RTD *rtd )
{
	SDL_GameController	*controller = NULL;
	SDL_Surface	*display;
	SDL_Event	event;
	unsigned short	flags = FLAG_RENDER;
	unsigned char	event_count;
	Green_Document state_snapshot;
	bool state_dirty = false;
	Uint32 state_save_at = 0;
	int exit_status = 0;
	
	if (SDL_Init( SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER ))
	{
		fprintf( stderr, "SDL_Init failed: %s\n", SDL_GetError() );
		return GREEN_EXIT_SDL_INIT;
	}
	
	/* Open a recognized SDL GameController; hot-plug is also supported below. */
	SDL_GameControllerEventState( SDL_ENABLE );
	controller = OpenFirstController();
	if (!controller)
		fprintf( stderr, "Waiting for an SDL game controller...\n" );

	/* SDL2: window + renderer, drawing goes into an off-screen XRGB8888 framebuffer (see Present) */
	{
		const char	*rot = getenv( "GREEN_ROTATE180" );
		Uint32	wflags = SDL_WINDOW_RESIZABLE;
		int	ww = rtd->width, wh = rtd->height, ow, oh;
		
		g_rotate180 = rot && atoi( rot );
		if (rtd->flags & GREEN_FULLSCREEN)
			wflags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
		if (ww <= 0 || wh <= 0)
		{
			SDL_DisplayMode	dm;
			
			if (SDL_GetDesktopDisplayMode( 0, &dm ) == 0)
			{
				if (ww <= 0) ww = dm.w;
				if (wh <= 0) wh = dm.h;
			}
			else
			{
				if (ww <= 0) ww = 640;
				if (wh <= 0) wh = 480;
			}
		}
		
		SDL_SetHint( SDL_HINT_RENDER_SCALE_QUALITY, "0" );
		g_window = SDL_CreateWindow( "MuGreen2 - the PDF reader", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, ww, wh, wflags );
		g_renderer = g_window ? SDL_CreateRenderer( g_window, -1, 0 ) : NULL;
		if (g_renderer)
		{
			SDL_RendererInfo info;
			const char *video_driver = SDL_GetCurrentVideoDriver();
			fprintf( stderr, "SDL video driver: %s\n", video_driver ? video_driver : "(none)" );
			if (SDL_GetRendererInfo( g_renderer, &info ) == 0)
				fprintf( stderr, "SDL renderer: %s (flags=0x%08x, max texture=%dx%d)\n",
					info.name ? info.name : "(unknown)", info.flags, info.max_texture_width, info.max_texture_height );
			else
				fprintf( stderr, "SDL_GetRendererInfo failed: %s\n", SDL_GetError() );
		}
		if (!g_window || !g_renderer || SDL_GetRendererOutputSize( g_renderer, &ow, &oh ) || CreateFramebuffer( ow, oh ))
		{
			fprintf( stderr, "SDL display setup failed: %s\n", SDL_GetError() );
			if (controller) SDL_GameControllerClose( controller );
			SDL_Quit();
			return GREEN_EXIT_DISPLAY;
		}
		display = DrawSurface();
	}
	if (Green_IsDocValid(rtd)) {
		if (rtd->side_by_side && rtd->doc->page_count > 1) ClampSpreadOffset(rtd, display->w, display->h);
		else Green_ValidateOffset(rtd->doc, display->w, display->h);
	}
	if (Green_IsDocValid( rtd))
		state_snapshot = *rtd->doc;
	else
		memset( &state_snapshot, 0, sizeof( state_snapshot ) );
	
	do
	{
		if ((flags & FLAG_QUIT))
			break;
		if (Green_IsDocValid( rtd) && !DocumentStateEqual( &state_snapshot, rtd->doc ))
		{
			state_snapshot = *rtd->doc;
			state_dirty = true;
			state_save_at = SDL_GetTicks() + 750;
		}
				/* Check the save deadline on every loop, not only when the event
		 * queue becomes idle. A steady stream of SDL events must not starve
		 * persistence indefinitely. */
		if (state_dirty && SDL_TICKS_PASSED( SDL_GetTicks(), state_save_at ))
		{
			if (Green_IsDocValid( rtd))
				Green_SaveDocumentState( rtd->doc );
			state_dirty = false;
		}
if (flags & FLAG_RENDER)
		{
			int failed_page = rtd->doc ? rtd->doc->page_cur : 0;
			if (Render( rtd, &failed_page ))
			{
				if (!g_has_presented_frame && !g_rendered_content_this_frame)
				{
					if (rtd->side_by_side && rtd->doc->page_count > 1)
					{
						int pair_next = ((failed_page > rtd->doc->page_cur ? failed_page : rtd->doc->page_cur) / 2 + 1) * 2;
						if (pair_next < rtd->doc->page_count) rtd->doc->page_cur = pair_next;
						else rtd->doc->page_cur = rtd->doc->page_count > 1 ? rtd->doc->page_count - 2 : 0;
					}
					else if (failed_page + 1 < rtd->doc->page_count) rtd->doc->page_cur = failed_page + 1;
					else if (failed_page > 0) rtd->doc->page_cur = failed_page - 1;
					Green_SaveDocumentState(rtd->doc);
					exit_status = GREEN_EXIT_RENDER;
					flags |= FLAG_QUIT;
					continue;
				}
				fprintf(stderr, "MuGreen2: page %d is incomplete; showing an error placeholder.\n", failed_page + 1);
			}
			g_has_presented_frame = true;
			flags ^= FLAG_RENDER;
		}
		
		event_count = 0;
		if (g_list_nav_dir && (g_state == TOC || g_state == HELP))
		{
			Uint32 now = SDL_GetTicks();
			int timeout = g_list_nav_next <= now ? 0 : (int)(g_list_nav_next - now);
			if (!SDL_WaitEventTimeout( &event, timeout ))
			{
				if (g_state == TOC)
					TOCStepSel( g_list_nav_dir );
				else
					HelpStepScroll( g_list_nav_dir, HelpMaxScroll( display ) );
				g_list_nav_next = SDL_GetTicks() + LIST_NAV_RATE_MS;
				flags |= FLAG_RENDER;
				continue;
			}
		}
		else if (state_dirty)
		{
			Uint32 now = SDL_GetTicks();
			int timeout = SDL_TICKS_PASSED( now, state_save_at ) ? 0 : (int)(state_save_at - now);
			if (!SDL_WaitEventTimeout( &event, timeout ))
				continue;
		}
		else if (!SDL_WaitEvent( &event ))
		{
			fprintf( stderr, "MuGreen2: SDL event wait failed: %s\n", SDL_GetError() );
			exit_status = GREEN_EXIT_EVENT;
			flags |= FLAG_QUIT;
			continue;
		}
		
		do
		{
			switch (event.type)
			{
				case SDL_QUIT: flags |= FLAG_QUIT; break;
				case SDL_CONTROLLERBUTTONDOWN:
				{
					Uint8 button = event.cbutton.button;
					if (button == SDL_CONTROLLER_BUTTON_BACK)
					{
						g_select_button_held = true; g_select_chord_used = false;
						if (g_start_button_held) { g_exit_chord = true; flags |= FLAG_QUIT; }
						break;
					}
					if (button == SDL_CONTROLLER_BUTTON_START)
					{
						g_start_button_held = true;
						if (g_select_button_held) { g_exit_chord = true; flags |= FLAG_QUIT; }
						else if (!g_exit_chord)
						{
							if (g_state == NORMAL) g_state = OpenTOCPanel( rtd, &flags );
							else if (g_state == TOC) g_state = TOCInput( rtd, button, &flags );
						}
						break;
					}
					if (button == SDL_CONTROLLER_BUTTON_GUIDE)
					{
						g_x_held = g_y_held = false;
						if (g_state != NORMAL || g_toc_active || g_help_active)
						{ ListNavStop(); g_state = NORMAL; g_toc_active = g_help_active = false; flags |= FLAG_RENDER; }
						else CloseCurrentFile( rtd, &flags );
						break;
					}
					if (g_select_button_held)
					{
						if (button == SDL_CONTROLLER_BUTTON_A || button == SDL_CONTROLLER_BUTTON_X
							|| button == SDL_CONTROLLER_BUTTON_Y || button == SDL_CONTROLLER_BUTTON_LEFTSHOULDER
							|| button == SDL_CONTROLLER_BUTTON_RIGHTSHOULDER)
						{
							g_select_chord_used = true;
							if (g_state == NORMAL) SelectChord( rtd, button, &flags );
							else if (g_state == TOC) g_state = TOCInput( rtd, button, &flags );
							else if (g_state == HELP) g_state = HelpInput( button, &flags );
						}
						else if (g_state == TOC) g_state = TOCInput( rtd, button, &flags );
						else if (g_state == HELP) g_state = HelpInput( button, &flags );
						break;
					}
					if (button == SDL_CONTROLLER_BUTTON_X && g_state == NORMAL) g_y_held = true;
					else if (button == SDL_CONTROLLER_BUTTON_Y && g_state == NORMAL) g_x_held = true;
					else if (g_state == TOC) g_state = TOCInput( rtd, button, &flags );
					else if (g_state == HELP) g_state = HelpInput( button, &flags );
					else if (g_state == NORMAL) g_state = NormalInput( rtd, button, &flags );
					break;
				}
				case SDL_CONTROLLERBUTTONUP:
				{
					Uint8 button = event.cbutton.button;
					if (button == SDL_CONTROLLER_BUTTON_BACK)
					{
						g_select_button_held = false;
						if (!g_select_chord_used && !g_exit_chord)
						{
							if (g_state == HELP) { ListNavStop(); g_help_active = false; g_state = NORMAL; flags |= FLAG_RENDER; }
							else g_state = OpenHelpPanel( &flags );
						}
						if (!g_start_button_held) g_exit_chord = false;
						g_select_chord_used = false;
					}
					else if (button == SDL_CONTROLLER_BUTTON_START)
					{ g_start_button_held = false; if (!g_select_button_held) g_exit_chord = false; }
					else if (button == SDL_CONTROLLER_BUTTON_DPAD_UP || button == SDL_CONTROLLER_BUTTON_DPAD_DOWN)
						ListNavStop();
					else if (button == SDL_CONTROLLER_BUTTON_X && g_y_held)
					{
						if (g_state == NORMAL && Green_IsDocValid( rtd ))
						{ Green_Document *doc = rtd->doc; if (rtd->side_by_side && doc->page_count > 1) ZoomSpread(rtd, display->w, display->h, doc->finescale / rtd->zoomstep); else Green_Zoom(doc, ViewWidth(rtd), display->h, doc->finescale / rtd->zoomstep); flags |= FLAG_RENDER; }
						g_y_held = false;
					}
					else if (button == SDL_CONTROLLER_BUTTON_Y && g_x_held)
					{
						if (g_state == NORMAL && Green_IsDocValid( rtd ))
					{ Green_Document *doc = rtd->doc; if (rtd->side_by_side && doc->page_count > 1) ZoomSpread(rtd, display->w, display->h, doc->finescale * rtd->zoomstep); else Green_Zoom(doc, ViewWidth(rtd), display->h, doc->finescale * rtd->zoomstep); flags |= FLAG_RENDER; }
						g_x_held = false;
					}
					break;
				}
				case SDL_CONTROLLERAXISMOTION:
					if (event.caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERLEFT || event.caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERRIGHT)
					{
						bool left = event.caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERLEFT;
						bool *held = left ? &g_l2_held : &g_r2_held;
						bool down = event.caxis.value > 16000;
						if (down && !*held && !g_select_button_held && g_state == NORMAL && Green_IsDocValid( rtd ))
							SkipPages( rtd->doc, rtd, left ? -10 : 10, ViewWidth( rtd ), display->h, &flags );
						*held = down;
					}
					break;
				case SDL_CONTROLLERDEVICEADDED:
					if (!controller && SDL_IsGameController( event.cdevice.which )) controller = SDL_GameControllerOpen( event.cdevice.which );
					break;
				case SDL_CONTROLLERDEVICEREMOVED:
					if (controller && SDL_JoystickInstanceID( SDL_GameControllerGetJoystick( controller ) ) == event.cdevice.which)
					{
						SDL_GameControllerClose( controller ); controller = OpenFirstController();
						g_select_button_held = g_select_chord_used = g_start_button_held = g_exit_chord = false;
						g_x_held = g_y_held = g_l2_held = g_r2_held = false; ListNavStop();
					}
					break;
				case SDL_WINDOWEVENT:
					if (event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED)
					{
						int ow, oh; SDL_GetRendererOutputSize( g_renderer, &ow, &oh );
						if (ow != display->w || oh != display->h)
						{
							if (CreateFramebuffer( ow, oh ))
							{
								fprintf( stderr, "MuGreen2: resize failed: %s\n", SDL_GetError() );
								exit_status = GREEN_EXIT_RESIZE;
								flags |= FLAG_QUIT;
								break;
							}
							display = DrawSurface();
							if (Green_IsDocValid(rtd)) {
								if (rtd->side_by_side && rtd->doc->page_count > 1) ClampSpreadOffset(rtd, display->w, display->h);
								else Green_ValidateOffset(rtd->doc, display->w, display->h);
							}
						}
						flags |= FLAG_RENDER;
					}
					else if (event.window.event == SDL_WINDOWEVENT_EXPOSED || event.window.event == SDL_WINDOWEVENT_RESTORED)
						flags |= FLAG_RENDER;
					break;
				default:
					/* Unrelated SDL events are ignored. */
					break;
			}

			event_count++;
			
		}	while (event_count && SDL_PollEvent( &event ));
		
	}	while (!(flags&FLAG_QUIT));
	
	Green_SaveAllLastPages( rtd );
	if (controller) SDL_GameControllerClose( controller );
	DestroyFramebuffer();
	if (g_renderer)
		SDL_DestroyRenderer( g_renderer );
	if (g_window)
		SDL_DestroyWindow( g_window );
	SDL_Quit();
	
	
	return exit_status;
}
