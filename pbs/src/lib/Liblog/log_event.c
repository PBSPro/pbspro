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
 * log_event.c - contains functions to log event messages to the log file.
 *
 *	This is specific to the PBS Server.
 *
 * Functions included are:
 *	log_event()
 *	log_change()
 */


#include <pbs_config.h>   /* the master config generated by configure */

#include <sys/param.h>
#include <sys/types.h>
#include "pbs_ifl.h"
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include "log.h"
#include "server_limits.h"
#include "list_link.h"
#include "attribute.h"
#include "server.h"

/* private data */

static long  log_event_lvl_priv = PBSEVENT_ERROR    | PBSEVENT_SYSTEM   |
	PBSEVENT_ADMIN    | PBSEVENT_JOB      |
PBSEVENT_JOB_USAGE| PBSEVENT_SECURITY |
PBSEVENT_DEBUG    | PBSEVENT_DEBUG2	|
PBSEVENT_RESV;

/* external global data */

extern char *path_home;
long	    *log_event_mask = &log_event_lvl_priv;

/**
 * @brief
 * 	log_event - log a server event to the log file
 *
 *	Checks to see if the event type is being recorded.  If they are,
 *	pass off to log_record().
 *
 *	The caller should ensure proper formating of the message if "text"
 *	is to contain "continuation lines".
 *
 * @param[in] eventtype - event type
 * @param[in] objclass - event object class 
 * @param[in] sev - indication for whether to syslogging enabled or not
 * @param[in] objname - object name stating log msg related to which object
 * @param[in] text - log msg to be logged.
 *
 *	Note, "sev" or severity is used only if syslogging is enabled,
 *	see syslog(3) and log_record.c for details.
 */

void
log_event(int eventtype, int objclass, int sev, const char *objname, const char *text)
{
	if (((eventtype & PBSEVENT_FORCE) == 0) &&
		((*log_event_mask & eventtype) == 0))
		return;		/* not logging this type of event */
	else
		log_record(eventtype, objclass, sev, objname, text);
}
