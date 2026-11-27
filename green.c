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
 */

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <sys/stat.h>
#include <errno.h>
#include "green.h"


typedef struct
{
	char	*uri;
	int	page;		/* 0-based */
	double	finescale;	/* zoom multiplier; 1 = fit only */
	int	xoffset, yoffset;
	int	rotation;
	bool	mirrored;
	Green_FitMethod	fit_method;
	bool	extended_state;
}	LastPageEntry;

static LastPageEntry	*g_last_pages = NULL;
static int	g_last_page_count = 0;
static bool	g_last_pages_loaded = false;


static char	*LastPagesPath( void )
{
	static char	path[512];
	const char	*home = getenv( "HOME" );
	
	if (!home || !*home)
		return NULL;
	if (snprintf( path, sizeof( path ), "%s/.green.pages", home ) >= (int)sizeof( path ))
		return NULL;
	return path;
}


static void	LoadLastPagesFile( void )
{
	FILE	*file;
	char	*path = LastPagesPath();
	char	line[4096];

	if (g_last_pages_loaded)
		return;
	g_last_pages_loaded = true;
	if (!path || !(file = fopen( path, "r" )))
		return;

	while (fgets( line, sizeof( line ), file ))
	{
		char	*uri, *end;
		int	page, x = 0, y = 0, rotation = 0, mirrored = 0, fit = WIDTH;
		bool	extended_state = false;
		double	finescale = 1.0;
		int	used = 0, fields;
		LastPageEntry	*tmp;

		if (line[0] == '#' || line[0] == '\n')
			continue;

		/* Current format: page scale x y rotation mirrored fit URI. */
		fields = sscanf( line, "%d %lf %d %d %d %d %d %n",
			&page, &finescale, &x, &y, &rotation, &mirrored, &fit, &used );
		if (fields == 7 && used > 0)
		{
			uri = line + used;
			extended_state = true;
		}
		else
		{
			/* Backward compatibility: old format was page [scale] URI. */
			used = 0;
			fields = sscanf( line, "%d %lf %n", &page, &finescale, &used );
			if (fields == 2 && used > 0)
				uri = line + used;
			else
			{
				used = 0;
				fields = sscanf( line, "%d %n", &page, &used );
				if (fields != 1 || used <= 0)
					continue;
				finescale = 1.0;
				uri = line + used;
			}
		}
		if (page < 1 || finescale <= 0)
			continue;
		while (*uri == ' ' || *uri == '\t') uri++;
		end = uri + strlen( uri );
		while (end > uri && (end[-1] == '\n' || end[-1] == '\r')) *--end = 0;
		if (!*uri) continue;

		tmp = realloc( g_last_pages, (g_last_page_count + 1) * sizeof( *g_last_pages ) );
		if (!tmp) break;
		g_last_pages = tmp;
		memset( &g_last_pages[g_last_page_count], 0, sizeof( *g_last_pages ) );
		g_last_pages[g_last_page_count].uri = strdup( uri );
		if (!g_last_pages[g_last_page_count].uri) break;
		g_last_pages[g_last_page_count].page = page - 1;
		g_last_pages[g_last_page_count].finescale = finescale;
		g_last_pages[g_last_page_count].xoffset = x;
		g_last_pages[g_last_page_count].yoffset = y;
		g_last_pages[g_last_page_count].rotation = ((rotation % 4) + 4) % 4;
		g_last_pages[g_last_page_count].mirrored = mirrored != 0;
		g_last_pages[g_last_page_count].fit_method = fit >= NATURAL && fit <= PAGE ? (Green_FitMethod)fit : WIDTH;
		g_last_pages[g_last_page_count].extended_state = extended_state;
		g_last_page_count++;
	}
	fclose( file );
}

static int	FindLastPageEntry( const char *uri )
{
	int	i;
	
	for (i = 0; i < g_last_page_count; i++)
		if (!strcmp( g_last_pages[i].uri, uri ))
			return i;
	return -1;
}

/* Per-document state lives below XDG_DATA_HOME (or ~/.local/share).
 * Two independent 64-bit hashes keep filenames short; the URI is stored
 * inside each file and checked on load to detect the vanishingly unlikely
 * event of a hash collision.
 */
static uint64_t StateHash( const char *text, uint64_t basis, uint64_t prime )
{
	uint64_t hash = basis;
	while (*text)
	{
		hash ^= (unsigned char)*text++;
		hash *= prime;
	}
	return hash;
}

