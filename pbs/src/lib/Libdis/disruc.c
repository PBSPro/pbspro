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
/**
 * @brief
 * Synopsis:
 *	unsigned char disruc(int stream, int *<retval>)
 *
 *	Gets a Data-is-Strings unsigned integer from <stream>, converts it into
 *	an unsigned char, and returns it.  The unsigned integer in <stream>
 *	consists of a counted string of digits, starting with a zero, which
 *	represents the number.  If the number doesn't lie between 0 and 9,
 *	inclusive, it is preceeded by at least one count.
 *
 *	This format for character strings representing unsigned integers can
 *	best be understood through the decoding algorithm:
 *
 *	1. Initialize the digit count to 1.
 *
 *	2. Read the next digit; if it is a plus sign, go to step (4); if it
 *	   is a minus sign, post an error.
 *
 *	3. Decode a new count from the digit decoded in step (2) and the next
 *	   count - 1 digits; repeat step (2).
 *
 *	4. Decode the next count digits as the unsigned integer.
 *
 *	*<retval> gets DIS_SUCCESS if everything works well.  It gets an error
 *	code otherwise.  In case of an error, the <stream> character pointer is
 *	reset, making it possible to retry with some other conversion strategy.
 */

#include <pbs_config.h>   /* the master config generated by configure */

#include <assert.h>
#include <stddef.h>

#include "dis.h"
#include "dis_.h"
#undef disruc

/**
 * @brief
 *      Gets a Data-is-Strings signed integer from <stream>, converts it
 *      into a unsigned char, and returns it
 *
 * @param[in] stream - pointer to data stream
 * @param[out] retval - return value
 *
 * @return      unsigned char
 * @retval      converted value         success
 * @retval      0                       error
 *
 */

unsigned char
disruc(int stream, int *retval)
{
	int		locret;
	int		negate;
	unsigned	value;

	assert(retval != NULL);
	assert(disr_commit != NULL);

	locret = disrsi_(stream, &negate, &value, 1, 0);
	if (locret != DIS_SUCCESS) {
		value = 0;
	} else if (negate) {
		value = 0;
		locret = DIS_BADSIGN;
	} else if (value > UCHAR_MAX) {
		value = UCHAR_MAX;
		locret = DIS_OVERFLOW;
	}
	*retval = ((*disr_commit)(stream, locret == DIS_SUCCESS) < 0) ?
		DIS_NOCOMMIT : locret;
	return ((unsigned char)value);
}
