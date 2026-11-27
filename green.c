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

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <limits.h>
#include <math.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h>
#include "green.h"


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

#define GREEN_MIN_FINE_SCALE 0.05
#define GREEN_MAX_FINE_SCALE 20.0

static int URIHexValue( char c )
{
	if (c >= '0' && c <= '9') return c - '0';
	if (c >= 'a' && c <= 'f') return c - 'a' + 10;
	if (c >= 'A' && c <= 'F') return c - 'A' + 10;
	return -1;
}

/* Recover the pre-percent-encoding URI so existing state files for paths
 * containing spaces, percent signs, or non-ASCII bytes remain discoverable. */
static char *LegacyUnescapedURI( const char *uri )
{
	size_t i, n = strlen( uri ), out = 0;
	char *legacy = malloc( n + 1 );
	if (!legacy)
		return NULL;
	for (i = 0; i < n; i++)
	{
		if (uri[i] == '%' && i + 2 < n)
		{
			int hi = URIHexValue( uri[i + 1] );
			int lo = URIHexValue( uri[i + 2] );
			if (hi >= 0 && lo >= 0)
			{
				legacy[out++] = (char)((hi << 4) | lo);
				i += 2;
				continue;
			}
		}
		legacy[out++] = uri[i];
	}
	legacy[out] = 0;
	return legacy;
}

static double ClampFineScale( double scale )
{
	if (!isfinite( scale ) || scale < GREEN_MIN_FINE_SCALE)
		return GREEN_MIN_FINE_SCALE;
	if (scale > GREEN_MAX_FINE_SCALE)
		return GREEN_MAX_FINE_SCALE;
	return scale;
}

static char *SkipSpace( char *p )
{
	while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')
		p++;
	return p;
}

/* Parse the complete state record without scanf's undefined behaviour for
 * out-of-range integer conversions, and reject missing or trailing fields. */
static bool ParseDocumentState( char *line, int *page, double *scale,
	int *x, int *y, int *rotation, int *mirrored, int *fit )
{
	char *p = line, *end;
	long values[6];
	double d;
	int i;

	p = SkipSpace( p );
	errno = 0;
	values[0] = strtol( p, &end, 10 );
	if (end == p || errno == ERANGE || values[0] < INT_MIN || values[0] > INT_MAX)
		return false;
	p = SkipSpace( end );
	errno = 0;
	d = strtod( p, &end );
	if (end == p || errno == ERANGE || !isfinite( d ))
		return false;
	p = end;
	for (i = 1; i < 6; i++)
	{
		p = SkipSpace( p );
		errno = 0;
		values[i] = strtol( p, &end, 10 );
		if (end == p || errno == ERANGE || values[i] < INT_MIN || values[i] > INT_MAX)
			return false;
		p = end;
	}
	p = SkipSpace( p );
	if (*p)
		return false;
	*page = (int)values[0];
	*scale = d;
	*x = (int)values[1];
	*y = (int)values[2];
	*rotation = (int)values[3];
	*mirrored = (int)values[4];
	*fit = (int)values[5];
	return true;
}

static bool LoadDocumentState( Green_Document *doc )
{
	char path[1200], legacy_path[1200], saved_uri[8192], line[512];
	char *legacy_uri = NULL;
	const char *expected_uri = doc->uri;
	FILE *file;
	int page, x, y, rotation, mirrored, fit;
	double scale;

	if (!DocumentStatePath( doc->uri, path, sizeof( path )))
		return false;
	file = fopen( path, "r" );
	if (!file)
	{
		/* Older versions hashed the unescaped filename URI. Try that key
		 * before giving up, then the next save naturally migrates the file. */
		legacy_uri = LegacyUnescapedURI( doc->uri );
		if (legacy_uri && strcmp( legacy_uri, doc->uri )
			&& DocumentStatePath( legacy_uri, legacy_path, sizeof( legacy_path )))
		{
			file = fopen( legacy_path, "r" );
			if (file)
				expected_uri = legacy_uri;
		}
	}
	if (!file)
	{
		free( legacy_uri );
		return false;
	}
	if (!fgets( line, sizeof( line ), file ) || strcmp( line, "MUGREEN2 1\n" )
		|| !fgets( saved_uri, sizeof( saved_uri ), file ))
	{
		fclose( file );
		free( legacy_uri );
		return false;
	}
	/* A truncated URI must never be accepted as a different document. */
	if (!strchr( saved_uri, '\n' ) && !feof( file ))
	{
		fclose( file );
		free( legacy_uri );
		return false;
	}
	saved_uri[strcspn( saved_uri, "\r\n" )] = 0;
	if (strcmp( saved_uri, expected_uri ) || !fgets( line, sizeof( line ), file )
		|| (!strchr( line, '\n' ) && !feof( file ))
		|| !ParseDocumentState( line, &page, &scale, &x, &y, &rotation, &mirrored, &fit )
		|| fgetc( file ) != EOF || ferror( file ))
	{
		fclose( file );
		free( legacy_uri );
		return false;
	}
	fclose( file );
	free( legacy_uri );
	if (page < 1 || page > doc->page_count
		|| scale <= 0 || rotation < 0 || rotation > 3
		|| mirrored < 0 || mirrored > 1
		|| fit < NATURAL || fit > PAGE)
		return false;

	doc->page_cur = page - 1;
	doc->finescale = ClampFineScale( scale );
	doc->xoffset = x < 0 ? 0 : x;
	doc->yoffset = y < 0 ? 0 : y;
	doc->rotation = ((rotation % 4) + 4) % 4;
	doc->mirrored = mirrored != 0;
	doc->fit_method = (Green_FitMethod)fit;
	return true;
}

