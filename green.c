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

#define _GNU_SOURCE
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


/* Per-document state lives below the shared MuGreen2 data directory.
 * Two independent 64-bit hashes keep filenames short; the canonical URI is
 * stored inside each file and checked on load to detect hash collisions.
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

static int UsableDirectory( char *path )
{
	return EnsureDirectory( path ) == 0 && access( path, W_OK | X_OK ) == 0 ? 0 : -1;
}

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

/* Resolve one writable, shared application data directory. All settings and
 * per-document state use this directory so launchers have one override. */
static char *GetDataDirectory( void )
{
	static char directory[PATH_MAX];
	static bool resolved = false;
	static bool warned = false;
	const char *override, *xdg, *home;
	char candidate[PATH_MAX], executable[PATH_MAX];
	ssize_t length;
	char *slash;
	int n;

	if (resolved)
		return directory[0] ? directory : NULL;
	resolved = true;

	override = getenv( "MUGREEN2_DATA_DIR" );
	if (override && *override)
	{
		if (override[0] == '/')
			n = snprintf( candidate, sizeof( candidate ), "%s", override );
		else
		{
			char *cwd = getcwd( NULL, 0 );
			if (!cwd) n = -1;
			else
			{
				n = snprintf( candidate, sizeof( candidate ), "%s/%s", cwd, override );
				free( cwd );
			}
		}
		if (n >= 0 && n < (int)sizeof( candidate ) && UsableDirectory( candidate ) == 0)
			goto use_candidate;
		fprintf( stderr, "MuGreen2: warning: MUGREEN2_DATA_DIR '%s' is unusable; trying default data directories.\n", override );
	}

	xdg = getenv( "XDG_DATA_HOME" );
	if (xdg && xdg[0] == '/')
	{
		n = snprintf( candidate, sizeof( candidate ), "%s/mugreen2", xdg );
		if (n >= 0 && n < (int)sizeof( candidate ) && UsableDirectory( candidate ) == 0)
			goto use_candidate;
	}

	home = getenv( "HOME" );
	if (home && home[0] == '/')
	{
		n = snprintf( candidate, sizeof( candidate ), "%s/.local/share/mugreen2", home );
		if (n >= 0 && n < (int)sizeof( candidate ) && UsableDirectory( candidate ) == 0)
			goto use_candidate;
	}

	/* /proc/self/exe is available on the Linux handheld targets. The
	 * executable directory is deliberately the final, writable-if-possible
	 * fallback rather than an assumed writable system installation path. */
	length = readlink( "/proc/self/exe", executable, sizeof( executable ) - 1 );
	if (length > 0 && length < (ssize_t)sizeof( executable ))
	{
		executable[length] = 0;
		slash = strrchr( executable, '/' );
		if (slash)
		{
			*slash = 0;
			n = snprintf( candidate, sizeof( candidate ), "%s/.mugreen2-data", executable );
			if (n >= 0 && n < (int)sizeof( candidate ) && UsableDirectory( candidate ) == 0)
				goto use_candidate;
		}
	}

	if (!warned)
	{
		fprintf( stderr, "MuGreen2: warning: no writable data directory; reading state and settings will not be saved. Set MUGREEN2_DATA_DIR to choose a writable directory.\n" );
		warned = true;
	}
	return NULL;

use_candidate:
	strcpy( directory, candidate );
	return directory;
}

static const char *GetDocumentsDirectory( void )
{
	static char directory[PATH_MAX];
	static bool resolved = false;
	const char *base;
	int n;

	if (resolved)
		return directory[0] ? directory : NULL;
	resolved = true;
	base = GetDataDirectory();
	if (!base)
		return NULL;
	n = snprintf( directory, sizeof( directory ), "%s/documents", base );
	if (n < 0 || n >= (int)sizeof( directory ) || UsableDirectory( directory ))
	{
		directory[0] = 0;
		fprintf( stderr, "MuGreen2: warning: document-state directory is not writable; document state will not be saved.\n" );
		return NULL;
	}
	return directory;
}

