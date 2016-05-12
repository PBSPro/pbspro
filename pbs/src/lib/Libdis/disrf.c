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
 * 	float disrf(int stream, int *retval)
 *
 *	Gets a Data-is-Strings floating point number from <stream> and converts
 *	it into a float and returns it.  The number from <stream> consists of
 *	two consecutive signed integers.  The first is the coefficient, with its
 *	implied decimal point at the low-order end.  The second is the exponent
 *	as a power of 10.
 *
 *	*<retval> gets DIS_SUCCESS if everything works well.  It gets an error
 *	code otherwise.  In case of an error, the <stream> character pointer is
 *	reset, making it possible to retry with some other conversion strategy.
 *
 *	By fiat of the author, neither loss of significance nor underflow are
 *	errors.
 */

#include <pbs_config.h>   /* the master config generated by configure */

#include <assert.h>
#include <math.h>
#include <stddef.h>

#include "dis.h"
#include "dis_.h"
#undef disrf


/**
 * @brief
 *	-Gets a Data-is-Strings floating point number from <stream> and converts
 *      it into a double and returns it.  The number from <stream> consists of
 *      two consecutive signed integers.  The first is the coefficient, with its
 *      implied decimal point at the low-order end.  The second is the exponent
 *      as a power of 10.
 *
 * @return	int
 * @retval	DIS_success/error status
 *
 */
static int
disrd_(int stream, unsigned count, unsigned *ndigs, unsigned *nskips, double *dval, int recursv)
{
	int		c;
	int		negate;
	unsigned	unum;
	char		*cp;

	if (++recursv > DIS_RECURSIVE_LIMIT)
		return (DIS_PROTO);

	/* dis_umaxd would be initialized by prior call to dis_init_tables */
	switch (c = (*dis_getc)(stream)) {
		case '-':
		case '+':
			negate = c == '-';
			*nskips = count > FLT_DIG ? count - FLT_DIG : 0;
			count -= *nskips;
			*ndigs = count;
			*dval = 0.0;
			do {
				if ((c = (*dis_getc)(stream)) < '0' || c > '9') {
					if (c < 0)
						return (DIS_EOD);
					return (DIS_NONDIGIT);
				}
				*dval = *dval * 10.0 + (double)(c - '0');
			} while (--count);
			if ((count = *nskips) > 0) {
				count--;
				switch ((*dis_getc)(stream)) {
					case '5':
						if (count == 0)
							break;
					case '6':
					case '7':
					case '8':
					case '9':
						*dval += 1.0;
					case '0':
					case '1':
					case '2':
					case '3':
					case '4':
						if (count > 0 &&
							(*disr_skip)(stream, (size_t)count) < 0)
							return (DIS_EOD);
						break;
					default:
						return (DIS_NONDIGIT);
				}
			}
			*dval = negate ? -(*dval) : (*dval);
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
			unum = c - '0';
			if (count > 1) {
				if (count > dis_umaxd)
					break;
				if ((*dis_gets)(stream, dis_buffer + 1, count - 1) !=
					count - 1)
					return (DIS_EOD);
				cp = dis_buffer;
				if (count == dis_umaxd) {
					*cp = c;
					if (memcmp(dis_buffer, dis_umax, dis_umaxd) > 0)
						break;
				}
				while (--count) {
					if ((c = *++cp) < '0' || c > '9')
						return (DIS_NONDIGIT);
					unum = unum * 10 + (unsigned)(c - '0');
				}
			}
			return (disrd_(stream, unum, ndigs, nskips, dval, recursv));
		case -1:
			return (DIS_EOD);
		case -2:
			return (DIS_EOF);
		default:
			return (DIS_NONDIGIT);
	}
	*dval = HUGE_VAL;
	return (DIS_OVERFLOW);
}

/**
 * @brief
 *      Gets a Data-is-Strings floating point number from <stream> and converts
 *      it into a float which it returns.  The number from <stream> consists of
 *      two consecutive signed integers.  The first is the coefficient, with its
 *      implied decimal point at the low-order end.  The second is the exponent
 *      as a power of 10.
 *
 * @param[in] stream - socket descriptor
 * @param[out] retval - success/error code
 *
 * @return      double
 * @retval      double value    success
 * @retval      0.0             error
 *
 */

float
disrf(int stream, int *retval)
{
	int		expon;
	unsigned	uexpon;
	int		locret;
	int		negate;
	/* following were static vars, so initializing them to defaults */
	unsigned        ndigs=0; /* 3 vars now stack variables for threads */
	unsigned        nskips=0;
	double          dval=0.0;


	assert(retval != NULL);
	assert(stream >= 0);
	assert(dis_getc != NULL);
	assert(dis_gets != NULL);
	assert(disr_skip != NULL);
	assert(disr_commit != NULL);

	dval = 0.0;
	if ((locret = disrd_(stream, 1, &ndigs, &nskips, &dval, 0)) == DIS_SUCCESS) {
		locret = disrsi_(stream, &negate, &uexpon, 1, 0);
		if (locret == DIS_SUCCESS) {
			expon = negate ? nskips - uexpon : nskips + uexpon;
			if (expon + (int)ndigs > FLT_MAX_10_EXP) {
				if (expon + (int)ndigs > FLT_MAX_10_EXP + 1) {
					dval = dval < 0.0 ?
						-HUGE_VAL : HUGE_VAL;
					locret = DIS_OVERFLOW;
				} else {
					dval *= disp10d_(expon - 1);
					if (dval > FLT_MAX / 10.0) {
						dval = dval < 0.0 ?
							-HUGE_VAL : HUGE_VAL;
						locret = DIS_OVERFLOW;
					} else
						dval *= 10.0;
				}
			} else {
				if (expon < DBL_MIN_10_EXP) {
					dval *= disp10d_(expon + (int)ndigs);
					dval /= disp10d_((int)ndigs);
				} else
					dval *= disp10d_(expon);
			}
		}
	}
	if ((*disr_commit)(stream, locret == DIS_SUCCESS) < 0)
		locret = DIS_NOCOMMIT;
	*retval = locret;
	return (dval);
}