/* Write through a private temporary file, sync its contents, then atomically
 * replace the destination. Syncing the parent directory makes the rename
 * durable across a sudden power loss on filesystems that support it. */
static void AtomicWriteText( const char *path, const char *content )
{
	char temporary[1240], directory[1200], *slash;
	int fd, dirfd;
	FILE *file;
	size_t length;

	if (!path || !content
		|| snprintf( temporary, sizeof( temporary ), "%s.tmp.XXXXXX", path ) >= (int)sizeof( temporary ))
		return;
	fd = mkstemp( temporary );
	if (fd < 0)
		return;
	file = fdopen( fd, "w" );
	if (!file)
	{
		close( fd );
		unlink( temporary );
		return;
	}
	length = strlen( content );
	if (fwrite( content, 1, length, file ) != length
		|| fflush( file ) || fsync( fileno( file )))
	{
		fclose( file );
		unlink( temporary );
		return;
	}
	if (fclose( file ))
	{
		unlink( temporary );
		return;
	}
	if (rename( temporary, path ))
	{
		unlink( temporary );
		return;
	}

	if (snprintf( directory, sizeof( directory ), "%s", path ) >= (int)sizeof( directory ))
		return;
	slash = strrchr( directory, '/' );
	if (!slash)
		strcpy( directory, "." );
	else if (slash == directory)
		slash[1] = 0;
	else
		*slash = 0;
	dirfd = open( directory, O_RDONLY
#ifdef O_DIRECTORY
		| O_DIRECTORY
#endif
	);
	if (dirfd >= 0)
	{
		(void)fsync( dirfd );
		close( dirfd );
	}
}

static void SaveDocumentState( Green_Document *doc )
{
	char path[1200], content[9000];
	double scale;
	if (!doc || !DocumentStatePath( doc->uri, path, sizeof( path )))
		return;
	scale = ClampFineScale( doc->finescale );
	if (snprintf( content, sizeof( content ),
		"MUGREEN2 1\n%s\n%d %.6f %d %d %d %d %d\n",
		doc->uri, doc->page_cur + 1, scale,
		doc->xoffset < 0 ? 0 : doc->xoffset,
		doc->yoffset < 0 ? 0 : doc->yoffset,
		((doc->rotation % 4) + 4) % 4,
		doc->mirrored ? 1 : 0,
		doc->fit_method >= NATURAL && doc->fit_method <= PAGE ? (int)doc->fit_method : WIDTH ) >= (int)sizeof( content ))
		return;
	AtomicWriteText( path, content );
}

void Green_SaveDocumentState( Green_Document *doc )
{
	SaveDocumentState( doc );
}

static void	RestoreLastPage( Green_Document *doc )
{
	if (!doc || !doc->uri)
		return;
	LoadDocumentState( doc );
	return;
}

static char *PathToURI( const char *path )
{
	static const char hex[] = "0123456789ABCDEF";
	size_t i, n = 0, len = strlen( path );
	char *uri;
	if (len > (SIZE_MAX - 6) / 3)
		return NULL;
	uri = malloc( 5 + len * 3 + 1 );
	if (!uri)
		return NULL;
	memcpy( uri, "file:", 5 );
	for (i = 0; i < len; i++)
	{
		unsigned char c = (unsigned char)path[i];
		/* Keep URI path separators and unreserved bytes; escape delimiters,
		 * percent signs, spaces, and non-ASCII bytes. */
		if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
			|| (c >= '0' && c <= '9') || c == '-' || c == '.' || c == '_'
			|| c == '~' || c == '/')
			uri[5 + n++] = (char)c;
		else
		{
			uri[5 + n++] = '%';
			uri[5 + n++] = hex[c >> 4];
			uri[5 + n++] = hex[c & 15];
		}
	}
	uri[5 + n] = 0;
	return uri;
}

char* FilenameToURI( char *filename )
{
	char *cwd = NULL, *absolute = NULL, *uri;
	size_t len;
	if (!filename || !*filename)
		return NULL;
	if (filename[0] == '/')
		return PathToURI( filename );
	cwd = getcwd( NULL, 0 );
	if (!cwd)
		return NULL;
	len = strlen( cwd ) + strlen( filename ) + 2;
	absolute = malloc( len );
	if (absolute)
		snprintf( absolute, len, "%s/%s", cwd, filename );
	free( cwd );
	if (!absolute)
		return NULL;
	uri = PathToURI( absolute );
	free( absolute );
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
	SaveDocumentState( doc );
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
		SaveDocumentState( rtd->doc );
	
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

static bool ParseBooleanSetting( const char *line, const char *key, bool *value )
{
	size_t n = strlen( key );
	if (strncmp( line, key, n ))
		return false;
	if ((line[n] == '0' || line[n] == '1')
		&& (line[n + 1] == '\n' || line[n + 1] == 0
			|| (line[n + 1] == '\r' && (line[n + 2] == '\n' || line[n + 2] == 0))))
	{
		*value = line[n] == '1';
		return true;
	}
	return false;
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
		(void)ParseBooleanSetting( line, "night=", &rtd->night );
		(void)ParseBooleanSetting( line, "right_to_left=", &rtd->right_to_left );
	}
	
	fclose( file );
	return;
}

void	Green_SaveSettings( Green_RTD *rtd )
{
	char	*path = SettingsPath();
	char	content[64];
	
	if (!rtd || !path)
		return;
	
	snprintf( content, sizeof( content ), "night=%d\nright_to_left=%d\n",
		rtd->night ? 1 : 0, rtd->right_to_left ? 1 : 0 );
	AtomicWriteText( path, content );
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
	new_fs = ClampFineScale( new_fs );
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

