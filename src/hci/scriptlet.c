/*
 * Copyright (C) 2010 Myricom, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

FILE_LICENCE ( GPL2_OR_LATER );

#include <gpxe/dhcp.h>
#include <gpxe/scriptlet.h>
#include <gpxe/settings.h>
#include <stddef.h>
#include <stdlib.h>

/** Scriptlet setting */
struct setting scriptlet_setting __setting = {
	.name = "scriptlet",
	.description = "small boot script",
	.tag = DHCP_EB_SCRIPTLET,
	.type = &setting_type_string,
};
extern struct setting scriptlet_setting __setting;

/**
 * Find a "scriptlet" NonVolatile Option (NVO), if one has been set,
 * and execute it.  Script lines are separated by the "\n" escape
 * sequence.  Other escape sequences are translated from "\X" to "X".
 *
 * If multiple NICs with NVO support are present, it is possible to set
 * multiple scriptlets, but this routine will only execute the first one
 * found.
 */
void scriptlet_exec (void)
{
	char script[256];
	char *line, *from, *to;

	DBGP ( "scriptlet_exec\n" );

	/* Find a startup scriptlet, if one has been set. */
	if ( 0 >= fetch_setting ( NULL,
				  &scriptlet_setting,
				  script,
				  sizeof ( script ) ) ) {
		DBG2 ("No scriptlet\n");
		return;
	}

	/* Convert escape sequences, and execute each line of the script. */
	line = from = to = script;
	while ( *from ) {
		/* Handle escape sequences. */
		if ( *from == '\\' ) {
			from++;
			/* backslash at end of script is ignored. */
			if ( *from == 0 ) {
				break;
			}
			/* backslash-n translates as end-of-line. */
			if ( *from == 'n' ) {
				from++;
				*to++ = 0;
				DBG2 ( "> %s\n", line );
				system ( line );
				line = to;
				continue;
			}
		}
		*to++ = *from++;
	}
	/* Execute the last line of the script. */
	*to++ = 0;
	DBG2 ( "> %s\n", line );
	system ( line );
}
