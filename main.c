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
#include "green.h"


const char	*help_text =
"Usage:\n"
"    mugreen2 [<options>] <PDF file>\n"
"\n"
"The following options are available:\n"
"    -fit=<mode>                 initial fit: none, width, height or page\n"
"    -step=<fraction>            scroll step as a fraction of the viewport\n"
"    -zoomstep=<fraction>        zoom increment as a fraction (default: 1/5)\n"
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
	printf( "MuGreen2 - the PDF reader\nversion: %s\nlicense: GNU AGPL version 3\n", "0.2" );
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
	rtd.step = 1;
	rtd.zoomstep = 1.2;	/* ~20% per zoom click */
	rtd.bb = 0x04;
	rtd.side_by_side = 0;
	rtd.night = false;
	rtd.right_to_left = false;
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
			rtd.width = strtol( opt, &opt, 10 );
			if (*opt)
				err = -1;
		}
		else if (!strncmp( opt, "height=", 7 ))
		{
			opt += 7;
			rtd.height = strtol( opt, &opt, 10 );
			if (*opt)
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
			fprintf( stderr, "Unknown option!\n" );
			PrintHelp();
			return err;
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
		fprintf( stderr, "Please provide exactly one PDF file.\n\n" );
		PrintHelp();
		return 1;
	}
	pb_configure( mupdf_store_bytes, -1 );
	if (Green_Open( &rtd, pdf_file ) < 0)
	{
		fprintf( stderr, "Failed to open: %s\n", pdf_file );
		return 1;
	}

	return Green_SDL_Main( &rtd );
}
