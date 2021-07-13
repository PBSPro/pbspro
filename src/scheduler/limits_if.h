/*
 * Copyright (C) 1994-2021 Altair Engineering, Inc.
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

#ifndef	_LIMITS_IF_H
#define	_LIMITS_IF_H
#include	"pbs_ifl.h"
#include	"pbs_entlim.h"

enum limtype {
	LIM_RES,	/* new-style resource limit */
	LIM_RUN,	/* new-style run (i.e. job count) limit */
	LIM_OLD		/* old-style run limit */
};

/**	@fn void *lim_alloc_liminfo(void)
 *	@brief	allocate and return storage for recording limit information
 *
 *	@par MT-safe:	No
 */
void *lim_alloc_liminfo(void);

/**	@fn void *lim_dup_liminfo(void *p)
 *	@brief	duplicate limit information allocated by lim_alloc_liminfo()
 *
 *	@param	p	the data to be cloned
 *
 *	@return		pointer to cloned data on success or NULL on failure
 *
 *	@par MT-safe:	No
 */
void *lim_dup_liminfo(void *);

/**	@fn void lim_free_liminfo(void *p)
 *	@brief	free limit information allocated by lim_alloc_liminfo()
 *
 *	@param	p	the data to be freed
 *
 *	@par MT-safe:	No
 */
void lim_free_liminfo(void *);

/**	@fn int has_hardlimits(void *p)
 *	@brief	are any hard limits set?
 *
 *	@param p	the limit storage to test
 *
 *	@retval 0	no hard limits set
 *	@retval 1	at least one hard limit set
 *
 *	@par MT-safe:	No
 */
int has_hardlimits(void *);

/**	@fn int has_softlimits(void *p)
 *	@brief	are any soft limits set?
 *
 *	@param p	the limit storage to test
 *
 *	@retval 0	no soft limits set
 *	@retval 1	at least one soft limit set
 *
 *	@par MT-safe:	No
 */
int has_softlimits(void *);

/**	@fn int is_reslimattr(const struct attrl *a)
 *	@brief	is the given attribute a new-style resource limit attribute?
 *
 *	@param a	pointer to the attribute
 *
 *	@retval 0	the named attribute is not a new-style resource limit
 *	@retval 1	the named attribute is a new-style resource limit
 *
 *	@par MT-safe:	Yes
 */
int is_reslimattr(const struct attrl *);

/**	@fn int is_runlimattr(const struct attrl *a)
 *	@brief	is the given attribute a new-style run limit attribute?
 *
 *	@param a	pointer to the attribute
 *
 *	@retval 0	the named attribute is not a new-style run limit
 *	@retval 1	the named attribute is a new-style run limit
 *
 *	@par MT-safe:	Yes
 */
int is_runlimattr(const struct attrl *);

/**	@fn int is_oldlimattr(const struct attrl *a)
 *	@brief	is the given attribute an old-style limit attribute?
 *
 *	@param a	pointer to the attribute
 *
 *	@retval 0	the named attribute is not an old-style limit attribute
 *	@retval 1	the named attribute is an old-style limit attribute
 *
 *	@par MT-safe:	Yes
 */
int is_oldlimattr(const struct attrl *);

/**
 * @brief
 * 		convert an old limit attribute name to the new one
 *
 * @param[in]	a	-	attribute list structure
 *
 * @return char *
 * @retval !NULL	: old limit attribute name
 * @retval NULL		: attribute value is not an old limit attribute
 *
 */
const char * convert_oldlim_to_new(const struct attrl *a);

/**	@fn int lim_setlimits(const struct attrl *a, enum limtype lt, void *p)
 *	@brief set resource or run-time limits
 *
 *	For the given attribute value and limit data, parse and set any limit
 *	directives found therein.
 *
 *	@param a	pointer to the attribute, whose value is a new-style
 *			param and may contain multiple limits to set
 *	@param lt	the class of limit being set
 *	@param p	the place to store limit information
 *
 *	@retval 0	limits were successfully set
 *	@retval nonzero	limits were not successfully set
 *
 *	@par MT-safe:	No
 */
int lim_setlimits(const struct attrl *, enum limtype, void *);

/**	@fn int check_limits(server_info *si, queue_info *qi, resource_resv *rr,
 *                          schd_error *err, int mode)
 *	@brief	check all run-time hard limits to see whether a job may run
 *
 *	@param si	server_info structure (for server resources, list of
 *			running jobs, limit information, ...)
 *	@param qi	queue_info structure (for server resources, list of
 *			running jobs, limit information, ...)
 *	@param rr	resource_resv structure (array of assigned resources to
 *			count against resource limits, group/user name, ...)
 *	@param err	schd_error structure to return error information
 *
 *	@param mode specifies the mode in which limits need to be checked
 *
 *	@retval 0	job exceeds no run-time hard limits
 *	@retval nonzero	job exceeds at least one run-time hard limit
 *
 *	@par MT-safe:	No
 */
int check_limits(server_info *, queue_info *, resource_resv *,
	schd_error *, unsigned int);

/**	@fn int check_soft_limits(server_info *si, queue_info *qi, resource_resv *rr)
 *	@brief	check to see whether a job exceeds its run-time soft limits
 *
 *	@param si	server_info structure (for server resources, list of
 *			running jobs, limit information, ...)
 *	@param qi	queue_info structure (for server resources, list of
 *			running jobs, limit information, ...)
 *	@param rr	resource_resv structure (array of assigned resources to
 *			count against resource limits, group/user name, ...)
 *
 *	@retval 0	job exceeds no run-time soft limits
 *	@retval PREEMPT_TO_BIT(enum preempt) otherwise
 *
 *	@par MT-safe:	No
 *
 *	@see		#preempt in constant.h
 */
int check_soft_limits(server_info *, queue_info *, resource_resv *);

/**
 *      @fn void clear_limres()
 *      @brief free and clear saved limit resources.  Must be called whenever
 *             resource definitions are updated.
 *      @return void
 */
void clear_limres(void);

/**
 * 	@fn schd_resource query_limres()
 * 	@brief returns a linked list of resources being limited.
 * 	@par This is to be treated as read-only.  Modifying this will adversely
 * 	     affect the limits code
 *
 * 	@return schd_resource *
 */
schd_resource *query_limres(void);

 /**
  *  @brief check the soft limit using soft limit function.
  *
  *  @return void
  */
void update_soft_limits(server_info *, queue_info *, resource_resv *);

/**
 * @brief	find the value of preempt bit with matching entity and resource in
 *		the counts structure
 * @return	int
 */
int find_preempt_bits(counts *, std::string &, resource_resv *);
#endif	/* _LIMITS_IF_H */