static char *DocumentStatePath( const char *uri, char *path, size_t size )
{
	const char *directory = GetDocumentsDirectory();
	uint64_t h1, h2;
	int n;

	if (!uri || !*uri || !directory)
		return NULL;

	h1 = StateHash( uri, UINT64_C(14695981039346656037), UINT64_C(1099511628211) );
	h2 = StateHash( uri, UINT64_C(7809847782465536322), UINT64_C(14029467366897019727) );
	n = snprintf( path, size, "%s/%016llx%016llx.state", directory,
		(unsigned long long)h1, (unsigned long long)h2 );
	if (n < 0 || n >= (int)size)
		return NULL;
	return path;
}

#define GREEN_MIN_FINE_SCALE 0.05
#define GREEN_MAX_FINE_SCALE 20.0

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
	int *x, int *y, int *rotation, int *mirrored, int *mirror_vertical, int *fit, int version )
{
	char *p = line, *end;
	long values[7];
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
	for (i = 1; i < (version == 2 ? 7 : 6); i++)
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
	*mirror_vertical = version == 2 ? (int)values[5] : 0;
	*fit = version == 2 ? (int)values[6] : (int)values[5];
	return true;
}

static bool LoadDocumentState( Green_Document *doc )
{
	char path[PATH_MAX + 64], saved_uri[8192], line[512];
	FILE *file;
	int page, x, y, rotation, mirrored, mirror_vertical, fit, version;
	double scale;

	if (!DocumentStatePath( doc->uri, path, sizeof( path )))
		return false;
	file = fopen( path, "r" );
	if (!file)
		return false;
	if (!fgets( line, sizeof( line ), file )
		|| (strcmp( line, "MUGREEN2 1\n" ) && strcmp( line, "MUGREEN2 2\n" ))
		|| !fgets( saved_uri, sizeof( saved_uri ), file ))
	{
		fclose( file );
		return false;
	}
	version = line[8] - '0';
	/* A truncated URI must never be accepted as a different document. */
	if (!strchr( saved_uri, '\n' ) && !feof( file ))
	{
		fclose( file );
		return false;
	}
	saved_uri[strcspn( saved_uri, "\r\n" )] = 0;
	if (strcmp( saved_uri, doc->uri ) || !fgets( line, sizeof( line ), file )
		|| (!strchr( line, '\n' ) && !feof( file ))
		|| !ParseDocumentState( line, &page, &scale, &x, &y, &rotation, &mirrored, &mirror_vertical, &fit, version )
		|| fgetc( file ) != EOF || ferror( file ))
	{
		fclose( file );
		return false;
	}
	fclose( file );
	if (page < 1 || page > doc->page_count
		|| scale <= 0 || rotation < 0 || rotation > 3
		|| mirrored < 0 || mirrored > 1
		|| mirror_vertical < 0 || mirror_vertical > 1
		|| fit < NATURAL || fit > PAGE)
		return false;

	doc->page_cur = page - 1;
	doc->finescale = ClampFineScale( scale );
	doc->xoffset = x < 0 ? 0 : x;
	doc->yoffset = y < 0 ? 0 : y;
	doc->rotation = ((rotation % 4) + 4) % 4;
	doc->mirrored = mirrored != 0;
	doc->mirror_vertical = mirror_vertical != 0;
	doc->fit_method = (Green_FitMethod)fit;
	return true;
}

/* Write through a private temporary file, sync its contents, then atomically
 * replace the destination. Syncing the parent directory makes the rename
 * durable across a sudden power loss on filesystems that support it. */
