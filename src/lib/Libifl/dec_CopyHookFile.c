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
 * @file	dec_CopyHookFile.c
 */

#include <pbs_config.h>   /* the master config generated by configure */

#include <sys/types.h>
#include <stdlib.h>
#include "libpbs.h"
#include "list_link.h"
#include "server_limits.h"
#include "attribute.h"
#include "credential.h"
#include "batch_request.h"
#include "dis.h"

/**
 *
 * @brief
 *	Decode the data items needed for a Copy Hook Filecopy request as:
 * 			u int	block sequence number
 *			u int	size of data in block
 *			string	hook file name
 *			cnt str	file data contents
 *
 * @param[in]	sock	- the connection to get data from.
 * @param[in]	preq	- a request structure
 *
 * @return	int
 * @retval	0 for success
 *		non-zero otherwise
 */

int
decode_DIS_CopyHookFile(int sock, struct batch_request *preq)
{
	int   rc=0;
	size_t amt;

	if (preq == NULL)
		return 0;

	preq->rq_ind.rq_hookfile.rq_data = 0;

	preq->rq_ind.rq_hookfile.rq_sequence = disrui(sock, &rc);
	if (rc) return rc;

	preq->rq_ind.rq_hookfile.rq_size = disrui(sock, &rc);
	if (rc) return rc;

	if ((rc = disrfst(sock, MAXPATHLEN+1,
		preq->rq_ind.rq_hookfile.rq_filename)) != 0)
		return rc;

	preq->rq_ind.rq_hookfile.rq_data = disrcs(sock, &amt, &rc);
	if ((amt != preq->rq_ind.rq_hookfile.rq_size) && (rc == 0))
		rc = DIS_EOD;
	if (rc) {
		if (preq->rq_ind.rq_hookfile.rq_data)
			(void)free(preq->rq_ind.rq_hookfile.rq_data);
		preq->rq_ind.rq_hookfile.rq_data = 0;
	}

	return rc;
}
