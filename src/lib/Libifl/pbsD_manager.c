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
 * @file	pbs_manager.c
 * @brief
 * Basically a pass-thru to PBS_manager
 */

#include <pbs_config.h>   /* the master config generated by configure */

#include "libpbs.h"


/**
 * @brief
 *	- Basically a pass-thru to PBS_manager
 *
 * @param[in] c - connection handle
 * @param[in] command - mgr command with respect to obj
 * @param[in] objtype - object type
 * @param[in] objname - object name
 * @param[in] attrib -  pointer to attropl structure
 * @param[in] extend - extend string to encode req
 *
 * @return	int
 * @retval	0	success
 * @retval	!0	error
 *
 */
int
__pbs_manager(int c, int command, int objtype, char *objname,
		struct attropl *attrib, char *extend)
{
	return PBSD_manager(c,
		PBS_BATCH_Manager,
		command,
		objtype,
		objname,
		attrib,
		extend);
}
