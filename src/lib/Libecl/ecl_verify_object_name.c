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
 * @file	ecl_verify_object_name.c
 *
 * @brief	Contains a function to validate object names
 */

#include <pbs_config.h>   /* the master config generated by configure */

#include <sys/types.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "pbs_ifl.h"
#include "pbs_ecl.h"
#include "pbs_error.h"
#include "pbs_nodes.h"

/**
 * @brief
 *	pbs_verify_object_name - Validate an object name
 *
 * @par Functionality:
 *	Verify that the name of an object conforms to the type provided.
 *
 * @see
 *	Formats chapter of the PBS Pro Reference Guide for further information.
 *
 * @param[in]	type - Object type
 * @param[in]	name - Object name to check
 *
 * @return	int
 * @retval	0 - The name conforms
 * @retval	1 - The name does not conform (pbs_errno is modified)
 *
 * @par Side Effects:
 *	None
 *
 * @par MT-safe: No
 *
 */
int
pbs_verify_object_name(int type, char *name)
{
	char *ptr;

	if ((type < 0) || (type >= MGR_OBJ_LAST)) {
		pbs_errno = PBSE_IVAL_OBJ_NAME;
		return 1;
	}
	/*
	 * In many cases, the object name will be empty. This is normal
	 * for qmgr commands such as "set server scheduling=true" because
	 * the command will be sent to the default server. Don't bother
	 * checking empty names.
	 */
	if ((name == NULL) || (*name == '\0'))
		return 0;
	switch (type) {
		case MGR_OBJ_SERVER:
			if (strlen(name) > PBS_MAXSERVERNAME) {
				pbs_errno = PBSE_IVAL_OBJ_NAME;
				return 1;
			}
			break;
		case MGR_OBJ_QUEUE:
			if (strlen(name) > PBS_MAXQUEUENAME) {
				pbs_errno = PBSE_QUENBIG;
				return 1;
			}
			/* Must begin with an alphanumeric character. */
			ptr = name;
			if (!isalnum(*ptr)) {
				pbs_errno = PBSE_IVAL_OBJ_NAME;
				return 1;
			}
			for (ptr++; *ptr != '\0'; ptr++) {
				switch (*ptr) {
					case '_':
					case '-':
						break;
					default:
						if (!isalnum(*ptr)) {
							pbs_errno = PBSE_IVAL_OBJ_NAME;
							return 1;
						}
						break;
				}
			}
			break;
		case MGR_OBJ_JOB:
			if (strlen(name) > PBS_MAXJOBNAME) {
				pbs_errno = PBSE_IVAL_OBJ_NAME;
				return 1;
			}
			break;
		case MGR_OBJ_NODE:
			if (strlen(name) > PBS_MAXNODENAME) {
				pbs_errno = PBSE_NODENBIG;
				return 1;
			}
			break;
		case MGR_OBJ_RESV:
			if (strlen(name) > PBS_MAXQRESVNAME) {
				pbs_errno = PBSE_IVAL_OBJ_NAME;
				return 1;
			}
			break;
		case MGR_OBJ_HOST:
			if (strlen(name) > PBS_MAXHOSTNAME) {
				pbs_errno = PBSE_IVAL_OBJ_NAME;
				return 1;
			}
			break;
		case MGR_OBJ_RSC:
		case MGR_OBJ_SCHED:
		case MGR_OBJ_SITE_HOOK:
		case MGR_OBJ_PBS_HOOK:
		default:
			break;
	}

	return 0;
}

