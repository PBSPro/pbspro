/*
 * Copyright (C) 1994-2020 Altair Engineering, Inc.
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
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.
 * See the GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Commercial License Information:
 *
 * For a copy of the commercial license terms and conditions,
 * go to: (http://www.pbspro.com/UserArea/agreement.html)
 * or contact the Altair Legal Department.
 *
 * Altair’s dual-license business model allows companies, individuals, and
 * organizations to create proprietary derivative works of PBS Pro and
 * distribute them - whether embedded or bundled with other software -
 * under a commercial license agreement.
 *
 * Use of Altair’s trademarks, including but not limited to "PBS™",
 * "PBS Professional®", and "PBS Pro™" and Altair’s logos is subject to Altair's
 * trademark licensing policies.
 *
 */
/**
 * @file	pbs_msgjob.c
 * @brief
 *	send the MessageJob request and get the reply.
 */

#include <pbs_config.h>   /* the master config generated by configure */

#include <string.h>
#include <stdio.h>
#include "libpbs.h"
#include "dis.h"
#include "pbs_ecl.h"


/**
 * @brief
 *	-send the MessageJob request and get the reply.
 *
 * @param[in] c - socket descriptor
 * @param[in] jobid - job id
 * @param[in] fileopt - which file
 * @param[in] msg - msg to be encoded
 * @param[in] extend - extend string for encoding req
 *
 * @return	int
 * @retval	0	success
 * @retval	!0	error
 *
 */

int
__pbs_msgjob(int c, char *jobid, int fileopt, char *msg, char *extend)
{
	struct batch_reply *reply;
	int	rc;

	if ((jobid == NULL) || (*jobid == '\0') ||
		(msg == NULL) || (*msg == '\0'))
		return (pbs_errno = PBSE_IVALREQ);

	/* initialize the thread context data, if not already initialized */
	if (pbs_client_thread_init_thread_context() != 0)
		return pbs_errno;

	/* lock pthread mutex here for this connection */
	/* blocking call, waits for mutex release */
	if (pbs_client_thread_lock_connection(c) != 0)
		return pbs_errno;

	/* setup DIS support routines for following DIS calls */

	DIS_tcp_setup(connection[c].ch_socket);

	if ((rc = PBSD_msg_put(c, jobid, fileopt, msg, extend, 0, NULL)) != 0) {
		connection[c].ch_errtxt = strdup(dis_emsg[rc]);
		if (connection[c].ch_errtxt == NULL) {
			pbs_errno = PBSE_SYSTEM;
		} else {
			pbs_errno = PBSE_PROTOCOL;
		}
		(void)pbs_client_thread_unlock_connection(c);
		return pbs_errno;
	}

	/* read reply */

	reply = PBSD_rdrpy(c);
	rc = connection[c].ch_errno;

	PBSD_FreeReply(reply);

	/* unlock the thread lock and update the thread context data */
	if (pbs_client_thread_unlock_connection(c) != 0)
		return pbs_errno;

	return rc;
}

/**
 * @brief
 *	-Send a request to spawn a python script to the MS
 *	of a job.  It will run as a task.
 *
 * @param[in] c - communication handle
 * @param[in] jobid - job id
 * @param[in] argv - pointer to argument list
 * @param[in] envp - pointer to environment variable
 *
 * @return	int
 * @retval	exit value of the task	success
 * @retval	-1			error
 *
 */