static int MakeDirectory( const char *path )
{
	struct stat st;
	if (mkdir( path, 0700 ) && errno != EEXIST)
		return -1;
	if (stat( path, &st ) || !S_ISDIR( st.st_mode ))
		return -1;
	return 0;
}

static int EnsureDirectory( char *path )
{
	char *p;
	for (p = path + 1; *p; p++)
		if (*p == '/')
		{
			*p = 0;
			if (MakeDirectory( path )) { *p = '/'; return -1; }
			*p = '/';
		}
	return MakeDirectory( path );
}

static char *DocumentStatePath( const char *uri, char *path, size_t size )
{
	const char *base = getenv( "XDG_DATA_HOME" );
	const char *home = getenv( "HOME" );
	char directory[1024];
	uint64_t h1, h2;

	if (!uri || !*uri) return NULL;
	if (base && *base)
	{
		if (snprintf( directory, sizeof( directory ), "%s/mugreen2/documents", base ) >= (int)sizeof( directory ))
			return NULL;
		/* Create the XDG data root as well if it does not exist yet. */
		{
			char root[1024];
			if (snprintf( root, sizeof( root ), "%s", base ) >= (int)sizeof( root) || EnsureDirectory( root ))
				return NULL;
		}
	}
	else
	{
		if (!home || !*home ||
			snprintf( directory, sizeof( directory ), "%s/.local/share/mugreen2/documents", home ) >= (int)sizeof( directory ))
			return NULL;
	}
	if (EnsureDirectory( directory ))
		return NULL;

	h1 = StateHash( uri, UINT64_C(14695981039346656037), UINT64_C(1099511628211) );
	h2 = StateHash( uri, UINT64_C(7809847782465536322), UINT64_C(14029467366897019727) );
	if (snprintf( path, size, "%s/%016llx%016llx.state", directory,
		(unsigned long long)h1, (unsigned long long)h2 ) >= (int)size)
		return NULL;
	return path;
}

static bool LoadDocumentState( Green_Document *doc )
{
	char path[1200], saved_uri[4096], line[256];
	FILE *file;
	int page, x, y, rotation, mirrored, fit;

	if (!DocumentStatePath( doc->uri, path, sizeof( path )) || !(file = fopen( path, "r" )))
		return false;
	if (!fgets( line, sizeof( line ), file ) || strcmp( line, "MUGREEN2 1\n" )
		|| !fgets( saved_uri, sizeof( saved_uri ), file ))
	{
		fclose( file);
		return false;
	}
	saved_uri[strcspn( saved_uri, "\r\n" )] = 0;
	if (strcmp( saved_uri, doc->uri ) || fscanf( file, "%d %lf %d %d %d %d %d",
		&page, &doc->finescale, &x, &y, &rotation, &mirrored, &fit ) != 7)
	{
		fclose( file );
		return false;
	}
	fclose( file );
	if (page < 1 || page > doc->page_count || doc->finescale <= 0)
		return false;
	doc->page_cur = page - 1;
	doc->xoffset = x;
	doc->yoffset = y;
	doc->rotation = ((rotation % 4) + 4) % 4;
	doc->mirrored = mirrored != 0;
	doc->fit_method = fit >= NATURAL && fit <= PAGE ? (Green_FitMethod)fit : WIDTH;
	return true;
}

static void SaveDocumentState( Green_Document *doc )
{
	char path[1200], temporary[1240];
	FILE *file;
	if (!doc || !DocumentStatePath( doc->uri, path, sizeof( path )))
		return;
	if (snprintf( temporary, sizeof( temporary ), "%s.tmp.%ld", path, (long)getpid() ) >= (int)sizeof( temporary ))
		return;
	file = fopen( temporary, "w" );
	if (!file) return;
	fprintf( file, "MUGREEN2 1\n%s\n%d %.6f %d %d %d %d %d\n",
		doc->uri, doc->page_cur + 1,
		doc->finescale > 0 ? doc->finescale : 1.0,
		doc->xoffset, doc->yoffset, doc->rotation,
		doc->mirrored ? 1 : 0, (int)doc->fit_method );
	if (fclose( file ) || rename( temporary, path ))
		remove( temporary );
}

static void	StoreLastPage( Green_Document *doc, bool flush )
{
	(void)flush;
	SaveDocumentState( doc );
}

