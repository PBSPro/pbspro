/*
 * Copyright (C) 1994-2016 Altair Engineering, Inc.
 * For more information, contact Altair at www.altair.com.
 *  
 * This file is part of the PBS Professional ("PBS Pro") software.
 * 
 * Open Source License Information:
 *  
 * PBS Pro is free software. You can redistribute it and/or modify it under the
 * terms of the GNU Affero General Public License as published by the Free 
 * Software Foundation, either version 3 of the License, or (at your option) any 
 * later version.
 *  
 * PBS Pro is distributed in the hope that it will be useful, but WITHOUT ANY 
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
 * PARTICULAR PURPOSE.  See the GNU Affero General Public License for more details.
 *  
 * You should have received a copy of the GNU Affero General Public License along 
 * with this program.  If not, see <http://www.gnu.org/licenses/>.
 *  
 * Commercial License Information: 
 * 
 * The PBS Pro software is licensed under the terms of the GNU Affero General 
 * Public License agreement ("AGPL"), except where a separate commercial license 
 * agreement for PBS Pro version 14 or later has been executed in writing with Altair.
 *  
 * Altair’s dual-license business model allows companies, individuals, and 
 * organizations to create proprietary derivative works of PBS Pro and distribute 
 * them - whether embedded or bundled with other software - under a commercial 
 * license agreement.
 * 
 * Use of Altair’s trademarks, including but not limited to "PBS™", 
 * "PBS Professional®", and "PBS Pro™" and Altair’s logos is subject to Altair's 
 * trademark licensing policies.
 *
 */

/*
 * These were moved from qsub so that AIF could access them.
 */
#include <ctype.h>
#include <string.h>

#include "cmds.h"
#include "libpbs.h"


/**
 * @brief
 *	check whether the script content in buf s executable or not
 *
 * @param[in] s - buf with script content(job)
 *
 * @return	int
 * @retval	TRUE	executable
 * @retval	FALSE	not executable
 *	
 */
int
pbs_isexecutable(char *s)
{
	char *c;

	c = s;
	if ((*c == ':') || ((*c == '#') && (*(c+1) == '!'))) return FALSE;
	while (isspace(*c)) c++;
	if (notNULL(c)) return (*c != '#');
	return FALSE;
}

/**
 * @brief
 *	returns the pbs directive
 *
 * @param[in] s - copy of script file
 * @param[in] prefix - prefix for pbs directives
 *
 * @return	string
 * @retval	NULL		error
 *
 */
char *
pbs_ispbsdir(char *s, char *prefix)
{
	char *it;
	int l;

	it = s;
	while (isspace(*it)) it++;
	l = strlen(prefix);
	if (l > 0 && strncmp(it, prefix, l) == 0)
		return (it+l);
	else
		return ((char *)NULL);
}

