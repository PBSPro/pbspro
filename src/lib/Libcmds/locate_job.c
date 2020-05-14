/*
 * Copyright (C) 1994-2020 Altair Engineering, Inc.
 * For more information, contact Altair at www.altair.com.
 *
 * This file is part of both the OpenPBS software ("OpenPBS")
 * and the PBS Professional ("PBS Pro") software.
 *
 * Open Source License Information:
 *
 * OpenPBS is free software. You can redistribute it and/or modify it under
 * the terms of the GNU Affero General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or (at your
 * option) any later version.
 *
 * OpenPBS is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Affero General Public
 * License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Commercial License Information:
 *
 * PBS Pro is commercially licensed software that shares a common core with
 * the OpenPBS software.  For a copy of the commercial license terms and
 * conditions, go to: (http://www.pbspro.com/agreement.html) or contact the
 * Altair Legal Department.
 *
 * Altair's dual-license business model allows companies, individuals, and
 * organizations to create proprietary derivative works of OpenPBS and
 * distribute them - whether embedded or bundled with other software -
 * under a commercial license agreement.
 *
 * Use of Altair's trademarks, including but not limited to "PBS™",
 * "OpenPBS®", "PBS Professional®", and "PBS Pro™" and Altair's logos is
 * subject to Altair's trademark licensing policies.
 */

/**
 * @file	locate_job.c
 * @brief
 *	Connect to the server the job was submitted to, and issue a
 *  Locate Job command. The result should be the server that the job
 *  is currently at.
 */

#include <pbs_config.h>   /* the master config generated by configure */

#include "cmds.h"
#include "pbs_ifl.h"


/**
 * @brief
 *	returns the location of job running at.
 *
 * @param[in] job_id - job id
 * @param[in] parent_server - server name
 * @param[out] located_server - server name
 *
 * @return	int
 * @retval	TRUE	success
 * @retval	-1	error
 *
 */

int
locate_job(job_id, parent_server, located_server)
char *job_id;
char *parent_server;
char *located_server;
{
	int connect;
	char jid_server[PBS_MAXCLTJOBID+1];
	char *location;

	if ((connect = pbs_connect(parent_server)) > 0) {
		strcpy(jid_server, job_id);
		if (notNULL(parent_server)) {
			strcat(jid_server, "@");
			strcat(jid_server, parent_server);
		}
		location = pbs_locjob(connect, jid_server, NULL);
		if (location == NULL) {
			pbs_disconnect(connect);
			return FALSE;
		}
		strcpy(located_server, location);
		free(location);
		pbs_disconnect(connect);
		return TRUE;
	} else
		return -1;
}
