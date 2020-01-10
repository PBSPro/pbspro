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
#include <pbs_config.h>   /* the master config generated by configure */

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "dis.h"
#include "dis_.h"

extern char *ulmax;
extern unsigned ulmaxdigs;
/**
 * @file	disrsl.c
 */
/**
 * @brief
 *      -  Gets a Data-is-Strings signed integer from <stream>, converts it into
 *      an int and returns it.
 *
 * @return      int
 * @retval      DIS_success/error status
 *
 */

int
disrsl_(int stream, int *negate, unsigned long *value, unsigned long count, int recursv)
{
	int		c;
	unsigned long	locval;
	unsigned long	ndigs;
	char		*cp;

	assert(negate != NULL);
	assert(value != NULL);
	assert(count);
	assert(stream >= 0);
	assert(dis_getc != NULL);
	assert(dis_gets != NULL);

	if (++recursv > DIS_RECURSIVE_LIMIT)
		return (DIS_PROTO);

	switch (c = (*dis_getc)(stream)) {
		case '-':
		case '+':
			if (count > ulmaxdigs)
				goto overflow;
			*negate = c == '-';
			if ((*dis_gets)(stream, dis_buffer, count) != count)
				return (DIS_EOD);
			if (count == ulmaxdigs) {
				if (memcmp(dis_buffer, ulmax, ulmaxdigs) > 0)
					goto overflow;
			}
			cp = dis_buffer;
			locval = 0;
			do {
				if ((c = *cp++) < '0' || c > '9')
					return (DIS_NONDIGIT);
				locval = 10 * locval + c - '0';
			} while (--count);
			*value = locval;
			return (DIS_SUCCESS);
		case '0':
			return (DIS_LEADZRO);
		case '1':
		case '2':
		case '3':
		case '4':
		case '5':
		case '6':
		case '7':
		case '8':
		case '9':
			ndigs = c - '0';
			if (count > 1) {
				if (count > ulmaxdigs)
					break;
				if ((*dis_gets)(stream, dis_buffer + 1, count - 1) !=
					count - 1)
					return (DIS_EOD);
				cp = dis_buffer;
				if (count == ulmaxdigs) {
					*cp = c;
					if (memcmp(dis_buffer, ulmax, ulmaxdigs) > 0)
						break;
				}
				while (--count) {
					if ((c = *++cp) < '0' || c > '9')
						return (DIS_NONDIGIT);
					ndigs = 10 * ndigs + c - '0';
				}
			}
			return (disrsl_(stream, negate, value, ndigs, recursv));
		case -1:
			return (DIS_EOD);
		case -2:
			return (DIS_EOF);
		default:
			return (DIS_NONDIGIT);
	}
	*negate = FALSE;
overflow:
	*value = ULONG_MAX;
	return (DIS_OVERFLOW);
}
