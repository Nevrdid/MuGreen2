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
#include "green.h"

#ifndef MUGREEN2_VERSION
#define MUGREEN2_VERSION "unknown"
#endif


const char	*help_text =
"Usage:\n"
"    mugreen2 [<options>] <PDF file>\n"
"\n"
"The following options are available:\n"
"    -fit=<mode>                 initial fit: none, width, height or page\n"
"    -step=<fraction>            scroll step as a fraction of the viewport (default: 0.9)\n"
"    -zoomstep=<fraction>        zoom increment as a fraction (default: 1/5)\n"
"    -rotation=<degrees>         initial reader rotation: 0, 90, 180 or 270\n"
"    -display-rotation=<degrees> rotate the complete display: 0, 90, 180 or 270\n"
"    -rtl                        start in right-to-left reading order\n"
"    -ltr                        start in left-to-right reading order\n"
"    -controller-layout=<mode>   face buttons: nintendo (default) or xbox\n"
"    -no-control-rotation        keep controls fixed when rotating the reader\n"
"    -fullscreen                 to startup in fullscreen mode\n"
"    -no-fullscreen              to startup in window mode\n"
"    -width=<width>              to specify the window width (in pixels)\n"
"    -height=<height>            to specify the window height (in pixels)\n"
"    -cache=<MB>                 MuPDF store size in MiB (default: 32)\n"
"    -help                       shows this help\n"
"    -version                    displays version information\n"
"\n"
"Further help is available online:\n"
"    https://github.com/Nevrdid/MuGreen2\n";


void	PrintHelp()
{
	puts( help_text );
	return;
}

void	PrintVersion()
{
	printf( "MuGreen2 - the PDF reader\nversion: %s\nlicense: GNU AGPL-3.0-only\nproject: https://github.com/Nevrdid/MuGreen2\n", MUGREEN2_VERSION );
	return;
}

double	ReadFraction( char *str )
{
	double	res;
	long	tmp;
	char	*str2;
	
	res = strtol( str, &str2, 10 );
	if (!*str2)
		return res;
	
	if (*str2 != '/')
		return 0;
	
	str2++;
	tmp = strtol( str2, &str2, 10 );
	if (!tmp || *str2)
		return 0;
	
	res /= tmp;
	return res;
}

/* Parse a window dimension without allowing strtol overflow or a
 * narrowing conversion to wrap. Zero retains SDL's automatic-size meaning. */
static int ReadDimension( const char *str, unsigned short *value )
{
	unsigned int parsed = 0;
	const unsigned char *p = (const unsigned char *)str;

	if (!p || !*p || !value)
		return 0;
	for (; *p; p++)
	{
		unsigned int digit;
		if (*p < '0' || *p > '9')
			return 0;
		digit = *p - '0';
		if (parsed > (65535u - digit) / 10u)
			return 0;
		parsed = parsed * 10u + digit;
	}
	*value = (unsigned short)parsed;
	return 1;
}