static void AtomicWriteText( const char *path, const char *content )
{
	char temporary[PATH_MAX + 128], directory[PATH_MAX + 64], *slash;
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
	char path[PATH_MAX + 64], content[9000];
	double scale;
	if (!doc || !DocumentStatePath( doc->uri, path, sizeof( path )))
		return;
	scale = ClampFineScale( doc->finescale );
	if (snprintf( content, sizeof( content ),
		"MUGREEN2 2\n%s\n%d %.6f %d %d %d %d %d %d\n",
		doc->uri, doc->page_cur + 1, scale,
		doc->xoffset < 0 ? 0 : doc->xoffset,
		doc->yoffset < 0 ? 0 : doc->yoffset,
		((doc->rotation % 4) + 4) % 4,
		doc->mirrored ? 1 : 0,
		doc->mirror_vertical ? 1 : 0,
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

static char *FilenameToURI( const char *filename )
{
	char *canonical, *cwd = NULL, *absolute = NULL, *uri;
	size_t len;

	if (!filename || !*filename)
		return NULL;

	/* Canonical paths make equivalent launcher spellings share one state
	 * record. realpath requires the document to exist; retain the previous
	 * absolute-path fallback so failures remain representable. */
	canonical = realpath( filename, NULL );
	if (canonical)
	{
		uri = PathToURI( canonical );
		free( canonical );
		return uri;
	}

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

pb_open_result Green_Open( Green_RTD *rtd, const char *filename, char *message, size_t message_size )
{
	Green_Document	*doc;
	pb_open_result result;

	if (message && message_size)
		message[0] = 0;
	if (!rtd || !filename || !*filename || rtd->doc)
	{
		if (message && message_size)
			snprintf( message, message_size, "invalid document argument" );
		return PB_OPEN_INVALID_ARGUMENT;
	}
	doc = calloc( 1, sizeof( *doc ) );
	if (!doc)
	{
		if (message && message_size)
			snprintf( message, message_size, "out of memory allocating document" );
		return PB_OPEN_NO_MEMORY;
	}
	
	doc->uri = FilenameToURI( filename );
	
	if (!doc->uri)
	{
		if (message && message_size)
			snprintf( message, message_size, "could not resolve document path" );
		free( doc );
		return PB_OPEN_IO_ERROR;
	}
	
	result = pb_open( doc->uri, &doc->doc, message, message_size );
	if (result != PB_OPEN_OK)
	{
		free( doc->uri );
		free( doc );
		return result;
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
	/* Command-line reader rotation takes precedence over saved document state. */
	if (rtd->startup_rotation_set) {
		doc->rotation = ((rtd->startup_rotation % 4) + 4) % 4;
		doc->xoffset = 0;
		doc->yoffset = 0;
	}
	rtd->doc = doc;
	return PB_OPEN_OK;
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

void Green_LogMessage( const char *message )
{
	char path[PATH_MAX + 32];
	const char *directory = GetDataDirectory();
	FILE *file;

	if (!directory || !message || !*message
		|| snprintf( path, sizeof( path ), "%s/mugreen2.log", directory ) >= (int)sizeof( path ))
		return;
	file = fopen( path, "a" );
	if (!file)
		return;
	fprintf( file, "%s\n", message );
	fclose( file );
}

static char *SettingsPath( void )
{
	static char path[PATH_MAX + 32];
	const char *directory = GetDataDirectory();

	if (!directory || snprintf( path, sizeof( path ), "%s/settings", directory ) >= (int)sizeof( path ))
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
	bool side_by_side, spread_vertical;
	
	if (!rtd || !path)
		return;
	
	side_by_side = rtd->side_by_side != 0;
	spread_vertical = rtd->spread_vertical;
	file = fopen( path, "r" );
	if (!file)
		return;
	
	while (fgets( line, sizeof( line ), file ))
	{
		(void)ParseBooleanSetting( line, "night=", &rtd->night );
		(void)ParseBooleanSetting( line, "right_to_left=", &rtd->right_to_left );
		(void)ParseBooleanSetting( line, "side_by_side=", &side_by_side );
		(void)ParseBooleanSetting( line, "spread_vertical=", &spread_vertical );
	}
	
	fclose( file );
	rtd->side_by_side = side_by_side ? 1 : 0;
	rtd->spread_vertical = spread_vertical;
	return;
}

void	Green_SaveSettings( Green_RTD *rtd )
{
	char	*path = SettingsPath();
	char	content[96];
	
	if (!rtd || !path)
		return;
	
	snprintf( content, sizeof( content ), "night=%d\nright_to_left=%d\nside_by_side=%d\nspread_vertical=%d\n",
		rtd->night ? 1 : 0, rtd->right_to_left ? 1 : 0,
		rtd->side_by_side ? 1 : 0, rtd->spread_vertical ? 1 : 0 );
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
	
	if (!isfinite(pwidth) || !isfinite(pheight) || pwidth <= 0 || pheight <= 0)
		return 1;

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