static void	RestoreLastPage( Green_Document *doc )
{
	int idx;
	if (!doc || !doc->uri)
		return;

	/* New per-document state takes precedence. */
	if (LoadDocumentState( doc ))
		return;

	/* One-time compatibility path: import the matching entry from the
	 * legacy ~/.green.pages index. Keep that file untouched as a backup.
	 */
	LoadLastPagesFile();
	idx = FindLastPageEntry( doc->uri );
	if (idx < 0)
		return;
	if (g_last_pages[idx].page >= 0 && g_last_pages[idx].page < doc->page_count)
		doc->page_cur = g_last_pages[idx].page;
	if (g_last_pages[idx].finescale > 0)
		doc->finescale = g_last_pages[idx].finescale;
	if (g_last_pages[idx].extended_state)
	{
		doc->xoffset = g_last_pages[idx].xoffset;
		doc->yoffset = g_last_pages[idx].yoffset;
		doc->rotation = g_last_pages[idx].rotation;
		doc->mirrored = g_last_pages[idx].mirrored;
		doc->fit_method = g_last_pages[idx].fit_method;
	}
	SaveDocumentState( doc );
	return;
}

char*	FilenameToURI( char *filename )
{
	const char	*prefix = "file:";
	char	abs = filename[0] == '/',
		*wd = abs ? NULL : getcwd( NULL, 0 ),
		*uri = malloc( strlen( prefix ) + strlen( filename ) + 1
			+ (abs ? 0 : strlen( wd ) + 1) );
	
	if (!uri || !(abs || wd))
	{
		free( uri );
		free( wd );
		return NULL;
	}
	
	if (abs)
		sprintf( uri, "%s%s", prefix, filename ); 
	else
	{
		sprintf( uri, "%s%s/%s", prefix, wd, filename );
		free( wd );
	}
	
	return uri;
}

void	Green_GetScrollRegion( Green_Document *doc, int w, int h, int *scroll_w, int *scroll_h )
{
	Green_GetDimension( doc, doc->page_cur, scroll_w, scroll_h, Green_Fit( doc, w, h ) * doc->finescale, doc->rotation % 2 );
	if (*scroll_w < w)
		*scroll_w = 0;
	else	
		*scroll_w -= w;
	
	if (*scroll_h < h)
		*scroll_h = 0;
	else	
		*scroll_h -= h;
	
	return;
}

int	Green_Open( Green_RTD *rtd, char *uri )
{
	Green_Document	*doc;

	if (!rtd || !uri || !*uri || rtd->doc)
		return -1;
	doc = malloc( sizeof( *doc ) );
	if (!doc)
		return -1;
	
	if (!strncmp( uri, "file:", 5 ))
		doc->uri = strdup( uri );
	else
		doc->uri = FilenameToURI( uri );
	
	if (!doc->uri)
	{
		free( doc );
		return -1;
	}
	
	doc->doc = pb_open( doc->uri );
	if (!doc->doc)
	{
		free( doc->uri );
		free( doc );
		return -2;
	}
	
	doc->page_count = pb_page_count( doc->doc );
	doc->page_cur = 0;
	doc->xoffset = 0;
	doc->yoffset = 0;
	doc->mirrored = false;
	doc->rotation = 0;
	doc->fit_method = rtd->fit_method;
	doc->finescale = 1;
	doc->bb = rtd->bb;
	RestoreLastPage( doc );
	rtd->doc = doc;
	return 0;
}

void	Green_Close( Green_RTD *rtd )
{
	Green_Document	*doc;

	if (!rtd || !rtd->doc)
		return;
	doc = rtd->doc;
	StoreLastPage( doc, true );
	pb_close( doc->doc );
	free( doc->uri );
	free( doc );
	rtd->doc = NULL;
}

void	Green_SaveAllLastPages( Green_RTD *rtd )
{
	if (!rtd)
		return;
	if (rtd->doc)
		StoreLastPage( rtd->doc, false );
	
	SaveLastPagesFile();
	Green_SaveSettings( rtd );
	return;
}

static char	*SettingsPath( void )
{
	static char	path[512];
	const char	*home = getenv( "HOME" );
	
	if (!home || !*home)
		return NULL;
	if (snprintf( path, sizeof( path ), "%s/.green.settings", home ) >= (int)sizeof( path ))
		return NULL;
	return path;
}

void	Green_LoadSettings( Green_RTD *rtd )
{
	FILE	*file;
	char	*path = SettingsPath();
	char	line[128];
	
	if (!rtd || !path)
		return;
	
	file = fopen( path, "r" );
	if (!file)
		return;
	
	while (fgets( line, sizeof( line ), file ))
	{
		if (!strncmp( line, "night=", 6 ))
			rtd->night = (line[6] != '0');
	}
	
	fclose( file );
	return;
}

