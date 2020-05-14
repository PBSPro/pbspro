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
 * @file	enc_Reg.c
 * @brief
 * encode_DIS_Register() - encode a Register Dependency Batch Request
 *
 *	This request is used by the server ONLY; its input is a server
 *	batch request structure.
 *
 * @par	Data items are:
 * 			string		job owner
 *			string		parent job id
 *			string		child job id
 *			unsigned int	dependency type
 *			unsigned int	operation
 *			signed long	cost
 */

#include <pbs_config.h>   /* the master config generated by configure */

#include <sys/types.h>
#include "libpbs.h"
#include "list_link.h"
#include "server_limits.h"
#include "attribute.h"
#include "credential.h"
#include "batch_request.h"
#include "dis.h"

/**
 * @brief
 *      -encode a Register Dependency Batch Request
 *
 * @par Functionality:
 *       	This request is used by the server ONLY; its input is a server
 *      	batch request structure.
 *
 * @par Data items are:
 *              string          job owner\n
 *              string          parent job id\n
 *              string          child job id\n
 *              unsigned int    dependency type\n
 *              unsigned int    operation\n
 *              signed long     cost\n
 *
 * @param[in] sock - socket descriptor
 * @param[out] preq - pointer to batch_request structure
 *
 * @return      int
 * @retval      DIS_SUCCESS(0)  success
 * @retval      error code      error
 *
 */

int
encode_DIS_Register(int sock, struct batch_request *preq)
{
	int   rc;

	if ((rc = diswst(sock, preq->rq_ind.rq_register.rq_owner) != 0) ||
		(rc = diswst(sock, preq->rq_ind.rq_register.rq_parent) != 0) ||
		(rc = diswst(sock, preq->rq_ind.rq_register.rq_child) != 0) ||
		(rc = diswui(sock, preq->rq_ind.rq_register.rq_dependtype) != 0) ||
		(rc = diswui(sock, preq->rq_ind.rq_register.rq_op) != 0) ||
		(rc = diswsl(sock, preq->rq_ind.rq_register.rq_cost) != 0))
			return rc;

	return 0;
}
