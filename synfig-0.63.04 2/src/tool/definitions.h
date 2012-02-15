/* === S Y N F I G ========================================================= */
/*!	\file tool/definitions.h
**	\brief Definitions for synfig tool
**
**	$Id$
**
**	\legal
**	Copyright (c) 2002-2005 Robert B. Quattlebaum Jr., Adrian Bentley
**	Copyright (c) 2007, 2008 Chris Moore
**
**	This package is free software; you can redistribute it and/or
**	modify it under the terms of the GNU General Public License as
**	published by the Free Software Foundation; either version 2 of
**	the License, or (at your option) any later version.
**
**	This package is distributed in the hope that it will be useful,
**	but WITHOUT ANY WARRANTY; without even the implied warranty of
**	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
**	General Public License for more details.
**	\endlegal
*/
/* ========================================================================= */

#ifndef __SYNFIG_DEFINITIONS_H
#define __SYNFIG_DEFINITIONS_H

/* === M A C R O S ========================================================= */

#ifdef ENABLE_NLS
#undef _
#define _(x) gettext(x)
#else
#undef _
#define _(x) (x)
#endif

enum exit_code
{
	SYNFIGTOOL_OK				= 0,
	SYNFIGTOOL_FILENOTFOUND		= 1,
	SYNFIGTOOL_BORED			= 2,
	SYNFIGTOOL_HELP				= 3,
	SYNFIGTOOL_UNKNOWNARGUMENT	= 4,
	SYNFIGTOOL_UNKNOWNERROR		= 5,
	SYNFIGTOOL_INVALIDTARGET	= 6,
	SYNFIGTOOL_RENDERFAILURE	= 7,
	SYNFIGTOOL_BLANK			= 8,
	SYNFIGTOOL_BADVERSION		= 9,
	SYNFIGTOOL_MISSINGARGUMENT	=10
};

#ifndef VERSION
#define VERSION "unknown"
#define PACKAGE "synfig-tool"
#endif

#ifdef DEFAULT_QUALITY
#undef DEFAULT_QUALITY
#endif

#define DEFAULT_QUALITY		2
#define VERBOSE_OUT(x) if(verbosity>=(x))std::cerr

/* === G L O B A L S ======================================================= */

extern const char *progname;
extern int verbosity;
extern bool be_quiet;
extern bool print_benchmarks;

#endif
