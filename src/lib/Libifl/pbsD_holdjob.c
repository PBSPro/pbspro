/*
 * Copyright (C) 1994-2019 Altair Engineering, Inc.
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
 * @file	pbs_holdjob.c
 * @brief
 * Send the Hold Job request to the server --
 * really just an instance of the "manager" request.
 */

#include <pbs_config.h>   /* the master config generated by configure */

#include <stdio.h>
#include "libpbs.h"


/**
 * @brief
 *	- Send the Hold Job request to the server --
 *	really just an instance of the "manager" request.
 *
 * @param[in] c - connection handler
 * @param[in] jobid - job identifier
 * @param[in] holdtype - value for holdtype
 * @param[in] extend - string to encode req
 *
 * @return      int
 * @retval      0       success
 * @retval      !0      error
 *
 */


int
__pbs_holdjob(int c, char *jobid, char *holdtype, char *extend)
{
	struct attropl aopl;

	if ((jobid == NULL) || (*jobid == '\0'))
		return (pbs_errno = PBSE_IVALREQ);

	aopl.name = ATTR_h;
	aopl.resource = NULL;
	if ((holdtype == NULL) || (*holdtype == '\0'))
		aopl.value = "u";
	else
		aopl.value = holdtype;
	aopl.op = SET;
	aopl.next = NULL;
	return PBSD_manager(c, PBS_BATCH_HoldJob,
		MGR_CMD_SET,
		MGR_OBJ_JOB,
		jobid,
		&aopl,
		extend);
}