int
pbs_py_spawn(int c, char *jobid, char **argv, char **envp)
{
	struct batch_reply *reply;
	int	rc;

	/*
	 ** Must have jobid and argv[0] as a minimum.
	 */
	if ((jobid == NULL) || (*jobid == '\0') ||
		(argv == NULL) || (argv[0] == NULL)) {
		pbs_errno = PBSE_IVALREQ;
		return -1;
	}
	/* initialize the thread context data, if not already initialized */
	if (pbs_client_thread_init_thread_context() != 0)
		return -1;

	/* lock pthread mutex here for this connection */
	/* blocking call, waits for mutex release */
	if (pbs_client_thread_lock_connection(c) != 0)
		return -1;

	/* setup DIS support routines for following DIS calls */

	DIS_tcp_setup(connection[c].ch_socket);

	if ((rc = PBSD_py_spawn_put(c, jobid, argv, envp, 0, NULL)) != 0) {
		connection[c].ch_errtxt = strdup(dis_emsg[rc]);
		if (connection[c].ch_errtxt == NULL) {
			pbs_errno = PBSE_SYSTEM;
		} else {
			pbs_errno = PBSE_PROTOCOL;
		}
		(void)pbs_client_thread_unlock_connection(c);
		return -1;
	}

	/* read reply */

	reply = PBSD_rdrpy(c);
	if ((reply == NULL) || (connection[c].ch_errno != 0))
		rc = -1;
	else
		rc = reply->brp_auxcode;

	PBSD_FreeReply(reply);

	/* unlock the thread lock and update the thread context data */
	if (pbs_client_thread_unlock_connection(c) != 0)
		return -1;

	return rc;
}

int
pbs_relnodesjob(c, jobid, node_list, extend)
int c;
char *jobid;
char *node_list;
char *extend;
{
	struct batch_reply *reply;
	int	rc;

	if ((jobid == NULL) || (*jobid == '\0') ||
					(node_list == NULL))
		return (pbs_errno = PBSE_IVALREQ);

	/* initialize the thread context data, if not already initialized */
	if (pbs_client_thread_init_thread_context() != 0)
		return pbs_errno;

	/* first verify the resource list in keep_select option */
	if (extend) {
		struct attrl *attrib = NULL;
		char ebuff[200], *erp, *emsg = NULL;
		int i;
		struct pbs_client_thread_connect_context *con;
		if ((i = set_resources(&attrib, extend, 1, &erp))) {
			if (i > 1) {
				snprintf(ebuff, sizeof(ebuff), "illegal -k value: %s\n", pbs_parse_err_msg(i));
				emsg = strdup(ebuff);
			} else
				emsg = strdup("illegal -k value\n");
			pbs_errno = PBSE_INVALSELECTRESC;
		} else {
			if (!attrib || strcmp(attrib->resource, "select")) {
				emsg = strdup("only a \"select=\" string is valid in -k option\n");
				pbs_errno = PBSE_IVALREQ;
			} else
				pbs_errno = PBSE_NONE;
		}
		if (pbs_errno) {
			if ((con = pbs_client_thread_find_connect_context(c))) {
				if (con->th_ch_errtxt)
					free(con->th_ch_errtxt);
				con->th_ch_errtxt = emsg;
				con->th_ch_errno = pbs_errno;
			} else
				connection[c].ch_errtxt = emsg;
			return pbs_errno;
		}
		rc = pbs_verify_attributes(c, PBS_BATCH_RelnodesJob,
			MGR_OBJ_JOB, MGR_CMD_NONE, (struct attropl *) attrib);
		if (rc)
			return rc;
	}

	/* lock pthread mutex here for this connection */
	/* blocking call, waits for mutex release */
	if (pbs_client_thread_lock_connection(c) != 0)
		return pbs_errno;

	/* setup DIS support routines for following DIS calls */

	DIS_tcp_setup(connection[c].ch_socket);

	if ((rc = PBSD_relnodes_put(c, jobid, node_list, extend, 0, NULL)) != 0) {
		connection[c].ch_errtxt = strdup(dis_emsg[rc]);
		if (connection[c].ch_errtxt == NULL) {
			pbs_errno = PBSE_SYSTEM;
		} else {
			pbs_errno = PBSE_PROTOCOL;
		}
		(void)pbs_client_thread_unlock_connection(c);
		return pbs_errno;
	}

	/* read reply */

	reply = PBSD_rdrpy(c);
	rc = connection[c].ch_errno;

	PBSD_FreeReply(reply);

	/* unlock the thread lock and update the thread context data */
	if (pbs_client_thread_unlock_connection(c) != 0)
		return pbs_errno;

	return rc;
}

