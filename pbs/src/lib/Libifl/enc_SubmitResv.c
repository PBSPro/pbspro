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
 * encode_DIS_SubmitResv() - encode a Submit Resvervation Batch Request
 *
 *	This request is used for the first step in submitting a reservation
 *      sending the reservation attributes.
 *
 *	Data items are:	string	resv id
 *			list of	attribute, see encode_DIS_attropl()
 */

#include <pbs_config.h>   /* the master config generated by configure */

#include "libpbs.h"
#include "pbs_error.h"
#include "dis.h"

/**
 * @brief
 *	-encode a Submit Resvervation Batch Request
 *
 * @par	Functionality:
 *		This request is used for the first step in submitting a reservation
 *      	sending the reservation attributes.
 *
 * @param[in] sock - socket descriptor
 * @param[in] resv_id - reservation id
 * @param[id] aoplp - pointer to attropl struct(list)
 *
 * @return      int
 * @retval      DIS_SUCCESS(0)  success
 * @retval      error code      error
 *
 */

int
encode_DIS_SubmitResv(int sock, char *resv_id, struct attropl *aoplp)
{
	int   rc;

	if (resv_id == (char *)0)
		resv_id = "";

	/* send the reservation ID and then an empty destination
	 * This is done so the server can use the queuejob structure
	 */
	if ((rc = diswst(sock, resv_id) != 0) ||
		(rc = diswst(sock, "") != 0))
			return rc;

	return (encode_DIS_attropl(sock, aoplp));
}
