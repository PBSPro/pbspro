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
 * contains functions related to receive command sent by the Server
 */

#include <pbs_config.h>   /* the master config generated by configure */
#include <stdlib.h>
#include "dis.h"
#include "sched_cmds.h"
#include "data_types.h"
#include "fifo.h"

#include <sys/types.h>
#include <sys/time.h>
#include <unistd.h>

#if defined(FD_SET_IN_SYS_SELECT_H)
#include <sys/select.h>
#endif

/**
 * @brief Gets the Scheduler Command sent by the Server
 *
 * @param[in]     sock - secondary connection to the server
 * @param[in,out] cmd  - pointer to sched cmd to be filled with received cmd
 *
 * @return	int
 * @retval	0	: for EOF
 * @retval	+1	: for success
 * @retval	-1	: for error
 */
int
get_sched_cmd(int sock, sched_cmd *cmd)
{
	int i;
	int rc = 0;
	char *jobid = NULL;

	i = disrsi(sock, &rc);
	if (rc != 0)
		goto err;
	if (i == SCH_SCHEDULE_AJOB) {
		jobid = disrst(sock, &rc);
		if (rc != 0)
			goto err;
	}

	cmd->cmd = i;
	cmd->jid = jobid;
	cmd->from_sock = sock;
	return 1;

err:
	if (rc == DIS_EOF)
		return 0;
	else
		return -1;
}

/**
 * @brief This is non-blocking version of get_sched_cmd()
 *
 * @param[in]     sock - secondary connection to the server
 * @param[in,out] cmd  - pointer to sched cmd to be filled with received cmd
 *
 * @return	int
 * @retval	0	no command to read
 * @retval	+1	for success
 * @retval	-1	for error
 * @retval	-2	for EOF
 *
 * @note this function uses different return code (-2) for EOF than get_sched_cmd() (which uses -1)
 */
int
get_sched_cmd_noblk(int sock, sched_cmd *cmd)
{
	struct timeval timeout;
	fd_set fdset;
	timeout.tv_usec = 0;
	timeout.tv_sec = 0;

	FD_ZERO(&fdset);
	FD_SET(sock, &fdset);

	if (select(FD_SETSIZE, &fdset, NULL, NULL, &timeout) != -1 && FD_ISSET(sock, &fdset)) {
		int rc = get_sched_cmd(sock, cmd);
		if (rc == 0)
			return -2;
		return rc;
	}
	return 0;
}