void	Green_SaveSettings( Green_RTD *rtd )
{
	FILE	*file;
	char	*path = SettingsPath();
	
	if (!rtd || !path)
		return;
	
	file = fopen( path, "w" );
	if (!file)
		return;
	
	fprintf( file, "night=%d\n", rtd->night ? 1 : 0 );
	fclose( file );
	return;
}

double	Green_Fit( Green_Document *doc, int w, int h )
{
	double	pwidth, pheight;
	
	if (doc->fit_method == NATURAL)
		return 1;
	
	if (doc->rotation % 2)
		Green_PageSize( doc, doc->page_cur, &pheight, &pwidth );
	else
		Green_PageSize( doc, doc->page_cur, &pwidth, &pheight );
	
	if (doc->fit_method == WIDTH)
		return w / pwidth;
	else if (doc->fit_method == HEIGHT)
		return h / pheight;
	else if (doc->fit_method == PAGE)
		return (w / pwidth <= h / pheight) ? w / pwidth : h / pheight;
	
	return 1;
}

void	Green_ScrollRelative( Green_Document *doc, int x, int y, int w, int h, int bb_flag )
{
	unsigned char	bb_mode;
	int	doc_dx, doc_dy, doc_max_x, doc_max_y,
		abs_x = x < 0 ? -x : x,
		abs_y = y < 0 ? -y : y,
		max_x, max_y;
	bool	left_border, right_border, top_border, bottom_border, bb_done = false;
		
	Green_GetScrollRegion( doc, w, h, &max_x, &max_y );
	if (doc->rotation % 2)
	{
		doc_max_x = max_y;
		doc_max_y = max_x;
	}
	else
	{
		doc_max_x = max_x;
		doc_max_y = max_y;
	}

	if (doc->rotation == 1)
	{
		doc_dx = doc->mirrored ? -y : y;
		doc_dy = -x;
		left_border = doc->yoffset == doc_max_y;
		right_border = doc->yoffset == 0;
		if (doc->mirrored)
		{
			top_border = doc->xoffset == doc_max_x;
			bottom_border = doc->xoffset == 0;
		}
		else
		{
			top_border = doc->xoffset == 0;
			bottom_border = doc->xoffset == doc_max_x;
		}
	}
	else if (doc->rotation == 2)
	{
		doc_dx = -x;
		doc_dy = doc->mirrored ? y : -y;
		left_border = doc->xoffset == doc_max_x;
		right_border = doc->xoffset == 0;
		if (doc->mirrored)
		{
			top_border = doc->yoffset == 0;
			bottom_border = doc->yoffset == doc_max_y;
		}
		else
		{
			top_border = doc->yoffset == doc_max_y;
			bottom_border = doc->yoffset == 0;
		}
	}
	else if (doc->rotation == 3)
	{
		doc_dx = doc->mirrored ? y : -y;
		doc_dy = x;
		left_border = doc->yoffset == 0;
		right_border = doc->yoffset == doc_max_y;
		if (doc->mirrored)
		{
			top_border = doc->xoffset == 0;
			bottom_border = doc->xoffset == doc_max_x;
		}
		else
		{
			top_border = doc->xoffset == doc_max_x;
			bottom_border = doc->xoffset == 0;
		}
	}
	else
	{
		doc_dx = x;
		doc_dy = doc->mirrored ? -y : y;
		left_border = doc->xoffset == 0;
		right_border = doc->xoffset == doc_max_x;
		if (doc->mirrored)
		{
			top_border = doc->yoffset == doc_max_y;
			bottom_border = doc->yoffset == 0;
		}
		else
		{
			top_border = doc->yoffset == 0;
			bottom_border = doc->yoffset == doc_max_y;
		}
	}
	
	if (bb_flag)
	{
		if (abs_x > abs_y && x > 0 && right_border)
		{
			if (!(doc->bb&0xF0) && (doc->bb&0x03))
			{
				bb_mode = doc->bb&0x03;
				if (bb_mode == 1)
					bb_done = Green_GotoPage( doc, doc->page_cur + 1, false );
				else if (bb_mode == 2)
					bb_done = Green_GotoPage( doc, doc->page_cur - 1, false );
				
				if (bb_done)
				{
					Green_GetScrollRegion( doc, w, h, &max_x, &max_y );
					if (doc->rotation == 1)
						doc->yoffset = 0;
					else if (doc->rotation == 2)
						doc->xoffset = max_x;
					else if (doc->rotation == 3)
						doc->yoffset = max_x;
					else
						doc->xoffset = 0;
					
					Green_ValidateOffset( doc, w, h );
				}
			}
			
			return;
		}
		else if (abs_x > abs_y && x < 0 && left_border)
		{
			if (!(doc->bb&0xF0) && (doc->bb&0x03))
			{
				bb_mode = doc->bb&0x03;
				if (bb_mode == 1)
					bb_done = Green_GotoPage( doc, doc->page_cur - 1, false );
				else if (bb_mode == 2)
					bb_done = Green_GotoPage( doc, doc->page_cur + 1, false );
				
				if (bb_done)
				{
					Green_GetScrollRegion( doc, w, h, &max_x, &max_y );
					if (doc->rotation == 1)
						doc->yoffset = max_x;
					else if (doc->rotation == 2)
						doc->xoffset = 0;
					else if (doc->rotation == 3)
						doc->yoffset = 0;
					else
						doc->xoffset = max_x;
					
					Green_ValidateOffset( doc, w, h );
				}

			}
			
			return;
		}
		else if (abs_x < abs_y && y > 0 && bottom_border)
		{
			if (!(doc->bb&0xF0) && (doc->bb&0x0C))
			{
				bb_mode = (doc->bb>>2)&0x03;
				if (bb_mode == 1)
					bb_done = Green_GotoPage( doc, doc->page_cur + 1, false );
				else if (bb_mode == 2)
					bb_done = Green_GotoPage( doc, doc->page_cur - 1, false );
				
				if (bb_done)
				{
					Green_GetScrollRegion( doc, w, h, &max_x, &max_y );
					if (doc->rotation == 1)
						doc->xoffset = doc->mirrored ? max_y : 0;
					else if (doc->rotation == 2)
						doc->yoffset = doc->mirrored ? 0 : max_y;
					else if (doc->rotation == 3)
						doc->xoffset = doc->mirrored ? 0 : max_y;
					else
						doc->yoffset = doc->mirrored ? max_y : 0;
					
					Green_ValidateOffset( doc, w, h );
				}
			}
			
			return;
		}
		else if (abs_x < abs_y && y < 0 && top_border)
		{
			if (!(doc->bb&0xF0) && (doc->bb&0x0C))
			{
				bb_mode = (doc->bb>>2)&0x03;
				if (bb_mode == 1)
					bb_done = Green_GotoPage( doc, doc->page_cur - 1, false );
				else if (bb_mode == 2)
					bb_done = Green_GotoPage( doc, doc->page_cur + 1, false );
				
				if (bb_done)
				{
					Green_GetScrollRegion( doc, w, h, &max_x, &max_y );
					if (doc->rotation == 1)
						doc->xoffset = doc->mirrored ? 0 : max_y;
					else if (doc->rotation == 2)
						doc->yoffset = doc->mirrored ? max_y : 0;
					else if (doc->rotation == 3)
						doc->xoffset = doc->mirrored ? max_y : 0;
					else
						doc->yoffset = doc->mirrored ? 0 : max_y;
					
					Green_ValidateOffset( doc, w, h );
				}
			}
			
			return;
		}
	}
	
	doc->xoffset += doc_dx;
	doc->yoffset += doc_dy;
	Green_ValidateOffset( doc, w, h );
	return;
}

