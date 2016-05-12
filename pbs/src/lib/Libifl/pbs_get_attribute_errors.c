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
/*	pbs_get_attribute_errors.c

 The function returns the attributes that failed verification
 */

#include <pbs_config.h>   /* the master config generated by configure */

#include <stdlib.h>
#include <stdio.h>
#include "libpbs.h"

/**
 * @brief
 *	-The function returns the attributes that failed verification
 *
 * @param[in] connect - socket descriptor
 *
 * @return	structure handle
 * @retval	pointer to ecl_attribute_errors struct		success
 * @retval	NULL						error
 *
 */
struct ecl_attribute_errors * 
pbs_get_attributes_in_error(int connect)
{
	struct ecl_attribute_errors *err_list = NULL;
	struct pbs_client_thread_context *ptr = pbs_client_thread_get_context_data();
	if (ptr)
		err_list = ptr->th_errlist;

	if (err_list && err_list->ecl_numerrors)
		return err_list;
	else
		return NULL;
}