int	main( int argc, char *argv[] )
{
	Green_RTD	rtd;
	char	*opt;
	int i, err = 0, pdf_count = 0;
	size_t mupdf_store_bytes = 32u << 20;
	char *pdf_file = NULL;
	
	rtd.flags = 0;
	rtd.width = 0;
	rtd.height = 0;	
	rtd.doc = NULL;
	rtd.c_background.r = 0x30;
	rtd.c_background.g = 0xD0;
	rtd.c_background.b = 0x30;
	rtd.c_background.a = 0xFF;
	rtd.fit_method = WIDTH;
	rtd.step = 0.9;
	rtd.zoomstep = 1.2;	/* ~20% per zoom click */
	rtd.bb = 0x04;
	rtd.side_by_side = 0;
	rtd.spread_vertical = false;
	rtd.xbox_layout = false;
	rtd.controls_rotate = true;
	rtd.night = false;
	rtd.right_to_left = false;
	rtd.startup_rotation = 0;
	rtd.startup_rotation_set = false;
	rtd.display_rotation = 0;
	Green_LoadSettings( &rtd );
	
	for (i = 1; i < argc; i++)
	{
		if (argv[i][0] != '-')
			continue;
		
		opt = argv[i][1] == '-' ? &argv[i][2] : &argv[i][1];
		if (!strcmp( opt, "h" ) || !strcmp( opt, "help" ))
		{
			PrintHelp();
			return 0;
		}
		else if (!strcmp( opt, "v" ) || !strcmp( opt, "version" ))
		{
			PrintVersion();
			return 0;
		}
		else if (!strncmp( opt, "fit=", 4 ))
		{
			opt += 4;
			if (!strcmp( opt, "width" ))
				rtd.fit_method = WIDTH;
			else if (!strcmp( opt, "height" ))
				rtd.fit_method = HEIGHT;
			else if (!strcmp( opt, "page" ))
				rtd.fit_method = PAGE;
			else if (!strcmp( opt, "none" ))
				rtd.fit_method = NATURAL;
			else
				err = -1;
		}
		else if (!strncmp( opt, "rotation=", 9 ))
		{
			opt += 9;
			if (!strcmp( opt, "0" ))
				rtd.startup_rotation = 0;
			else if (!strcmp( opt, "90" ))
				rtd.startup_rotation = 1;
			else if (!strcmp( opt, "180" ))
				rtd.startup_rotation = 2;
			else if (!strcmp( opt, "270" ))
				rtd.startup_rotation = 3;
			else
				err = -1;
			if (!err)
				rtd.startup_rotation_set = true;
		}
		else if (!strncmp( opt, "display-rotation=", 17 ))
		{
			opt += 17;
			if (!strcmp( opt, "0" ))
				rtd.display_rotation = 0;
			else if (!strcmp( opt, "90" ))
				rtd.display_rotation = 1;
			else if (!strcmp( opt, "180" ))
				rtd.display_rotation = 2;
			else if (!strcmp( opt, "270" ))
				rtd.display_rotation = 3;
			else
				err = -1;
		}
		else if (!strcmp( opt, "rtl" ))
			rtd.right_to_left = true;
		else if (!strcmp( opt, "ltr" ))
			rtd.right_to_left = false;
		else if (!strcmp( opt, "no-control-rotation" ))
			rtd.controls_rotate = false;
		else if (!strncmp( opt, "controller-layout=", 18 ))
		{
			opt += 18;
			if (!strcmp( opt, "nintendo" ))
				rtd.xbox_layout = false;
			else if (!strcmp( opt, "xbox" ))
				rtd.xbox_layout = true;
			else
				err = -1;
		}
		else if (!strncmp( opt, "step=", 5 ))
		{
			opt += 5;
			rtd.step = ReadFraction( opt );
			if (rtd.step <= 0 || rtd.step > 1)
				err = -1;
		}
		else if (!strncmp( opt, "zoomstep=", 9 ))
		{
			opt += 9;
			rtd.zoomstep = ReadFraction( opt );
			if (rtd.zoomstep <= 0)
				err = -1;
			
			rtd.zoomstep += 1;
		}
		else if (!strncmp( opt, "width=", 6 ))
		{
			opt += 6;
			if (!ReadDimension( opt, &rtd.width ))
				err = -1;
		}
		else if (!strncmp( opt, "height=", 7 ))
		{
			opt += 7;
			if (!ReadDimension( opt, &rtd.height ))
				err = -1;
		}
		else if (!strncmp( opt, "cache=", 6 ))
		{
			char *end;
			unsigned long mb;
			opt += 6;
			mb = strtoul( opt, &end, 10 );
			if (*end || mb < 1 || mb > 1024)
				err = -1;
			else
				mupdf_store_bytes = (size_t)mb << 20;
		}
		else if (!strcmp( opt, "fullscreen" ))
			rtd.flags |= GREEN_FULLSCREEN;
		else if (!strcmp( opt, "no-fullscreen" ))
			rtd.flags &= ~GREEN_FULLSCREEN;
		else
			err = -1;
		
		if (err)
		{
			fprintf( stderr, "MuGreen2: invalid option or value: %s\n", argv[i] );
			PrintHelp();
			return GREEN_EXIT_USAGE;
		}
	}
	
	for (i = 1; i < argc; i++)
		if (argv[i][0] != '-')
		{
			pdf_file = argv[i];
			pdf_count++;
		}

	if (pdf_count != 1)
	{
		fprintf( stderr, "MuGreen2: please provide exactly one PDF file.\n\n" );
		PrintHelp();
		return GREEN_EXIT_USAGE;
	}
	pb_configure( mupdf_store_bytes, -1 );
	{
		char open_error[512];
		pb_open_result result = Green_Open( &rtd, pdf_file, open_error, sizeof( open_error ) );
		if (result != PB_OPEN_OK)
		{
			int exit_code;
			switch (result)
			{
				case PB_OPEN_NOT_FOUND: exit_code = GREEN_EXIT_FILE_NOT_FOUND; break;
				case PB_OPEN_NOT_READABLE: exit_code = GREEN_EXIT_FILE_UNREADABLE; break;
				case PB_OPEN_INVALID_DOCUMENT:
				case PB_OPEN_NOT_REGULAR: exit_code = GREEN_EXIT_INVALID_DOCUMENT; break;
				case PB_OPEN_PASSWORD_REQUIRED: exit_code = GREEN_EXIT_PASSWORD_REQUIRED; break;
				case PB_OPEN_INVALID_ARGUMENT:
				case PB_OPEN_NO_MEMORY:
				case PB_OPEN_BACKEND_ERROR:
				case PB_OPEN_IO_ERROR:
				default: exit_code = GREEN_EXIT_OPEN_ERROR; break;
			}
			{
				char log_message[1024];
				snprintf( log_message, sizeof( log_message ), "Cannot open '%s': %s",
					pdf_file, open_error[0] ? open_error : "unknown error" );
				Green_LogMessage( log_message );
				fprintf( stderr, "MuGreen2: %s\n", log_message );
			}
			return exit_code;
		}
	}

	return Green_SDL_Main( &rtd );
}