void	Green_Zoom( Green_Document *doc, int width, int height, double new_fs )
{
	double	old_tscale, new_tscale;
	int	old_w, old_h, new_w, new_h;
	
	old_tscale = Green_Fit(doc, width, height) * doc->finescale;
	doc->finescale = new_fs;
	new_tscale = Green_Fit(doc, width, height) * new_fs;
	Green_GetDimension( doc, doc->page_cur, &old_w, &old_h, old_tscale, doc->rotation % 2 );
	Green_GetDimension( doc, doc->page_cur, &new_w, &new_h, new_tscale, doc->rotation % 2 );
	
	if (doc->rotation % 2 == 0)
		doc->xoffset = doc->xoffset * new_tscale / old_tscale
			+ (new_tscale - old_tscale) * width / 2 / new_tscale;
	else
		doc->xoffset = doc->xoffset * new_tscale / old_tscale
			+ (new_tscale - old_tscale) * height / 2 / new_tscale;
	
	if (doc->rotation % 2 == 0)
		doc->yoffset = doc->yoffset * new_tscale / old_tscale
			+ (new_tscale - old_tscale) * height / 2 / new_tscale;
	else
		doc->yoffset = doc->yoffset * new_tscale / old_tscale
			+ (new_tscale - old_tscale) * width / 2 / new_tscale;
	
	Green_ValidateOffset( doc, width, height );
	return;
}

