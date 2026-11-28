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
#include <math.h>
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

/* Physical display mounting correction, independent of document rotation. */
static int	g_display_rotation = 0;

/* Hold D-pad to repeat reading scroll or TOC/help navigation. */
static int	g_list_nav_dir = 0;
static int	g_list_nav_button = -1;
static Uint32	g_list_nav_next = 0;
#define LIST_NAV_DELAY_MS	300
#define LIST_NAV_RATE_MS	100
#define PAGE_INDICATOR_DURATION_MS	1200
#define ACTION_OVERLAY_DURATION_MS	1400
static int	g_page_indicator_page = -1;
static Uint32	g_page_indicator_until = 0;
static char	g_action_message[80] = "";
static Uint32	g_action_message_until = 0;
/* SDL presents the logical content framebuffer with one final transform. */
static int	g_reader_rotation = 0;

static bool	g_help_active = false;
/* Manual override for devices whose physical rotation direction differs. */
static bool	g_spread_order_reversed = false;
static bool	g_has_presented_frame = false;
static bool	g_rendered_content_this_frame = false;
static int	g_help_scroll = 0;

static const char	*g_help_lines[] = {
	"Game controller controls",
	"",
	"D-pad          Scroll / navigate; hold to repeat",
	"A              Fit page to width / select TOC entry",
	"Select+Left    Rotate clockwise",
	"Select+Right   Rotate anti-clockwise",
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
	"Hotkey+Up      Toggle stack axis",
	"Hotkey+Down    Toggle facing-page order (dual-page only)",
	"Hotkey+B       Toggle night mode",
	"Hotkey+X       Mirror horizontally",
	"Hotkey+Y       Mirror vertically",
	"Hotkey+L1      Toggle dual-page mode",
	"Hotkey+R1      Toggle right-to-left manga reading",
	"",
	"Reading behaviour",
	"",
	"Hold D-pad to repeat scrolling",
	"Default scroll step: 90% of viewport",
	"Page/spread number appears briefly after navigation",
	"Dual-page mode pairs consecutive pages where possible",
	"Rotate the reader to match the handheld orientation",
	"",
	"In document TOC",
	"",
	"Up/Down        Move selection",
	"Left/Right     Collapse / expand sections",
	"A              Jump to selected section",
	"B / Start      Close TOC",
	"",
	"Optional controls",
	"",
	"Layout: -controller-layout=nintendo|xbox",
	"Fixed controls: -no-control-rotation",
	"L2 / R2        Page jumps (if supported)",
	"",
	"About MuGreen2",
	"Version " MUGREEN2_VERSION,
	"License: AGPL-3.0-only",
	"github.com/Nevrdid/MuGreen2",
};
#define HELP_LINE_COUNT	((int)(sizeof( g_help_lines ) / sizeof( g_help_lines[0] )))


static SDL_Surface *DrawSurface(void);
static Uint8 *DstPixel(SDL_Surface *d, int lx, int ly);
void DrawString(SDL_Surface *display, int x, int y, const char *str, Uint32 color);

/* Combined-spread geometry: offsets are device pixels from the spread's
 * leading top-left corner, allowing either page to move across the viewport. */

static RState	OpenHelpPanel( unsigned short *flags );
static void	RenderHelp( SDL_Surface *display );
static void RenderHelpContent(SDL_Surface *display);

static void RenderHelp(SDL_Surface *display)
{
	if (g_help_active)
		RenderHelpContent(display);
}

static RState	HelpInput( Uint8 button, unsigned short *flags );
static void	HelpStepScroll( int dir, int max_scroll );
static int	HelpMaxScroll( SDL_Surface *display );
static int	HelpMaxScrollForHeight( int height );

/* ---- SDL2 display ----
 * All drawing goes into g_fb, an off-screen XRGB8888 surface (so the PDF backend can render straight into it,
 * whatever pixel format the window/driver uses). Present() uploads it to a streaming texture and shows it.
 * display_rotation rotates the final image independently of document controls. */
static SDL_Window	*g_window = NULL;
static SDL_Renderer	*g_renderer = NULL;
static SDL_Texture	*g_texture = NULL;
static SDL_Surface	*g_fb = NULL;
/* Logical-PDF UI is kept on its own layer so document mirroring never flips
 * controller-facing overlays. Both layers receive the same final rotation. */
static SDL_Surface	*g_overlay_fb = NULL;
static SDL_Texture	*g_overlay_texture = NULL;

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
	if (g_overlay_texture)
		SDL_DestroyTexture( g_overlay_texture );
	if (g_overlay_fb)
		SDL_FreeSurface( g_overlay_fb );
	g_texture = NULL;
	g_fb = NULL;
	g_overlay_texture = NULL;
	g_overlay_fb = NULL;
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
	{
		g_overlay_fb = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ARGB8888);
		g_overlay_texture = SDL_CreateTexture(g_renderer, SDL_PIXELFORMAT_ARGB8888,
			SDL_TEXTUREACCESS_STREAMING, w, h);
		if (!g_overlay_fb || !g_overlay_texture
			|| SDL_SetTextureBlendMode(g_overlay_texture, SDL_BLENDMODE_BLEND) != 0)
		{
			DestroyFramebuffer();
			return -1;
		}
	}
	return 0;
}

static Uint8	*DstPixel( SDL_Surface *d, int lx, int ly )
{
	return (Uint8 *)d->pixels + ly * d->pitch + lx * d->format->BytesPerPixel;
}

/* PDF content and UI are composed in logical coordinates. Present() applies
 * the same final reader/display rotation to both layers. */
