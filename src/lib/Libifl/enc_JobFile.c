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
 * @file	enc_JobFile.c
 * @brief
 * encode_DIS_JobFile() - encode a Job Releated File
 *
 * @par	Data items are:
 * 			u int	block sequence number
 *			u int	file type (stdout, stderr, ...)
 *			u int	size of data in block
 *			string	job id
 *			cnt str	data
 */

#include <pbs_config.h>   /* the master config generated by configure */

#include "libpbs.h"
#include "pbs_error.h"
#include "dis.h"

/**
 * @brief
 *	-encode a Job Releated File
 *
 * @param[in]   sock -  the communication end point.
 * @param[in]   seq -   sequence number of the current block of data being sent
 * @param[in]   buf - block of data to be sent
 * @param[in]   len - # of characters in 'buf'
 * @param[in]	jobid - job id
 * @param[in] 	which - file type
 *
 * @return      int
 * @retval      0 for success
 * @retval      non-zero otherwise
 */

int
encode_DIS_JobFile(int sock, int seq, char *buf, int len, char *jobid, int which)
{
	int   rc;

	if (jobid == NULL)
		jobid = "";
	if ((rc = diswui(sock, seq) != 0) ||
		(rc = diswui(sock, which) != 0) ||
		(rc = diswui(sock, len) != 0) ||
		(rc = diswst(sock, jobid) != 0) ||
		(rc = diswcs(sock, buf, len) != 0))
			return rc;

	return 0;
}