static void	Present( void )
{
	int ow, oh;
	SDL_Rect dst;
	double angle = (double)(((g_reader_rotation + g_display_rotation) & 3) * 90);

	if (!g_fb || !g_texture)
		return;
	SDL_UpdateTexture(g_texture, NULL, g_fb->pixels, g_fb->pitch);
	SDL_RenderClear(g_renderer);
	if (SDL_GetRendererOutputSize(g_renderer, &ow, &oh) == 0)
	{
		dst.w = g_fb->w;
		dst.h = g_fb->h;
		dst.x = (ow - dst.w) / 2;
		dst.y = (oh - dst.h) / 2;
		SDL_RenderCopyEx(g_renderer, g_texture, NULL, &dst, angle, NULL,
			SDL_FLIP_NONE);
		/* UI follows reader rotation, but is deliberately not mirrored. */
		if (g_overlay_fb && g_overlay_texture)
		{
			SDL_UpdateTexture(g_overlay_texture, NULL, g_overlay_fb->pixels,
				g_overlay_fb->pitch);
			SDL_RenderCopyEx(g_renderer, g_overlay_texture, NULL, &dst, angle,
				NULL, SDL_FLIP_NONE);
		}
	}
	SDL_RenderPresent(g_renderer);
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

static void RenderTOCContent(SDL_Surface *display)
{
	SDL_Rect panel, hl;
	Uint32 title_col, entry_col, sel_col, sel_bg;
	int panel_extent, content_w, content_h, origin_x, origin_y;
	int x0, y0, line_h, max_lines, start, i, indent, text_w;
	Uint8 er, eg, eb;

	if (!g_toc_active) return;

	/* Draw the TOC along the logical left edge. The final presentation
	 * transform rotates this overlay with the reader. */
	panel_extent = display->w * 3 / 4;
	if (panel_extent < 200)
		panel_extent = display->w < 200 ? display->w : 200;

	origin_x = 0;
	origin_y = 0;
	content_w = panel_extent;
	content_h = display->h;

	panel.x = origin_x;
	panel.y = origin_y;
	panel.w = content_w;
	panel.h = content_h;
	SDL_FillRect(display, &panel, SDL_MapRGB(display->format, 0x14, 0x14, 0x14));

	title_col = SDL_MapRGB(display->format, 0xE8, 0xE8, 0xE8);
	entry_col = SDL_MapRGB(display->format, 0xB8, 0xB8, 0xB8);
	sel_col = SDL_MapRGB(display->format, 0xFF, 0xFF, 0x00);
	sel_bg = SDL_MapRGB(display->format, 0x2E, 0x2E, 0x5E);

	x0 = origin_x + 8;
	y0 = origin_y + 8;
	DrawString(display, x0, y0, "Table of Contents", title_col);
	y0 += 22;
	line_h = 18;
	max_lines = (origin_y + content_h - y0) / line_h;
	if (max_lines <= 0) return;

	if (g_toc_count <= 0)
	{
		DrawString(display, x0, y0, "No table of contents", entry_col);
		return;
	}

	start = 0;
	if (g_toc_count > max_lines)
	{
		start = g_toc_sel - max_lines + 1;
		if (start < 0) start = 0;
		if (start + max_lines > g_toc_count) start = g_toc_count - max_lines;
	}

	for (i = start; i < g_toc_count && i < start + max_lines; i++)
	{
		TOCNode *n = g_toc_entries[i].node;
		int y = y0 + (i - start) * line_h;

		if (i == g_toc_sel)
		{
			hl.x = origin_x;
			hl.y = y;
			hl.w = content_w;
			hl.h = line_h;
			SDL_FillRect(display, &hl, sel_bg);
			er = eg = 0xFF;
			eb = 0x00;
		}
		else er = eg = eb = 0xB8;

		indent = x0 + g_toc_entries[i].depth * 14;
		if (n->n_children > 0)
			DrawString(display, indent, y, n->open ? "-" : "+",
				i == g_toc_sel ? sel_col : entry_col);

		text_w = origin_x + content_w - indent - 28;
		if (text_w < 8) text_w = 8;
		DrawUtf8(display, indent + 26, y, text_w, line_h,
			n->title ? n->title : "", er, eg, eb);
	}
}

static void RenderTOC(SDL_Surface *display, Green_RTD *rtd)
{
	(void)rtd;
	if (g_toc_active)
		RenderTOCContent(display);
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
	g_list_nav_button = -1;
	g_list_nav_next = 0;
}

static void	ListNavStart( Uint8 button )
{
	if (button == SDL_CONTROLLER_BUTTON_DPAD_UP || button == SDL_CONTROLLER_BUTTON_DPAD_LEFT)
		g_list_nav_dir = -1;
	else if (button == SDL_CONTROLLER_BUTTON_DPAD_DOWN || button == SDL_CONTROLLER_BUTTON_DPAD_RIGHT)
		g_list_nav_dir = 1;
	else
		return;
	g_list_nav_button = button;
	g_list_nav_next = SDL_GetTicks() + LIST_NAV_DELAY_MS;
}

static int IsDpadButton(Uint8 button)
{
	return button == SDL_CONTROLLER_BUTTON_DPAD_UP || button == SDL_CONTROLLER_BUTTON_DPAD_DOWN
		|| button == SDL_CONTROLLER_BUTTON_DPAD_LEFT || button == SDL_CONTROLLER_BUTTON_DPAD_RIGHT;
}

/* Convert SDL GameController button IDs into the reader's logical controls. */
static Uint8 MapControllerButton(Green_RTD *rtd, Uint8 button)
{
	int rotation = (rtd->controls_rotate && Green_IsDocValid(rtd)) ? (rtd->doc->rotation & 3) : 0;

	if (rtd->xbox_layout)
	{
		if (button == SDL_CONTROLLER_BUTTON_A) button = SDL_CONTROLLER_BUTTON_B;
		else if (button == SDL_CONTROLLER_BUTTON_B) button = SDL_CONTROLLER_BUTTON_A;
		else if (button == SDL_CONTROLLER_BUTTON_X) button = SDL_CONTROLLER_BUTTON_Y;
		else if (button == SDL_CONTROLLER_BUTTON_Y) button = SDL_CONTROLLER_BUTTON_X;
	}

	switch (rotation)
	{
		case 1:
			switch (button) {
				case SDL_CONTROLLER_BUTTON_A: button = SDL_CONTROLLER_BUTTON_X; break;
				case SDL_CONTROLLER_BUTTON_B: button = SDL_CONTROLLER_BUTTON_A; break;
				case SDL_CONTROLLER_BUTTON_X: button = SDL_CONTROLLER_BUTTON_Y; break;
				case SDL_CONTROLLER_BUTTON_Y: button = SDL_CONTROLLER_BUTTON_B; break;
				case SDL_CONTROLLER_BUTTON_DPAD_UP: button = SDL_CONTROLLER_BUTTON_DPAD_LEFT; break;
				case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: button = SDL_CONTROLLER_BUTTON_DPAD_UP; break;
				case SDL_CONTROLLER_BUTTON_DPAD_DOWN: button = SDL_CONTROLLER_BUTTON_DPAD_RIGHT; break;
				case SDL_CONTROLLER_BUTTON_DPAD_LEFT: button = SDL_CONTROLLER_BUTTON_DPAD_DOWN; break;
			}
			break;
		case 2:
			switch (button) {
				case SDL_CONTROLLER_BUTTON_A: button = SDL_CONTROLLER_BUTTON_Y; break;
				case SDL_CONTROLLER_BUTTON_B: button = SDL_CONTROLLER_BUTTON_X; break;
				case SDL_CONTROLLER_BUTTON_X: button = SDL_CONTROLLER_BUTTON_B; break;
				case SDL_CONTROLLER_BUTTON_Y: button = SDL_CONTROLLER_BUTTON_A; break;
				case SDL_CONTROLLER_BUTTON_DPAD_UP: button = SDL_CONTROLLER_BUTTON_DPAD_DOWN; break;
				case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: button = SDL_CONTROLLER_BUTTON_DPAD_LEFT; break;
				case SDL_CONTROLLER_BUTTON_DPAD_DOWN: button = SDL_CONTROLLER_BUTTON_DPAD_UP; break;
				case SDL_CONTROLLER_BUTTON_DPAD_LEFT: button = SDL_CONTROLLER_BUTTON_DPAD_RIGHT; break;
			}
			break;
		case 3:
			switch (button) {
				case SDL_CONTROLLER_BUTTON_A: button = SDL_CONTROLLER_BUTTON_B; break;
				case SDL_CONTROLLER_BUTTON_B: button = SDL_CONTROLLER_BUTTON_Y; break;
				case SDL_CONTROLLER_BUTTON_X: button = SDL_CONTROLLER_BUTTON_A; break;
				case SDL_CONTROLLER_BUTTON_Y: button = SDL_CONTROLLER_BUTTON_X; break;
				case SDL_CONTROLLER_BUTTON_DPAD_UP: button = SDL_CONTROLLER_BUTTON_DPAD_RIGHT; break;
				case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: button = SDL_CONTROLLER_BUTTON_DPAD_DOWN; break;
				case SDL_CONTROLLER_BUTTON_DPAD_DOWN: button = SDL_CONTROLLER_BUTTON_DPAD_LEFT; break;
				case SDL_CONTROLLER_BUTTON_DPAD_LEFT: button = SDL_CONTROLLER_BUTTON_DPAD_UP; break;
			}
			break;
		default:
			break;
	}

	return button;
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


/* Input has already been rotated into logical overlay coordinates. */
static int TOCSelectDirection(Uint8 button)
{
	if (button == SDL_CONTROLLER_BUTTON_DPAD_UP) return -1;
	if (button == SDL_CONTROLLER_BUTTON_DPAD_DOWN) return 1;
	return 0;
}

static int TOCHierarchyDirection(Uint8 button)
{
	if (button == SDL_CONTROLLER_BUTTON_DPAD_LEFT) return -1;
	if (button == SDL_CONTROLLER_BUTTON_DPAD_RIGHT) return 1;
	return 0;
}

static void TOCListNavStart(Uint8 button, int dir)
{
	if (!dir) return;
	g_list_nav_dir = dir;
	g_list_nav_button = button;
	g_list_nav_next = SDL_GetTicks() + LIST_NAV_DELAY_MS;
}

static RState TOCInput(Green_RTD *rtd, Uint8 button, unsigned short *flags)
{
	int dir;
	TOCNode *n;
	switch (button)
	{
		case SDL_CONTROLLER_BUTTON_B:
		case SDL_CONTROLLER_BUTTON_START:
			ListNavStop(); g_toc_active = false; *flags |= FLAG_RENDER; return NORMAL;
		case SDL_CONTROLLER_BUTTON_A:
			if (g_toc_count > 0 && TOCGoToPage(rtd, g_toc_entries[g_toc_sel].node, flags))
			{ ListNavStop(); g_toc_active = false; return NORMAL; }
			break;
		default:
			dir = TOCSelectDirection(button);
			if (dir)
			{
				TOCStepSel(dir);
				TOCListNavStart(button, dir);
				*flags |= FLAG_RENDER;
				break;
			}
			dir = TOCHierarchyDirection(button);
			if (dir && g_toc_count > 0)
			{
				n = g_toc_entries[g_toc_sel].node;
				if (dir > 0 && n->n_children > 0 && !n->open)
				{ n->open = true; RebuildTOCFlat(); }
				else if (dir < 0)
				{
					if (n->n_children > 0 && n->open)
					{ n->open = false; RebuildTOCFlat(); }
					else
					{ int idx = FindParentIndex(n); if (idx >= 0) g_toc_sel = idx; }
				}
				*flags |= FLAG_RENDER;
			}
			break;
	}
	return TOC;
}

/* A single effective order rule drives spread placement and D-pad page turns.
 * Horizontal RTL and the manual override each reverse the horizontal order;
 * in vertical layout RTL keeps document order, while the manual override can
 * still swap the stack. */
static int LogicalSpreadOrderReversed(Green_RTD *rtd)
{
    return g_spread_order_reversed
        ^ (!rtd->spread_vertical && rtd->right_to_left);
}

static int LogicalPDFPageDelta(Green_RTD *rtd, Uint8 button)
{
	Green_Document *doc = rtd->doc;
	int page_delta;

	switch (button) {
		case SDL_CONTROLLER_BUTTON_LEFTSHOULDER:
		case SDL_CONTROLLER_BUTTON_DPAD_LEFT:
		case SDL_CONTROLLER_BUTTON_DPAD_UP:
			page_delta = -1;
			break;
		case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER:
		case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:
		case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
			page_delta = 1;
			break;
		default:
			return 0;
	}

	/* D-pad directions are already logical after rotation mapping. In a
	 * spread, page progression follows its effective visual order. In
	 * single-page mode, preserve the established RTL horizontal behaviour.
	 * Vertical RTL progression remains document order. */
	if (button == SDL_CONTROLLER_BUTTON_LEFTSHOULDER
		|| button == SDL_CONTROLLER_BUTTON_RIGHTSHOULDER) {
		if ((doc->rotation & 3) == 1 || (doc->rotation & 3) == 2)
			page_delta = -page_delta;
		if (rtd->right_to_left)
			page_delta = -page_delta;
	} else if (IsDpadButton(button)) {
		int reverse = rtd->side_by_side && doc->page_count > 1
			? LogicalSpreadOrderReversed(rtd)
			: (rtd->right_to_left
				&& (button == SDL_CONTROLLER_BUTTON_DPAD_LEFT
					|| button == SDL_CONTROLLER_BUTTON_DPAD_RIGHT));
		if (reverse)
			page_delta = -page_delta;
	}
	return page_delta;
}

/* An unmatched final page in dual-page mode occupies one slot of a spread.
 * Fit it as though the missing partner had the same dimensions, so it keeps
 * the same apparent page size as pages in complete spreads. */
static double LogicalPDFPageScale(Green_RTD *rtd, SDL_Surface *display)
{
    Green_Document *doc = rtd->doc;
    double page_w, page_h, fit_w, fit_h, base_scale;

    if (rtd->side_by_side && doc->page_count > 1
        && doc->page_cur == doc->page_count - 1 && (doc->page_cur % 2) == 0) {
        Green_PageSize(doc, doc->page_cur, &page_w, &page_h);
        if (!isfinite(page_w) || !isfinite(page_h) || page_w <= 0 || page_h <= 0)
            return Green_FitLogical(doc, display->w, display->h) * doc->finescale;
        if (rtd->spread_vertical) {
            fit_w = page_w;
            fit_h = page_h * 2.0;
        } else {
            fit_w = page_w * 2.0;
            fit_h = page_h;
        }
        switch (doc->fit_method) {
            case NATURAL: base_scale = 1.0; break;
            case WIDTH: base_scale = display->w / fit_w; break;
            case HEIGHT: base_scale = display->h / fit_h; break;
            case PAGE:
            default:
                base_scale = display->w / fit_w;
                if (display->h / fit_h < base_scale) base_scale = display->h / fit_h;
                break;
        }
        return base_scale * doc->finescale;
    }
    return Green_FitLogical(doc, display->w, display->h) * doc->finescale;
}

/* Pan in logical page coordinates. Only an additional press beyond an edge
 * changes page: first reach the edge, then cross it on the next D-pad repeat
 * or press. Page dimensions and viewport dimensions are both logical, so
 * their bounds remain correct when the physical display axes are swapped. */
static int ScrollLogicalPDFPage(Green_RTD *rtd, SDL_Surface *display, Uint8 button, int allow_page_turn)
{
	Green_Document *doc = rtd->doc;
	double scale;
	int page_w, page_h, max_x, max_y;
	int dx = 0, dy = 0, logical_dx, logical_dy, step;
	int old_x, old_y, proposed_x, proposed_y;

	switch (button) {
		case SDL_CONTROLLER_BUTTON_DPAD_LEFT: dx = -1; break;
		case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: dx = 1; break;
		case SDL_CONTROLLER_BUTTON_DPAD_UP: dy = -1; break;
		case SDL_CONTROLLER_BUTTON_DPAD_DOWN: dy = 1; break;
		default: return 0;
	}
	scale = LogicalPDFPageScale(rtd, display);
	if (pb_device_size(doc->doc, doc->page_cur, (float)scale,  &page_w, &page_h))
		return 0;

	/* Input directions are already in logical page coordinates. */
	logical_dx = dx;
	logical_dy = dy;

	/* Mirroring affects presentation only; controller input remains unchanged. */

	max_x = page_w > display->w ? page_w - display->w : 0;
	max_y = page_h > display->h ? page_h - display->h : 0;
	old_x = doc->xoffset;
	old_y = doc->yoffset;
	if (old_x < 0) old_x = 0;
	else if (old_x > max_x) old_x = max_x;
	if (old_y < 0) old_y = 0;
	else if (old_y > max_y) old_y = max_y;
	doc->xoffset = old_x;
	doc->yoffset = old_y;

	/* Handle each axis independently. If the page fits within the logical
	 * viewport on that axis, there is no panning to do and D-pad input
	 * changes page immediately. If it overflows, pan until the corresponding
	 * edge is reached; only a further press changes page. */
	if ((logical_dx < 0 && ((max_x > 0 && old_x == 0)
			|| page_w <= display->w))
		|| (logical_dx > 0 && ((max_x > 0 && old_x == max_x)
			|| page_w <= display->w))
		|| (logical_dy < 0 && ((max_y > 0 && old_y == 0)
			|| page_h <= display->h))
		|| (logical_dy > 0 && ((max_y > 0 && old_y == max_y)
			|| page_h <= display->h))) {
		if (!allow_page_turn) return 0;
		int page_delta = LogicalPDFPageDelta(rtd, button);
		if (page_delta && Green_GotoPage(doc, doc->page_cur + page_delta, true)) {
			int next_page = rtd->right_to_left ? page_delta < 0 : page_delta > 0;
			int enter_from_right = rtd->right_to_left ? next_page : !next_page;
			/* Page turns always enter at the top. The reading direction chooses
			 * the horizontal entry corner; reader rotation never changes it. */
			scale = LogicalPDFPageScale(rtd, display);
			doc->xoffset = 0;
			doc->yoffset = 0;
			if (!pb_device_size(doc->doc, doc->page_cur, (float)scale,
				&page_w, &page_h) && enter_from_right
				&& page_w > display->w)
				doc->xoffset = page_w - display->w;
			return 1;
		}
		/* At the first/last page, keep the view clamped at this edge. */
		return 0;
	}

	step = logical_dx ? (int)(display->w * rtd->step)
		: (int)(display->h * rtd->step);
	if (step < 1) step = 1;
	proposed_x = old_x + logical_dx * step;
	proposed_y = old_y + logical_dy * step;
	doc->xoffset = proposed_x < 0 ? 0 : (proposed_x > max_x ? max_x : proposed_x);
	doc->yoffset = proposed_y < 0 ? 0 : (proposed_y > max_y ? max_y : proposed_y);

	return doc->xoffset != old_x || doc->yoffset != old_y;
}

/* Zoom a logical single page around the viewport centre. */
static int ZoomLogicalPDFPage(Green_RTD *rtd, SDL_Surface *display, double factor)
{
	Green_Document *doc = rtd->doc;
	double base_scale, old_scale, new_scale, center_x, center_y;
	double old_page_x, old_page_y, new_fs = doc->finescale * factor;
	int old_page_w, old_page_h, new_page_w, new_page_h;
	int old_x = doc->xoffset, old_y = doc->yoffset;

	if (new_fs < 0.05) new_fs = 0.05;
	if (new_fs > 20.0) new_fs = 20.0;
	if (new_fs == doc->finescale) return 0;
	base_scale = LogicalPDFPageScale(rtd, display) / doc->finescale;
	old_scale = base_scale * doc->finescale;
	new_scale = base_scale * new_fs;
	if (old_scale <= 0.0 || new_scale <= 0.0
		|| pb_device_size(doc->doc, doc->page_cur, (float)old_scale,  &old_page_w, &old_page_h)
		|| pb_device_size(doc->doc, doc->page_cur, (float)new_scale,  &new_page_w, &new_page_h))
		return 0;

	old_page_x = old_page_w < display->w ? (display->w - old_page_w) / 2.0 : -old_x;
	old_page_y = old_page_h < display->h ? (display->h - old_page_h) / 2.0 : -old_y;
	center_x = (display->w / 2.0 - old_page_x) / old_scale;
	center_y = (display->h / 2.0 - old_page_y) / old_scale;

	doc->finescale = new_fs;
	doc->xoffset = new_page_w <= display->w ? 0
		: (int)(center_x * new_scale - display->w / 2.0 + 0.5);
	doc->yoffset = new_page_h <= display->h ? 0
		: (int)(center_y * new_scale - display->h / 2.0 + 0.5);
	if (doc->xoffset < 0) doc->xoffset = 0;
	else if (doc->xoffset > new_page_w - display->w) doc->xoffset = new_page_w - display->w;
	if (doc->yoffset < 0) doc->yoffset = 0;
	else if (doc->yoffset > new_page_h - display->h) doc->yoffset = new_page_h - display->h;

	return 1;
}

/* Logical-coordinate facing-page geometry. The stack preference is kept
 * unchanged across reader rotations: horizontal remains side-by-side and
 * vertical remains top-and-bottom from the user's viewing orientation. */
static void RenderPageWarning(SDL_Surface *display, Green_Document *doc,
	int page, int x1, int y1, int x2)
{
	if (!pb_page_has_warnings(doc->doc, page))
		return;
	if (x2 - x1 >= 18)
	{
		SDL_Rect marker = { x2 - 18, y1 + 4, 14, 14 };
		SDL_FillRect(display, &marker, SDL_MapRGB(display->format, 0xD0, 0x90, 0x20));
		DrawString(display, marker.x + 3, marker.y + 1, "!",
			SDL_MapRGB(display->format, 0x10, 0x10, 0x10));
	}
	if (pb_take_page_warning(doc->doc, page))
	{
		char message[256];
		snprintf(message, sizeof(message),
			"Page %d contains content that MuPDF could not fully interpret", page + 1);
		Green_LogMessage(message);
		fprintf(stderr, "MuGreen2: %s\n", message);
	}
}

static int GetLogicalSpreadMetrics(Green_RTD *rtd, SDL_Surface *display,
    double *scale, int *lp, int *rp, int *lw, int *lh, int *rw, int *rh,
    int *vertical)
{
    Green_Document *doc = rtd->doc;
    double lpage_w, lpage_h, rpage_w, rpage_h, fit_w, fit_h;
    double base_scale;
    if (!rtd->side_by_side || doc->page_count < 2) return 0;
    if (doc->page_cur % 2 == 0) {
        *lp = doc->page_cur;
        *rp = doc->page_cur + 1;
    } else {
        *lp = doc->page_cur - 1;
        *rp = doc->page_cur;
    }
    if (*lp < 0 || *rp < 0 || *lp >= doc->page_count || *rp >= doc->page_count)
        return 0;
    *vertical = rtd->spread_vertical;
    /* Apply the same effective-order predicate used by D-pad page turns.
     * Start with document order, then reverse for horizontal RTL or the
     * explicit manual override (which also works in vertical layout). */
    if (*vertical && *lp > *rp) {
        int page = *lp; *lp = *rp; *rp = page;
    }
    if (LogicalSpreadOrderReversed(rtd)) {
        int page = *lp; *lp = *rp; *rp = page;
    }
    Green_PageSize(doc, *lp, &lpage_w, &lpage_h);
    Green_PageSize(doc, *rp, &rpage_w, &rpage_h);
    if (*vertical) {
        fit_w = lpage_w > rpage_w ? lpage_w : rpage_w;
        fit_h = lpage_h + rpage_h;
    } else {
        fit_w = lpage_w + rpage_w;
        fit_h = lpage_h > rpage_h ? lpage_h : rpage_h;
    }
    if (fit_w <= 0 || fit_h <= 0) return 0;
    switch (doc->fit_method) {
        case NATURAL: base_scale = 1.0; break;
        case WIDTH: base_scale = display->w / fit_w; break;
        case HEIGHT: base_scale = display->h / fit_h; break;
        case PAGE:
        default:
            base_scale = display->w / fit_w;
            if (display->h / fit_h < base_scale) base_scale = display->h / fit_h;
            break;
    }
    *scale = base_scale * doc->finescale;
    if (pb_device_size(doc->doc, *lp, (float)*scale,  lw, lh)
        || pb_device_size(doc->doc, *rp, (float)*scale,  rw, rh))
        return 0;
    return 1;
}

static int LogicalSpreadPageTurn(Green_RTD *rtd, SDL_Surface *display,
    int page_delta)
{
    Green_Document *doc = rtd->doc;
    int lp, rp, lw, lh, rw, rh, vertical, spread_w, spread_h;
    int target_page, target_x, target_y, target_w;
    int next_page, enter_from_right, max_x, max_y, order_reversed;
    double scale;
    int pair = (doc->page_cur / 2) * 2;
    int step = page_delta * 2;

    if (!page_delta || !Green_GotoPage(doc, pair + step, true)) return 0;
    if (!GetLogicalSpreadMetrics(rtd, display, &scale, &lp, &rp,
        &lw, &lh, &rw, &rh, &vertical)) return 1;

    spread_w = vertical ? (lw > rw ? lw : rw) : lw + rw;
    spread_h = vertical ? lh + rh : (lh > rh ? lh : rh);
    order_reversed = LogicalSpreadOrderReversed(rtd);
    /* LogicalPDFPageDelta already returns document-order movement, so a
     * positive delta always means advancing to a later document page. */
    next_page = page_delta > 0;
    enter_from_right = order_reversed ? next_page : !next_page;

    /* Always align the first page of the destination spread in reading
     * order. This means a backward turn lands on the previous-previous page,
     * allowing the other page of that spread to remain visible as well. */
    target_page = vertical || !order_reversed ? lp : rp;

    if (target_page == lp) {
        target_x = vertical ? (spread_w - lw) / 2 : 0;
        target_y = vertical ? 0 : (spread_h - lh) / 2;
        target_w = lw;
    } else {
        target_x = vertical ? (spread_w - rw) / 2 : lw;
        target_y = vertical ? lh : (spread_h - rh) / 2;
        target_w = rw;
    }

    max_x = spread_w > display->w ? spread_w - display->w : 0;
    max_y = spread_h > display->h ? spread_h - display->h : 0;

    /* Align the selected page's top edge with the viewport. Enter from the
     * effective reading-side corner: the shared order rule also accounts for
     * the manual slot override. All coordinates are logical. */
    /* Align to the spread's outer edge on the cross-axis. Pages are
     * centred within that axis, so aligning to the selected page instead
     * can clip a taller partner above (horizontal) or a wider partner
     * to the left (vertical). Keep directional entry on the page-turn axis. */
    doc->xoffset = vertical ? 0 : (enter_from_right
        ? target_x + target_w - display->w : target_x);
    doc->yoffset = vertical ? target_y : 0;
    if (doc->xoffset < 0) doc->xoffset = 0;
    else if (doc->xoffset > max_x) doc->xoffset = max_x;
    if (doc->yoffset < 0) doc->yoffset = 0;
    else if (doc->yoffset > max_y) doc->yoffset = max_y;

    return 1;
}

static int ScrollLogicalPDFSpread(Green_RTD *rtd, SDL_Surface *display, Uint8 button, int allow_page_turn)
{
    Green_Document *doc = rtd->doc;
    int lp, rp, lw, lh, rw, rh, vertical, spread_w, spread_h;
    int dx = 0, dy = 0, logical_dx, logical_dy, max_x, max_y, step;
    int old_x, old_y, proposed_x, proposed_y;
    double scale;
    switch (button) {
        case SDL_CONTROLLER_BUTTON_DPAD_LEFT: dx = -1; break;
        case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: dx = 1; break;
        case SDL_CONTROLLER_BUTTON_DPAD_UP: dy = -1; break;
        case SDL_CONTROLLER_BUTTON_DPAD_DOWN: dy = 1; break;
        default: return 0;
    }
    if (!GetLogicalSpreadMetrics(rtd, display, &scale, &lp, &rp,
        &lw, &lh, &rw, &rh, &vertical))
        return ScrollLogicalPDFPage(rtd, display, button, allow_page_turn);
    spread_w = vertical ? (lw > rw ? lw : rw) : lw + rw;
    spread_h = vertical ? lh + rh : (lh > rh ? lh : rh);
    logical_dx = dx;
    logical_dy = dy;
    /* Mirroring affects presentation only; controller input remains unchanged. */
    max_x = spread_w > display->w ? spread_w - display->w : 0;
    max_y = spread_h > display->h ? spread_h - display->h : 0;
    old_x = doc->xoffset < 0 ? 0 : (doc->xoffset > max_x ? max_x : doc->xoffset);
    old_y = doc->yoffset < 0 ? 0 : (doc->yoffset > max_y ? max_y : doc->yoffset);
    doc->xoffset = old_x; doc->yoffset = old_y;
    if ((logical_dx < 0 && old_x == 0) || (logical_dx > 0 && old_x == max_x)
        || (logical_dy < 0 && old_y == 0) || (logical_dy > 0 && old_y == max_y)) {
        if (!allow_page_turn) return 0;
        return LogicalSpreadPageTurn(rtd, display, LogicalPDFPageDelta(rtd, button));
    }
    step = logical_dx ? (int)(display->w * rtd->step) : (int)(display->h * rtd->step);
    if (step < 1) step = 1;
    proposed_x = old_x + logical_dx * step;
    proposed_y = old_y + logical_dy * step;
    doc->xoffset = proposed_x < 0 ? 0 : (proposed_x > max_x ? max_x : proposed_x);
    doc->yoffset = proposed_y < 0 ? 0 : (proposed_y > max_y ? max_y : proposed_y);
    return doc->xoffset != old_x || doc->yoffset != old_y;
}

static int ZoomLogicalPDFSpread(Green_RTD *rtd, SDL_Surface *display, double factor)
{
    Green_Document *doc = rtd->doc;
    int lp, rp, lw, lh, rw, rh, vertical, old_w, old_h, new_w, new_h, max_x, max_y;
    double scale, old_scale, old_fs = doc->finescale, anchor_x, anchor_y;
    int old_x = doc->xoffset, old_y = doc->yoffset;
    if (!GetLogicalSpreadMetrics(rtd, display, &old_scale, &lp, &rp,
        &lw, &lh, &rw, &rh, &vertical)) return 0;
    old_w = vertical ? (lw > rw ? lw : rw) : lw + rw;
    old_h = vertical ? lh + rh : (lh > rh ? lh : rh);
    anchor_x = old_x + display->w / 2.0
        - (old_w < display->w ? (display->w - old_w) / 2.0 : 0);
    anchor_y = old_y + display->h / 2.0
        - (old_h < display->h ? (display->h - old_h) / 2.0 : 0);
    doc->finescale *= factor;
    if (doc->finescale < 0.05) doc->finescale = 0.05;
    if (doc->finescale > 20.0) doc->finescale = 20.0;
    if (doc->finescale == old_fs) return 0;
    if (!GetLogicalSpreadMetrics(rtd, display, &scale, &lp, &rp,
        &lw, &lh, &rw, &rh, &vertical)) {
        doc->finescale = old_fs; return 0;
    }
    /* The anchor is measured in old spread pixels; convert it to the new
     * scale before deriving offsets, otherwise zoom shifts the viewed point. */
    anchor_x *= scale / old_scale;
    anchor_y *= scale / old_scale;
    new_w = vertical ? (lw > rw ? lw : rw) : lw + rw;
    new_h = vertical ? lh + rh : (lh > rh ? lh : rh);
    doc->xoffset = (int)(anchor_x - display->w / 2.0
        + (new_w < display->w ? (display->w - new_w) / 2.0 : 0) + 0.5);
    doc->yoffset = (int)(anchor_y - display->h / 2.0
        + (new_h < display->h ? (display->h - new_h) / 2.0 : 0) + 0.5);
    max_x = new_w > display->w ? new_w - display->w : 0;
    max_y = new_h > display->h ? new_h - display->h : 0;
    if (doc->xoffset < 0) doc->xoffset = 0;
    else if (doc->xoffset > max_x) doc->xoffset = max_x;
    if (doc->yoffset < 0) doc->yoffset = 0;
    else if (doc->yoffset > max_y) doc->yoffset = max_y;
    return 1;
}

/* Render a page crop with its mirror applied to that page alone. The
 * requested mirror axis is in the final reader view; odd reader rotations
 * exchange the corresponding logical source axis. */
static int RenderLogicalPageRegion(Green_RTD *rtd, SDL_Surface *display,
    int page, int page_w, int page_h, double scale,
    int local_x, int local_y, int width, int height, int dst_x, int dst_y)
{
    Green_Document *doc = rtd->doc;
    int mirror_horizontal = (!doc->mirror_vertical) ^ ((doc->rotation + g_display_rotation) & 1);
    int mirror_vertical = !mirror_horizontal;
    int source_x = local_x, source_y = local_y;
    SDL_Surface *temporary;
    int x, y;

    if (!doc->mirrored)
        return pb_render(doc->doc, page, (float)scale, rtd->night,
            source_x, source_y, width, height, PB_FMT_XRGB8888,
            DstPixel(display, dst_x, dst_y), display->pitch);

    if (mirror_horizontal)
        source_x = page_w - local_x - width;
    else
        source_y = page_h - local_y - height;

    temporary = SDL_CreateRGBSurfaceWithFormat(0, width, height, 32,
        SDL_PIXELFORMAT_XRGB8888);
    if (!temporary)
        return -2;

    if (pb_render(doc->doc, page, (float)scale, rtd->night,
        source_x, source_y, width, height, PB_FMT_XRGB8888,
        temporary->pixels, temporary->pitch)) {
        SDL_FreeSurface(temporary);
        return -1;
    }

    for (y = 0; y < height; y++) {
        const Uint32 *src = (const Uint32 *)((const Uint8 *)temporary->pixels
            + (mirror_vertical ? height - 1 - y : y) * temporary->pitch);
        Uint32 *dst = (Uint32 *)((Uint8 *)display->pixels
            + (dst_y + y) * display->pitch) + dst_x;
        for (x = 0; x < width; x++)
            dst[x] = src[mirror_horizontal ? width - 1 - x : x];
    }
    SDL_FreeSurface(temporary);
    return 0;
}

static void RenderLogicalSpreadPage(Green_RTD *rtd, SDL_Surface *display,
    int page, int px, int py, int pw, int ph, double scale, int *failed_page)
{
    int x1 = px < 0 ? 0 : px, y1 = py < 0 ? 0 : py;
    int x2 = px + pw > display->w ? display->w : px + pw;
    int y2 = py + ph > display->h ? display->h : py + ph;
    const char *error;
    if (x1 >= x2 || y1 >= y2) return;
    {
        int result = RenderLogicalPageRegion(rtd, display, page, pw, ph, scale,
            x1 - px, y1 - py, x2 - x1, y2 - y1, x1, y1);
        if (result == 0) {
            g_rendered_content_this_frame = true;
            RenderPageWarning(display, rtd->doc, page, x1, y1, x2);
            return;
        }
        error = result == -2 ? SDL_GetError() : pb_error_message(rtd->doc->doc);
    }
    if (error) {
        SDL_Rect failed = { x1, y1, x2 - x1, y2 - y1 };
        SDL_FillRect(display, &failed, SDL_MapRGB(display->format, 0x28, 0x18, 0x18));
        SDL_SetClipRect(display, &failed);
        DrawString(display, x1 + 8, y1 + 8, "Page cannot be rendered",
            SDL_MapRGB(display->format, 0xFF, 0x80, 0x80));
        DrawString(display, x1 + 8, y1 + 30, error,
            SDL_MapRGB(display->format, 0xE0, 0xE0, 0xE0));
        SDL_SetClipRect(display, NULL);
        fprintf(stderr, "MuGreen2: logical spread rendering failed on page %d: %s\n",
            page + 1, error);
        if (*failed_page < 0) *failed_page = page;
    }
}

static int RenderLogicalPDFSpread(Green_RTD *rtd, SDL_Surface *display,
    int *failed_page)
{
    int lp, rp, lw, lh, rw, rh, vertical, spread_w, spread_h, ox, oy;
    double scale;
    Green_Document *doc = rtd->doc;
    if (!GetLogicalSpreadMetrics(rtd, display, &scale, &lp, &rp,
        &lw, &lh, &rw, &rh, &vertical))
        return 0;
    spread_w = vertical ? (lw > rw ? lw : rw) : lw + rw;
    spread_h = vertical ? lh + rh : (lh > rh ? lh : rh);
    if (doc->xoffset < 0) doc->xoffset = 0;
    if (doc->yoffset < 0) doc->yoffset = 0;
    if (spread_w <= display->w) doc->xoffset = 0;
    else if (doc->xoffset > spread_w - display->w) doc->xoffset = spread_w - display->w;
    if (spread_h <= display->h) doc->yoffset = 0;
    else if (doc->yoffset > spread_h - display->h) doc->yoffset = spread_h - display->h;
    ox = (spread_w < display->w ? (display->w - spread_w) / 2 : 0) - doc->xoffset;
    oy = (spread_h < display->h ? (display->h - spread_h) / 2 : 0) - doc->yoffset;
    if (vertical) {
        RenderLogicalSpreadPage(rtd, display, lp, ox + (spread_w - lw) / 2,
            oy, lw, lh, scale, failed_page);
        RenderLogicalSpreadPage(rtd, display, rp, ox + (spread_w - rw) / 2,
            oy + lh, rw, rh, scale, failed_page);
    } else {
        RenderLogicalSpreadPage(rtd, display, lp, ox, oy + (spread_h - lh) / 2,
            lw, lh, scale, failed_page);
        RenderLogicalSpreadPage(rtd, display, rp, ox + lw, oy + (spread_h - rh) / 2,
            rw, rh, scale, failed_page);
    }
    return 1;
}

static int RenderLogicalPDFPage(Green_RTD *rtd, SDL_Surface *display, int *failed_page)
{
	Green_Document *doc = rtd->doc;
	double scale;
	int page_w, page_h, page_x, page_y, x1, y1, x2, y2, offset_x, offset_y;
	const char *error = NULL;
	scale = LogicalPDFPageScale(rtd, display);
	if (pb_device_size(doc->doc, doc->page_cur, (float)scale,  &page_w, &page_h))
		error = pb_error_message(doc->doc);
	if (error)
		goto render_error;

	/* Single-page fallback for unpaired first/last pages. */
	offset_x = doc->xoffset;
	offset_y = doc->yoffset;
	if (offset_x < 0) offset_x = 0;
	if (offset_y < 0) offset_y = 0;
	if (page_w <= display->w) offset_x = 0;
	else if (offset_x > page_w - display->w) offset_x = page_w - display->w;
	if (page_h <= display->h) offset_y = 0;
	else if (offset_y > page_h - display->h) offset_y = page_h - display->h;

	page_x = page_w < display->w ? (display->w - page_w) / 2 : -offset_x;
	page_y = page_h < display->h ? (display->h - page_h) / 2 : -offset_y;
	x1 = page_x < 0 ? 0 : page_x;
	y1 = page_y < 0 ? 0 : page_y;
	x2 = page_x + page_w > display->w ? display->w : page_x + page_w;
	y2 = page_y + page_h > display->h ? display->h : page_y + page_h;
	if (x1 < x2 && y1 < y2) {
        int result = RenderLogicalPageRegion(rtd, display, doc->page_cur,
            page_w, page_h, scale, x1 - page_x, y1 - page_y,
            x2 - x1, y2 - y1, x1, y1);
        if (result == -2)
            error = SDL_GetError();
        else if (result != 0)
            error = pb_error_message(doc->doc);
    }
	if (!error) {
		g_rendered_content_this_frame = true;
		if (x1 < x2 && y1 < y2)
			RenderPageWarning(display, doc, doc->page_cur, x1, y1, x2);
		return 0;
	}

render_error:
	{
		SDL_Rect failed = { 0, 0, display->w, display->h };
		SDL_FillRect(display, &failed, SDL_MapRGB(display->format, 0x28, 0x18, 0x18));
		DrawString(display, 8, 8, "Page cannot be rendered",
			SDL_MapRGB(display->format, 0xFF, 0x80, 0x80));
		if (error) {
			SDL_SetClipRect(display, &failed);
			DrawString(display, 8, 30, error, SDL_MapRGB(display->format, 0xE0, 0xE0, 0xE0));
			SDL_SetClipRect(display, NULL);
			fprintf(stderr, "MuGreen2: PDF rendering failed on page %d: %s\n",
				doc->page_cur + 1, error);
		}
		if (*failed_page < 0) *failed_page = doc->page_cur;
	}
	return -1;
}

static void ShowActionMessage(const char *message)
{
	snprintf(g_action_message, sizeof(g_action_message), "%s", message);
	g_action_message_until = SDL_GetTicks() + ACTION_OVERLAY_DURATION_MS;
}

static void RenderActionMessage(SDL_Surface *display)
{
	SDL_Rect box;
	int text_w = (int)strlen(g_action_message) * 12;
	Uint32 bg = SDL_MapRGB(display->format, 0x18, 0x18, 0x20);
	Uint32 fg = SDL_MapRGB(display->format, 0xF0, 0xF0, 0xF0);

	box.w = text_w + 24;
	if (box.w > display->w - 16) box.w = display->w - 16;
	if (box.w < 1) return;
	box.h = 28;
	box.x = (display->w - box.w) / 2;
	box.y = display->h - box.h - 8;
	SDL_FillRect(display, &box, bg);
	SDL_SetClipRect(display, &box);
	DrawString(display, box.x + 12, box.y + 7, g_action_message, fg);
	SDL_SetClipRect(display, NULL);
}

static void RenderLogicalPageIndicator(SDL_Surface *display, Green_RTD *rtd,
	Green_Document *doc)
{
	char label[64];
	int first = doc->page_cur, last = first, box_w;
	SDL_Rect box;
	Uint32 bg = SDL_MapRGB(display->format, 0x18, 0x18, 0x20);
	Uint32 fg = SDL_MapRGB(display->format, 0xF0, 0xF0, 0xF0);
	if (rtd->side_by_side && doc->page_count > 1) {
		first = (doc->page_cur / 2) * 2;
		last = first + 1;
	}
	if (last < doc->page_count && last != first)
		snprintf(label, sizeof(label), "%d-%d / %d", first + 1, last + 1, doc->page_count);
	else
		snprintf(label, sizeof(label), "%d / %d", first + 1, doc->page_count);
	box_w = (int)strlen(label) * 12 + 16;
	box.x = (display->w - box_w) / 2;
	box.y = 8;
	box.w = box_w;
	box.h = 24;
	SDL_FillRect(display, &box, bg);
	DrawString(display, box.x + 8, box.y + 5, label, fg);
}

static int	Render( Green_RTD *rtd, int *failed_page )
{
	Green_Document *doc;
	SDL_Surface *display;
	SDL_Rect rect;
	int rotation;

	*failed_page = -1;
	g_rendered_content_this_frame = false;
	display = DrawSurface();
	if (!display)
		return -1;

	if (Green_IsDocValid(rtd))
	{
		doc = rtd->doc;
		rotation = doc->rotation & 3;
		if (rotation != g_reader_rotation)
		{
			int ow, oh;
			if (SDL_GetRendererOutputSize(g_renderer, &ow, &oh) != 0
				|| CreateFramebuffer(((rotation + g_display_rotation) & 1) ? oh : ow,
					((rotation + g_display_rotation) & 1) ? ow : oh) != 0)
			{
				fprintf(stderr, "MuGreen2: logical framebuffer resize failed: %s\n",
					SDL_GetError());
				*failed_page = doc->page_cur;
				return -1;
			}
			g_reader_rotation = rotation;
			display = DrawSurface();
		}
		SDL_FillRect(display, NULL, SDL_MapRGB(display->format,
			rtd->night ? 0x10 : rtd->c_background.r,
			rtd->night ? 0x10 : rtd->c_background.g,
			rtd->night ? 0x10 : rtd->c_background.b));
		if (!(rtd->side_by_side && doc->page_count > 1
			&& RenderLogicalPDFSpread(rtd, display, failed_page)))
			(void)RenderLogicalPDFPage(rtd, display, failed_page);
	}
	else
	{
		rect.x = rect.y = 0;
		rect.w = display->w;
		rect.h = display->h;
		if (rtd->night)
			SDL_FillRect(display, &rect, SDL_MapRGB(display->format, 0x10, 0x10, 0x10));
		else
			SDL_FillRect(display, &rect, SDL_MapRGB(display->format,
				rtd->c_background.r, rtd->c_background.g, rtd->c_background.b));
	}

	if (g_overlay_fb)
	{
		SDL_FillRect(g_overlay_fb, NULL,
			SDL_MapRGBA(g_overlay_fb->format, 0, 0, 0, 0));
		if (Green_IsDocValid(rtd))
			RenderTOC(g_overlay_fb, rtd);
		RenderHelp(g_overlay_fb);
		if (Green_IsDocValid(rtd) && g_state == NORMAL
			&& g_page_indicator_until
			&& !SDL_TICKS_PASSED(SDL_GetTicks(), g_page_indicator_until))
			RenderLogicalPageIndicator(g_overlay_fb, rtd, rtd->doc);
		if (g_state == NORMAL && g_action_message[0] && g_action_message_until
			&& !SDL_TICKS_PASSED(SDL_GetTicks(), g_action_message_until))
			RenderActionMessage(g_overlay_fb);
	}
	Present();
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

static void SetLogicalPageStart(Green_RTD *rtd, SDL_Surface *display);

/* Reset the viewport to the physical reading edge after changing reading
 * direction. The selected logical offset axis depends on reader rotation. */
static void SetLogicalPageStart(Green_RTD *rtd, SDL_Surface *display)
{
	Green_Document *doc = rtd->doc;
	int page_w, page_h, max_x, max_y;
	double scale;
	int rotation = doc->rotation & 3;
	scale = LogicalPDFPageScale(rtd, display);
	doc->xoffset = doc->yoffset = 0;
	if (pb_device_size(doc->doc, doc->page_cur, (float)scale, 
		&page_w, &page_h))
		return;
	max_x = page_w > display->w ? page_w - display->w : 0;
	max_y = page_h > display->h ? page_h - display->h : 0;

	/* Select the physical reading edge in logical page coordinates. */
	if (rtd->right_to_left)
	{
		switch (rotation)
		{
			case 1: doc->yoffset = 0; break;
			case 2: doc->xoffset = 0; break;
			case 3: doc->yoffset = max_y; break;
			default: doc->xoffset = max_x; break;
		}
	}
	else
	{
		switch (rotation)
		{
			case 1: doc->yoffset = max_y; break;
			case 2: doc->xoffset = max_x; break;
			case 3: doc->yoffset = 0; break;
			default: doc->xoffset = 0; break;
		}
	}
}

/* Scroll in screen directions and change pages at the corresponding edges. */


static void SkipPages(Green_Document *doc, Green_RTD *rtd, int delta,
	unsigned short *flags)
{
	int page, max_x, max_y;

	if (!doc)
		return;
	page = doc->page_cur + (rtd->right_to_left ? -delta : delta);
	if (page < 0) page = 0;
	else if (page >= doc->page_count) page = doc->page_count - 1;
	if (page == doc->page_cur || !Green_GotoPage(doc, page, false))
		return;

	if (rtd->side_by_side && doc->page_count > 1)
	{
		int lp, rp, lw, lh, rw, rh, vertical;
		double scale;
		if (GetLogicalSpreadMetrics(rtd, DrawSurface(), &scale,
			&lp, &rp, &lw, &lh, &rw, &rh, &vertical))
		{
			int spread_w = vertical ? (lw > rw ? lw : rw) : lw + rw;
			int spread_h = vertical ? lh + rh : (lh > rh ? lh : rh);
			max_x = spread_w > DrawSurface()->w ? spread_w - DrawSurface()->w : 0;
			max_y = spread_h > DrawSurface()->h ? spread_h - DrawSurface()->h : 0;
		}
		else
		{
			int page_w, page_h;
			if (pb_device_size(doc->doc, doc->page_cur,
				(float)LogicalPDFPageScale(rtd, DrawSurface()), &page_w, &page_h))
			{
				*flags |= FLAG_RENDER;
				return;
			}
			max_x = page_w > DrawSurface()->w ? page_w - DrawSurface()->w : 0;
			max_y = page_h > DrawSurface()->h ? page_h - DrawSurface()->h : 0;
		}
	}
	else
	{
		int page_w, page_h;
		if (pb_device_size(doc->doc, doc->page_cur,
			(float)LogicalPDFPageScale(rtd, DrawSurface()),  &page_w, &page_h))
		{
			*flags |= FLAG_RENDER;
			return;
		}
		max_x = page_w > DrawSurface()->w ? page_w - DrawSurface()->w : 0;
		max_y = page_h > DrawSurface()->h ? page_h - DrawSurface()->h : 0;
	}
	if (doc->xoffset < 0) doc->xoffset = 0;
	if (doc->yoffset < 0) doc->yoffset = 0;
	if (doc->xoffset > max_x) doc->xoffset = max_x;
	if (doc->yoffset > max_y) doc->yoffset = max_y;
	*flags |= FLAG_RENDER;
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

/* Input has already been rotated into logical overlay coordinates. */
static int HelpScrollDirection(Uint8 button)
{
	if (button == SDL_CONTROLLER_BUTTON_DPAD_UP) return -1;
	if (button == SDL_CONTROLLER_BUTTON_DPAD_DOWN) return 1;
	return 0;
}

static void HelpListNavStart(Uint8 button, int dir)
{
	if (!dir) return;
	g_list_nav_dir = dir;
	g_list_nav_button = button;
	g_list_nav_next = SDL_GetTicks() + LIST_NAV_DELAY_MS;
}

static int HelpMaxScrollForHeight(int height)
{
	int line_h = 16, y0 = 48, visible = (height - y0 - 8) / line_h;
	if (visible < 1) visible = 1;
	return HELP_LINE_COUNT > visible ? HELP_LINE_COUNT - visible : 0;
}

static int HelpMaxScroll(SDL_Surface *display)
{
	return HelpMaxScrollForHeight(display->h);
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

static void RenderHelpContent(SDL_Surface *display)
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

static RState HelpInput(Uint8 button, unsigned short *flags)
{
	SDL_Surface *display = DrawSurface();
	int max_scroll = HelpMaxScrollForHeight(display->h);
	int dir;

	switch (button)
	{
		case SDL_CONTROLLER_BUTTON_B:
			ListNavStop();
			g_help_active = false;
			*flags |= FLAG_RENDER;
			return NORMAL;
		default:
			dir = HelpScrollDirection(button);
			if (dir)
			{
				HelpStepScroll(dir, max_scroll);
				HelpListNavStart(button, dir);
				*flags |= FLAG_RENDER;
			}
			break;
	}
	return HELP;
}

static RState	OpenTOCPanel( Green_RTD *rtd, unsigned short *flags )
{
	ListNavStop();
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

static RState NormalInput(Green_RTD *rtd, Uint8 button, unsigned short *flags, int allow_page_turn)
{
	Green_Document *doc = Green_IsDocValid(rtd) ? rtd->doc : NULL;
	SDL_Surface *display = DrawSurface();

	if (!doc)
		return button == SDL_CONTROLLER_BUTTON_START
			? OpenTOCPanel(rtd, flags) : NORMAL;

	switch (button)
	{
		case SDL_CONTROLLER_BUTTON_DPAD_UP:
		case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
		case SDL_CONTROLLER_BUTTON_DPAD_LEFT:
		case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:
			if (rtd->side_by_side && doc->page_count > 1)
			{
				if (ScrollLogicalPDFSpread(rtd, display, button, allow_page_turn))
					*flags |= FLAG_RENDER;
			}
			else if (ScrollLogicalPDFPage(rtd, display, button, allow_page_turn))
				*flags |= FLAG_RENDER;
			break;
		case SDL_CONTROLLER_BUTTON_A:
			doc->fit_method = WIDTH;
			doc->finescale = 1;
			doc->xoffset = doc->yoffset = 0;
			ShowActionMessage("View reset");
			*flags |= FLAG_RENDER;
			break;
		case SDL_CONTROLLER_BUTTON_LEFTSHOULDER:
		case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER:
		{
			int page_delta = LogicalPDFPageDelta(rtd, button);
			if (rtd->side_by_side && doc->page_count > 1)
			{
				if (LogicalSpreadPageTurn(rtd, display, page_delta))
					*flags |= FLAG_RENDER;
			}
			else if (page_delta && Green_GotoPage(doc,
				doc->page_cur + page_delta, true))
				*flags |= FLAG_RENDER;
			break;
		}
		case SDL_CONTROLLER_BUTTON_START:
			return OpenTOCPanel(rtd, flags);
		default:
			break;
	}
	return NORMAL;
}

/* Apply Select+button shortcuts directly from controller input. */
static void SelectChord(Green_RTD *rtd, Uint8 button, unsigned short *flags)
{
	Green_Document *doc = Green_IsDocValid(rtd) ? rtd->doc : NULL;
	SDL_Surface *display = DrawSurface();

	switch (button)
	{
		case SDL_CONTROLLER_BUTTON_B:
			rtd->night = !rtd->night;
			ShowActionMessage(rtd->night ? "Night mode ON" : "Night mode OFF");
			Green_SaveSettings(rtd);
			*flags |= FLAG_RENDER;
			break;
		case SDL_CONTROLLER_BUTTON_DPAD_LEFT:
		case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:
			if (doc)
			{
				if (button == SDL_CONTROLLER_BUTTON_DPAD_LEFT) {
					Green_RotateRight(doc);
					ShowActionMessage("Rotate +90");
				} else {
					Green_RotateLeft(doc);
					ShowActionMessage("Rotate -90");
				}
				doc->xoffset = doc->yoffset = 0;
				*flags |= FLAG_RENDER;
			}
			break;
		case SDL_CONTROLLER_BUTTON_DPAD_UP:
			if (doc && rtd->side_by_side && doc->page_count > 1)
			{
				rtd->spread_vertical = !rtd->spread_vertical;
				ShowActionMessage(rtd->spread_vertical ? "Vertical spread" : "Horizontal spread");
				doc->xoffset = doc->yoffset = 0;
				Green_SaveSettings(rtd);
				*flags |= FLAG_RENDER;
			}
			break;
		case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
			if (doc && rtd->side_by_side && doc->page_count > 1)
			{
				g_spread_order_reversed = !g_spread_order_reversed;
				ShowActionMessage(g_spread_order_reversed ? "Pages swapped" : "Page order restored");
				doc->xoffset = doc->yoffset = 0;
				*flags |= FLAG_RENDER;
			}
			break;
		case SDL_CONTROLLER_BUTTON_Y:
			if (doc) {
				Green_MirrorH(doc);
				ShowActionMessage("Horizontal mirror");
			}
			*flags |= FLAG_RENDER;
			break;
		case SDL_CONTROLLER_BUTTON_X:
			if (doc) {
				Green_MirrorV(doc);
				ShowActionMessage("Vertical mirror");
			}
			*flags |= FLAG_RENDER;
			break;
		case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER:
			rtd->right_to_left = !rtd->right_to_left;
			ShowActionMessage(rtd->right_to_left ? "Manga mode" : "Regular mode");
			if (doc)
			{
				if (rtd->side_by_side && doc->page_count > 1)
					doc->xoffset = doc->yoffset = 0;
				else
					SetLogicalPageStart(rtd, display);
			}
			Green_SaveSettings(rtd);
			*flags |= FLAG_RENDER;
			break;
		case SDL_CONTROLLER_BUTTON_LEFTSHOULDER:
			rtd->side_by_side = !rtd->side_by_side;
			ShowActionMessage(rtd->side_by_side ? "Dual-page mode ON" : "Dual-page mode OFF");
			if (doc)
				doc->xoffset = doc->yoffset = 0;
			Green_SaveSettings(rtd);
			*flags |= FLAG_RENDER;
			break;
		default:
			break;
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
		Uint32	wflags = SDL_WINDOW_RESIZABLE;
		int	ww = rtd->width, wh = rtd->height, ow, oh;
		
		g_display_rotation = rtd->display_rotation & 3;
		g_reader_rotation = Green_IsDocValid(rtd) ? (rtd->doc->rotation & 3) : 0;
		fprintf(stderr, "MuGreen2: logical rendering pipeline active\n");
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
		if (!g_window || !g_renderer || SDL_GetRendererOutputSize( g_renderer, &ow, &oh ) ||
			CreateFramebuffer(((g_reader_rotation + g_display_rotation) & 1) ? oh : ow,
			((g_reader_rotation + g_display_rotation) & 1) ? ow : oh))
		{
			fprintf( stderr, "SDL display setup failed: %s\n", SDL_GetError() );
			if (controller) SDL_GameControllerClose( controller );
			SDL_Quit();
			return GREEN_EXIT_DISPLAY;
		}
		display = DrawSurface();
	}
	
	if (Green_IsDocValid( rtd))
	{
		state_snapshot = *rtd->doc;
		g_page_indicator_page = rtd->doc->page_cur;
	}
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
		/* Check deadlines on every loop, not only when the event queue becomes
		 * idle. Continuous SDL input must not starve state saving or navigation. */
		if (state_dirty && SDL_TICKS_PASSED( SDL_GetTicks(), state_save_at ))
		{
			if (Green_IsDocValid( rtd))
				Green_SaveDocumentState( rtd->doc );
			state_dirty = false;
		}
		if (g_list_nav_dir && SDL_TICKS_PASSED(SDL_GetTicks(), g_list_nav_next))
		{
			if (g_state == NORMAL)
			{
				g_state = NormalInput(rtd, (Uint8)g_list_nav_button, &flags, 0);
			}
			else if (g_state == TOC)
				TOCStepSel(g_list_nav_dir);
			else if (g_state == HELP)
				HelpStepScroll(g_list_nav_dir, HelpMaxScrollForHeight(display->h));
			g_list_nav_next = SDL_GetTicks() + LIST_NAV_RATE_MS;
			flags |= FLAG_RENDER;
		}
		if (g_page_indicator_until && SDL_TICKS_PASSED(SDL_GetTicks(), g_page_indicator_until))
		{
			g_page_indicator_until = 0;
			if (g_state == NORMAL) flags |= FLAG_RENDER;
		}
		if (g_action_message_until && SDL_TICKS_PASSED(SDL_GetTicks(), g_action_message_until))
		{
			g_action_message_until = 0;
			g_action_message[0] = '\0';
			if (g_state == NORMAL) flags |= FLAG_RENDER;
		}
		if (Green_IsDocValid(rtd) && rtd->doc->page_cur != g_page_indicator_page)
		{
			g_page_indicator_page = rtd->doc->page_cur;
			g_page_indicator_until = SDL_GetTicks() + PAGE_INDICATOR_DURATION_MS;
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
			/* Render may recreate the logical framebuffer after a reader rotation. */
			display = DrawSurface();
			g_has_presented_frame = true;
			flags ^= FLAG_RENDER;
		}
		
		event_count = 0;
		{
			Uint32 now = SDL_GetTicks();
			int timeout = -1;
			if (g_list_nav_dir)
				timeout = SDL_TICKS_PASSED(now, g_list_nav_next) ? 0 : (int)(g_list_nav_next - now);
			if (state_dirty)
			{
				int remaining = SDL_TICKS_PASSED(now, state_save_at) ? 0 : (int)(state_save_at - now);
				if (timeout < 0 || remaining < timeout) timeout = remaining;
			}
			if (g_page_indicator_until)
			{
				int remaining = SDL_TICKS_PASSED(now, g_page_indicator_until) ? 0 : (int)(g_page_indicator_until - now);
				if (timeout < 0 || remaining < timeout) timeout = remaining;
			}
			if (g_action_message_until)
			{
				int remaining = SDL_TICKS_PASSED(now, g_action_message_until) ? 0 : (int)(g_action_message_until - now);
				if (timeout < 0 || remaining < timeout) timeout = remaining;
			}
			if (timeout >= 0)
			{
				if (!SDL_WaitEventTimeout(&event, timeout)) continue;
			}
			else if (!SDL_WaitEvent(&event))
		{
				fprintf( stderr, "MuGreen2: SDL event wait failed: %s\n", SDL_GetError() );
				exit_status = GREEN_EXIT_EVENT;
				flags |= FLAG_QUIT;
				continue;
			}
		}
		
		do
		{
			switch (event.type)
			{
				case SDL_QUIT: flags |= FLAG_QUIT; break;
				case SDL_CONTROLLERBUTTONDOWN:
				{
					Uint8 button = MapControllerButton(rtd, event.cbutton.button);
					if (button == SDL_CONTROLLER_BUTTON_BACK)
					{
						ListNavStop();
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
						if (button == SDL_CONTROLLER_BUTTON_B || button == SDL_CONTROLLER_BUTTON_X
							|| button == SDL_CONTROLLER_BUTTON_Y || button == SDL_CONTROLLER_BUTTON_DPAD_UP
							|| button == SDL_CONTROLLER_BUTTON_DPAD_DOWN
							|| button == SDL_CONTROLLER_BUTTON_DPAD_LEFT || button == SDL_CONTROLLER_BUTTON_DPAD_RIGHT
							|| button == SDL_CONTROLLER_BUTTON_LEFTSHOULDER || button == SDL_CONTROLLER_BUTTON_RIGHTSHOULDER)
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
					else if (g_state == NORMAL)
					{
						g_state = NormalInput( rtd, button, &flags, 1 );
						if (IsDpadButton(button)) ListNavStart(button);
					}
					break;
				}
				case SDL_CONTROLLERBUTTONUP:
				{
					Uint8 button = MapControllerButton(rtd, event.cbutton.button);
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
					else if (button == g_list_nav_button)
						ListNavStop();
					else if (button == SDL_CONTROLLER_BUTTON_X && g_y_held)
					{
						if (g_state == NORMAL && Green_IsDocValid( rtd ))
						{
							Green_Document *doc = rtd->doc;
							if (rtd->side_by_side && doc->page_count > 1) {
								if (ZoomLogicalPDFSpread(rtd, display, 1.0 / rtd->zoomstep)
									|| ZoomLogicalPDFPage(rtd, display, 1.0 / rtd->zoomstep)) flags |= FLAG_RENDER;
							} else if (ZoomLogicalPDFPage(rtd, display, 1.0 / rtd->zoomstep)) flags |= FLAG_RENDER;
						}
						g_y_held = false;
					}
					else if (button == SDL_CONTROLLER_BUTTON_Y && g_x_held)
					{
						if (g_state == NORMAL && Green_IsDocValid( rtd ))
						{
							Green_Document *doc = rtd->doc;
							if (rtd->side_by_side && doc->page_count > 1) {
								if (ZoomLogicalPDFSpread(rtd, display, rtd->zoomstep)
									|| ZoomLogicalPDFPage(rtd, display, rtd->zoomstep)) flags |= FLAG_RENDER;
							} else if (ZoomLogicalPDFPage(rtd, display, rtd->zoomstep)) flags |= FLAG_RENDER;
						}
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
							SkipPages(rtd->doc, rtd, left ? -10 : 10, &flags);
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
						int ow, oh, logical_w, logical_h;
						SDL_GetRendererOutputSize(g_renderer, &ow, &oh);
						logical_w = ((g_reader_rotation + g_display_rotation) & 1) ? oh : ow;
						logical_h = ((g_reader_rotation + g_display_rotation) & 1) ? ow : oh;
						if (logical_w != display->w || logical_h != display->h)
						{
							if (CreateFramebuffer(logical_w, logical_h))
							{
								fprintf( stderr, "MuGreen2: resize failed: %s\n", SDL_GetError() );
								exit_status = GREEN_EXIT_RESIZE;
								flags |= FLAG_QUIT;
								break;
							}
							display = DrawSurface();
							
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
