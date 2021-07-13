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

/**
 * @file    limits.c
 *
 * @brief
 * 		limits.c - This file contains functions related to limit information.
 *
 * Functions included are:
 * 	lim_alloc_liminfo()
 * 	lim_dup_liminfo()
 * 	lim_free_liminfo()
 * 	is_reslimattr()
 * 	is_runlimattr()
 * 	is_oldlimattr()
 * 	lim_setlimits()
 * 	has_hardlimits()
 * 	has_softlimits()
 * 	check_limits()
 * 	check_soft_limits()
 * 	check_server_max_user_run()
 * 	check_server_max_group_run()
 * 	check_server_max_user_res()
 * 	check_server_max_group_res()
 * 	check_queue_max_user_run()
 * 	check_queue_max_group_run()
 * 	check_queue_max_user_res()
 * 	check_queue_max_group_res()
 * 	check_queue_max_res()
 * 	check_server_max_res()
 * 	check_server_max_run()
 * 	check_queue_max_run()
 * 	check_queue_max_run_soft()
 * 	check_queue_max_user_run_soft()
 * 	check_queue_max_group_run_soft()
 * 	check_queue_max_user_res_soft()
 * 	check_queue_max_group_res_soft()
 * 	check_server_max_run_soft()
 * 	check_server_max_user_run_soft()
 * 	check_server_max_group_run_soft()
 * 	check_server_max_user_res_soft()
 * 	check_server_max_group_res_soft()
 * 	check_server_max_res_soft()
 * 	check_queue_max_res_soft()
 * 	check_max_group_res()
 * 	check_max_group_res_soft()
 * 	check_max_user_res()
 * 	check_max_user_res_soft()
 * 	lim_setreslimits()
 * 	clear_limres()
 * 	lim_setrunlimits()
 * 	lim_setoldlimits()
 * 	lim_dup_ctx()
 * 	is_hardlimit()
 * 	lim_gengroupreskey()
 * 	lim_genprojectreskey()
 * 	lim_genuserreskey()
 * 	lim_callback()
 * 	lim_get()
 * 	schderr_args_q()
 * 	schderr_args_q_res()
 * 	schderr_args_server()
 * 	schderr_args_server_res()
 * 	check_max_project_res()
 * 	check_max_project_res_soft()
 * 	check_server_max_project_res()
 * 	check_server_max_project_run_soft()
 * 	check_server_max_project_res_soft()
 * 	check_queue_max_project_res()
 * 	check_queue_max_project_run_soft()
 * 	check_queue_max_project_res_soft()
 * 	check_server_max_project_run()
 * 	check_queue_max_project_run()
 *
 */
#include	<unistd.h>
#include	<stdlib.h>
#include	<errno.h>
#include	<stdio.h>
#include	<string.h>
#include	<assert.h>
#include	"pbs_config.h"
#include	"pbs_ifl.h"
#include	"data_types.h"
#include	"resource_resv.h"
#include	"misc.h"
#include	"log.h"
#include	"check.h"
#include	"limits_if.h"
#include	"simulate.h"
#include	"resource.h"
#include	"globals.h"

class  limcounts
{
	public:
	counts_umap user;
	counts_umap group;
	counts_umap project;
	counts_umap all;
	limcounts();
	limcounts(const counts_umap &user_,
		  const counts_umap &group_,
		  const counts_umap &project_,
		  const counts_umap &all_);
	limcounts (const limcounts &);
	limcounts &operator=(const limcounts &);
	~limcounts();
};

static int
check_max_group_res(resource_resv *, counts_umap &,
	resdef **, void *);
static int
check_max_project_res(resource_resv *, counts_umap &,
	resdef **, void *);
static int
check_max_user_res(resource_resv *, counts_umap &, 
	resdef **, void *);
static int
check_max_group_res_soft(resource_resv *,
	counts_umap &, void *, int);
static int
check_max_project_res_soft(resource_resv *,
	counts_umap &, void *, int);
static int
check_max_user_res_soft(resource_resv **, resource_resv *,
	counts_umap &, void *, int);
static int
check_server_max_user_run(server_info *, queue_info *,
	resource_resv *, limcounts *, limcounts *, schd_error *);
static int
check_server_max_group_run(server_info *, queue_info *,
	resource_resv *, limcounts *, limcounts *, schd_error *);
static int
check_server_max_project_run(server_info *, queue_info *,
	resource_resv *, limcounts *, limcounts *, schd_error *);
static int
check_server_max_user_res(server_info *, queue_info *,
	resource_resv *, limcounts *, limcounts *, schd_error *);
static int
check_server_max_group_res(server_info *, queue_info *,
	resource_resv *, limcounts *, limcounts *, schd_error *);
static int
check_server_max_project_res(server_info *, queue_info *,
	resource_resv *, limcounts *, limcounts *, schd_error *);
static int
check_queue_max_user_run(server_info *, queue_info *,
	resource_resv *, limcounts *, limcounts *, schd_error *);
static int
check_queue_max_group_run(server_info *, queue_info *,
	resource_resv *, limcounts *, limcounts *, schd_error *);
static int
check_queue_max_project_run(server_info *, queue_info *,
	resource_resv *, limcounts *, limcounts *, schd_error *);
static int
check_queue_max_user_res(server_info *, queue_info *,
	resource_resv *, limcounts *, limcounts *, schd_error *);
static int
check_queue_max_group_res(server_info *, queue_info *,
	resource_resv *, limcounts *, limcounts *, schd_error *);
static int
check_queue_max_project_res(server_info *, queue_info *,
	resource_resv *, limcounts *, limcounts *, schd_error *);
static int
check_server_max_res(server_info *, queue_info *,
	resource_resv *, limcounts *, limcounts *, schd_error *);
static int
check_queue_max_res(server_info *, queue_info *,
	resource_resv *, limcounts *, limcounts *, schd_error *);
static int
check_server_max_run(server_info *, queue_info *,
	resource_resv *, limcounts *, limcounts *, schd_error *);
static int
check_queue_max_run(server_info *, queue_info *,
	resource_resv *, limcounts *, limcounts *, schd_error *);
static int
check_server_max_user_run_soft(server_info *, queue_info *,
	resource_resv *);
static int
check_server_max_group_run_soft(server_info *, queue_info *,
	resource_resv *);
static int
check_server_max_project_run_soft(server_info *, queue_info *,
	resource_resv *);
static int
check_server_max_user_res_soft(server_info *, queue_info *,
	resource_resv *);
static int
check_server_max_group_res_soft(server_info *, queue_info *,
	resource_resv *);
static int
check_server_max_project_res_soft(server_info *, queue_info *,
	resource_resv *);
static int
check_server_max_res_soft(server_info *, queue_info *,
	resource_resv *);
static int
check_queue_max_res_soft(server_info *, queue_info *,
	resource_resv *);
static int
check_queue_max_user_run_soft(server_info *, queue_info *,
	resource_resv *);
static int
check_queue_max_group_run_soft(server_info *, queue_info *,
	resource_resv *);
static int
check_queue_max_project_run_soft(server_info *, queue_info *,
	resource_resv *);
static int
check_queue_max_user_res_soft(server_info *, queue_info *,
	resource_resv *);
static int
check_queue_max_group_res_soft(server_info *, queue_info *,
	resource_resv *);
static int
check_queue_max_project_res_soft(server_info *, queue_info *,
	resource_resv *);
static int
check_server_max_run_soft(server_info *, queue_info *,
	resource_resv *);
static int
check_queue_max_run_soft(server_info *, queue_info *,
	resource_resv *);

/**
 * @typedef
 * 		int (*limfunc_t)(server_info *, queue_info *, resource_resv *, schd_error *);
 * @brief
 * 		each hard limit function we call has this interface
 * @par
 * 		When adding a new hard limit, be sure to address the following:
 * 			add the function that does limit enforcement to this
 *				list
 * 			add a new error enum to sched_error
 * 			add new log and comment formatting strings to the
 *				fc_translation_table
 * 			format the error string the scheduler will report (use
 *				one of the existing schderr_args_*() functions below or
 *				create a new one)
 * 			add the new error case to translate_fail_code()
 * 			if the limit applies to a job's owner or group, add the
 *				new error case to update_accruetype() so that the job is
 *				marked as ineligible
 *
 * @see	sched_error_code in constant.h
 * @see	fc_translation_table in job_info.c
 */
typedef	int	(*limfunc_t)(server_info *, queue_info *, resource_resv *,
	limcounts *, limcounts *, schd_error *);

static limfunc_t	limfuncs[] = {
	check_queue_max_group_run,
	check_queue_max_project_run,
	check_queue_max_run,
	check_queue_max_user_run,
	check_server_max_group_run,
	check_server_max_project_run,
	check_server_max_run,
	check_server_max_user_run,
	check_queue_max_group_res,
	check_queue_max_project_res,
	check_queue_max_res,
	check_queue_max_user_res,
	check_server_max_group_res,
	check_server_max_project_res,
	check_server_max_res,
	check_server_max_user_res,
};

/**
 * @typedef
 * 		int (*softlimfunc_t)(server_info *, queue_info *, resource_resv *);
 * @brief
 * 		each soft limit function we call has this interface
 */
typedef	int	(*softlimfunc_t)(server_info *, queue_info *, resource_resv *);
static softlimfunc_t	softlimfuncs[] = {
	check_queue_max_run_soft,
	check_queue_max_user_run_soft,
	check_queue_max_group_run_soft,
	check_queue_max_project_run_soft,
	check_server_max_run_soft,
	check_server_max_user_run_soft,
	check_server_max_group_run_soft,
	check_server_max_project_run_soft,
	check_queue_max_user_res_soft,
	check_queue_max_group_res_soft,
	check_queue_max_project_res_soft,
	check_server_max_user_res_soft,
	check_server_max_group_res_soft,
	check_server_max_project_res_soft,
	check_server_max_res_soft,
	check_queue_max_res_soft,
};

/**
 * @struct	lim_old2new
 * @brief
 * 		Maps between old-style limit attribute names and new-style params
 * @par
 *		This structure holds information needed to map old-style limit attribute
 *		names to new-style parameterized keys ("param"s).
 *
 * @param[in]	lim_attr	-	the (old) attribute name
 * @param[in]	lim_param	-	the (new-style) entity parameter
 * @param[in]	lim_isreslim	-	1 if this attribute is a resource limit, else 0
 */
struct lim_old2new {
	const char	*lim_attr;
	const char	*lim_param;
	int	lim_isreslim;
};
static struct lim_old2new old2new[] = {
	{ATTR_maxgroupres,	"g:" PBS_GENERIC_ENTITY,	1 },
	{ATTR_maxgrprun,	"g:" PBS_GENERIC_ENTITY,	0 },
	{ATTR_maxrun,		"o:" PBS_ALL_ENTITY,		0 },
	{ATTR_maxuserres,	"u:" PBS_GENERIC_ENTITY,	1 },
	{ATTR_maxuserrun,	"u:" PBS_GENERIC_ENTITY,	0 }
};
static struct lim_old2new old2new_soft[] = {
	{ATTR_max_run_soft,	"o:" PBS_ALL_ENTITY,		0 },
	{ATTR_max_run_res_soft,	"o:" PBS_ALL_ENTITY,		1 },
	{ATTR_maxgroupressoft,	"g:" PBS_GENERIC_ENTITY,	1 },
	{ATTR_maxgrprunsoft,	"g:" PBS_GENERIC_ENTITY,	0 },
	{ATTR_maxuserressoft,	"u:" PBS_GENERIC_ENTITY,	1 },
	{ATTR_maxuserrunsoft,	"u:" PBS_GENERIC_ENTITY,	0 }
};

static const char	allparam[] = PBS_ALL_ENTITY;
static const char	genparam[] = PBS_GENERIC_ENTITY;

static int		is_hardlimit(const struct attrl *);
static int
lim_callback(void *, enum lim_keytypes, char *, char *,
	char *, char *);
static void		*lim_dup_ctx(void *);
static char		*lim_gengroupreskey(const char *);
static char		*lim_genprojectreskey(const char *);
static char		*lim_genuserreskey(const char *);
static void		schderr_args_q(const std::string& , const char *, schd_error *);
static void		schderr_args_q(const std::string& , const std::string&, schd_error *);
static void 		schderr_args_q_res(const std::string&, const char *, char *, schd_error *);
static void 		schderr_args_q_res(const std::string&, const std::string&, char *, schd_error *);
static void		schderr_args_server(const char *, schd_error *);
static void		schderr_args_server(const std::string&, schd_error *);
static void		schderr_args_server_res(std::string &, const char *, schd_error *);
static sch_resource_t	lim_get(const char *, void *);
static int		lim_setoldlimits(const struct attrl *, void *);
static int		lim_setreslimits(const struct attrl *, void *);
static int		lim_setrunlimits(const struct attrl *, void *);

/**
 * @struct	limit_info
 * @brief
 * 		internal structure of stored limit information
 *
 * @param[in]	li_ctxh	-	limit context for storing (hard) resource and run limits
 * @param[in]	li_ctxs	-	limit context for storing (soft) resource and run limits
 */
struct limit_info {
	void	*li_ctxh;
	void	*li_ctxs;
};
#define	LI2RESCTX(li)		(((struct limit_info *) li)->li_ctxh)
#define	LI2RESCTXSOFT(li)	(((struct limit_info *) li)->li_ctxs)
#define	LI2RUNCTX(li)		(((struct limit_info *) li)->li_ctxh)
#define	LI2RUNCTXSOFT(li)	(((struct limit_info *) li)->li_ctxs)

/**
 * @var	resource *limres
 *
 * @brief
 * 		list of resources that have limits
 * @par
 *		We record in this list only those resources that have had limits set.
 *		This is done in lim_setreslimits() and lim_setoldlimits() and used in
 *		the resource checking functions.  These latter functions loop over
 *		only those resources that appear in this list.  We do not maintain a
 *		separate list of per-queue or per-server resources because each limit
 *		checking function uses a limit evaluation context that narrows the
 *		limit search to the proper context.
 * @note
 *		Note that we do not free and rebuild this list for each scheduling cycle.
 *		Instead, we assume that the number of resources with limits is small and
 *		the index tree limit fetching code is sufficiently fast that this isn't an
 *		issue.
 */
static schd_resource	*limres;	/* list of resources that have limits */
/**
 * @brief
 * 		We currently store both resource and run limits in a
 * 		single member of the limit_info structure.  That might
 * 		change some day, and these assertions are here in order
 * 		to protect accidental violations of this assumption:
 * 		if the run limit contexts are ever NULL after allocation
 * 		of the resource limit contexts, the assumption has been
 * 		violated.
 *
 * @return	allocated lim_info structure
 * @retval	NULL	: failed to allocate memory
*/
void *
lim_alloc_liminfo(void)
{
	struct limit_info	*lip;

	if ((lip = static_cast<limit_info *>(calloc(1, sizeof(struct limit_info)))) == NULL)
		return NULL;
	else {
		void	*ctx;

		if ((ctx = entlim_initialize_ctx()) == NULL) {
			lim_free_liminfo(lip);
			return NULL;
		} else
			LI2RESCTX(lip) = ctx;
		if ((ctx = entlim_initialize_ctx()) == NULL) {
			lim_free_liminfo(lip);
			return NULL;
		} else
			LI2RESCTXSOFT(lip) = ctx;


		assert(LI2RUNCTX(lip) != NULL);
		assert(LI2RUNCTXSOFT(lip) != NULL);

		return (lip);
	}
}
/**
 * @brief
 * 		take duplicate of passed structure and return the duplicate of the lim_info structure
 *
 * @param[in]	p	-	old limit info structure
 *
 * @return	duplicate of old limit info structure
 * @retval	NULL	: failure
 */
void *
lim_dup_liminfo(void *p)
{
	struct limit_info	*oldlip = static_cast<limit_info *>(p);
	struct limit_info	*newlip;

	if ((oldlip == NULL) ||
		(LI2RESCTX(oldlip) == NULL) ||
		(LI2RESCTXSOFT(oldlip) == NULL))
		return NULL;

	if ((newlip = static_cast<limit_info *>(calloc(1, sizeof(struct limit_info)))) == NULL)
		return NULL;
	else {
		void	*ctx;

		if ((ctx = lim_dup_ctx(LI2RESCTX(oldlip))) == NULL) {
			lim_free_liminfo(newlip);
			return NULL;
		} else
			LI2RESCTX(newlip) = ctx;
		if ((ctx = lim_dup_ctx(LI2RESCTXSOFT(oldlip))) == NULL) {
			lim_free_liminfo(newlip);
			return NULL;
		} else
			LI2RESCTXSOFT(newlip) = ctx;

		/*
		 *	We currently store both resource and run limits in a
		 *	single member of the limit_info structure.  That might
		 *	change some day, and these assertions are here in order
		 *	to protect accidental violations of this assumption:
		 *	if the run limit contexts are ever NULL after allocation
		 *	of the resource limit contexts, the assumption has been
		 *	violated.
		 */
		assert(LI2RUNCTX(newlip) != NULL);
		assert(LI2RUNCTXSOFT(newlip) != NULL);

		return (newlip);
	}
}
/**
 * @brief
 * 		free the limit info structure
 *
 * @param[in]	p	-	limit info structure to be freed.
 */
void
lim_free_liminfo(void *p)
{
	struct limit_info	*lip = static_cast<limit_info *>(p);

	if (lip == NULL)
		return;

	if (LI2RESCTX(lip) != NULL) {
		(void) entlim_free_ctx(LI2RESCTX(lip), free);
		LI2RESCTX(lip) = NULL;
	}
	if (LI2RESCTXSOFT(lip) != NULL) {
		(void) entlim_free_ctx(LI2RESCTXSOFT(lip), free);
		LI2RESCTXSOFT(lip) = NULL;
	}
	if (LI2RUNCTX(lip) != NULL) {
		(void) entlim_free_ctx(LI2RUNCTX(lip), free);
		LI2RUNCTX(lip) = NULL;
	}
	if (LI2RUNCTXSOFT(lip) != NULL) {
		(void) entlim_free_ctx(LI2RUNCTXSOFT(lip), free);
		LI2RUNCTXSOFT(lip) = NULL;
	}
	free(lip);
}
/**
 * @brief
 * 		check attribute has max run result as name
 *
 * @param[in]	a	-	attribute list structure
 *
 * @return	int
 * @retval	1	: Yes
 * @retval	0	: No
 */
int
is_reslimattr(const struct attrl *a)
{
	if (!strcmp(a->name, ATTR_max_run_res) ||
		!strcmp(a->name, ATTR_max_run_res_soft))
		return (1);
	else
		return (0);
}
/**
 * @brief
 * 		check attribute has run limit as name
 *
 * @param[in]	a	-	attribute list structure
 *
 * @return	int
 * @retval	1	: Yes
 * @retval	0	: No
 */
int
is_runlimattr(const struct attrl *a)
{
	if (!strcmp(a->name, ATTR_max_run) ||
		!strcmp(a->name, ATTR_max_run_soft))
		return (1);
	else
		return (0);
}
/**
 * @brief
 * 		convert an old limit attribute name to the new one
 *		@see old2new[]
 *		@see old2new_soft[]
 *
 * @param[in]	a	-	attribute list structure
 *
 * @return char *
 * @retval !NULL	: old limit attribute name
 * @retval NULL		: attribute value is not an old limit attribute
 *
 */
const char *
convert_oldlim_to_new(const struct attrl *a)
{
	size_t	i;

	for (i = 0; i < sizeof(old2new)/sizeof(old2new[0]); i++)
		if (!strcmp(a->name, old2new[i].lim_attr))
			return old2new[i].lim_param;
	for (i = 0; i < sizeof(old2new_soft)/sizeof(old2new_soft[0]); i++)
		if (!strcmp(a->name, old2new_soft[i].lim_attr))
			return old2new_soft[i].lim_param;

	return NULL;
}

/*
 * @brief
 *		Return true if attribute is an old limit attribute
 * @param[in] a		attribute list structure
 *
 * @return	int
 * @retval	1	: Yes
 * @retval	0	: No
 */
int
is_oldlimattr(const struct attrl *a)
{
	return (convert_oldlim_to_new(a) != NULL);
}

/**
 * @brief
 * 		assign the resource-limits to the limit context based on the limit type.
 *
 * @param[in]	a	-	attribute list structure
 * @param[in]	lt	-	limit type.
 * @param[in]	p	-	pointer to limit_info structure
 *
 * @return	int
 * @retval	1	: Yes
 * @retval	0	: No
 */
int
lim_setlimits(const struct attrl *a, enum limtype lt, void *p)
{
	struct limit_info	*lip = static_cast<limit_info *>(p);

	switch (lt) {
		case LIM_RES:
			if (is_hardlimit(a))
				return (lim_setreslimits(a, LI2RESCTX(lip)));
			else
				return (lim_setreslimits(a, LI2RESCTXSOFT(lip)));
		case LIM_RUN:
			if (is_hardlimit(a))
				return (lim_setrunlimits(a, LI2RUNCTX(lip)));
			else
				return (lim_setrunlimits(a, LI2RUNCTXSOFT(lip)));
		case LIM_OLD:
			return (lim_setoldlimits(a, lip));
		default:
			log_eventf(PBSEVENT_ERROR, PBS_EVENTCLASS_SCHED, LOG_ERR, __func__,
				"attribute %s not a limit attribute", a->name);
			return (1);
	}
}
/**
 * @brief
 * 		check whether the limit info structure has at least one hard resource limit,
 * 		if so, free them.
 *
 * @param[in,out]	p	-	limit info structure which needs to be checked.
 *
 * @return	int
 * @retval	1	: Hard limit found and freed.
 * @retval	0	: Run limit already checked, or nothing is found.
 */
int
has_hardlimits(void *p)
{
	struct limit_info	*lip = static_cast<limit_info *>(p);
	char *k = NULL;

	if (entlim_get_next(LI2RESCTX(lip), (void **)&k) != NULL) /* at least one hard resource limit present */
		return (1);

	/* run limit already checked? */
	if (LI2RUNCTX(lip) == LI2RESCTX(lip))
		return (0);
	k = NULL;
	if (entlim_get_next(LI2RUNCTX(lip), (void **)&k) != NULL) /* at least one hard run limit present */
		return (1);

	return (0);
}
/**
 * @brief
 * 		check whether the limit info structure has at least one soft resource limit,
 * 		if so, free them.
 *
 * @param[in,out]	p	-	limit info structure which needs to be checked.
 *
 * @return	int
 * @retval	1	: Soft limit found and freed.
 * @retval	0	: Run limit already checked, or nothing is found.
 */
int
has_softlimits(void *p)
{
	struct limit_info	*lip = static_cast<limit_info *>(p);
	char *k = NULL;

	if (entlim_get_next(LI2RESCTXSOFT(lip), (void **)&k) != NULL) /* at least one soft resource limit present */
		return (1);

	/* run limit already checked? */
	if (LI2RUNCTXSOFT(lip) == LI2RESCTXSOFT(lip))
		return (0);
	k = NULL;
	if (entlim_get_next(LI2RUNCTXSOFT(lip), (void **)&k) != NULL) /* at least one soft run limit present */
		return (1);

	return (0);
}
/**
 * @brief
 *		limitcount class constructor.
 */
limcounts::limcounts()
{
}
// Parametrized Constructor
limcounts::limcounts(const counts_umap &user_,
		     const counts_umap &group_,
		     const counts_umap &project_,
		     const counts_umap &all_)
{
	user = user_;
	group = group_;
	project = project_;
	all = all_;
}

// Copy Constructor
limcounts::limcounts(const limcounts &rlimit)
{
	user = rlimit.user;
	group = rlimit.group;
	project = rlimit.project;
	all = rlimit.all;
}

// Assignment operator
limcounts & limcounts::operator=(const limcounts &rlimit)
{
	user = rlimit.user;
	group = rlimit.group;
	project = rlimit.project;
	all = rlimit.all;
	return *this;
}

// destructor
limcounts::~limcounts()
{
    //do nothing
}

/**
 * @brief
 *		check_limits - hard limit checking function.
 *		This is table-driven limit checking, against limfuncs[]
 *		array.
 *
 * @param[in]	si	-	server_info structure to use for limit evaluation
 * @param[in]	qi	-	queue_info structure to use for limit evaluation
 * @param[in]	rr	-	resource_resv structure to use for limit evaluation
 * @param[out]	err	-	sched_error_code structure to return error information
 * @param[in]	flags	-	CHECK_LIMITS - check real limits
 *                      CHECK_CUMULATIVE_LIMIT - check limits against total counts
 *                      RETURN_ALL_ERR - check all limits and return an err for all failed limits *
 *
 * @return	integer indicating failing limit test if limit is exceeded,
 *				along with error 'err'.
 * @retval	0	: if limit is not exceeded.
 */

int
check_limits(server_info *si, queue_info *qi, resource_resv *rr, schd_error *err, unsigned int flags)
{
	enum sched_error_code rc;
	int	any_fail_rc = 0;
	size_t	i;
	limcounts *svr_counts = NULL;
	limcounts *que_counts = NULL;
	limcounts *svr_counts_max = NULL;
	limcounts *que_counts_max = NULL;
	limcounts *server_lim = NULL;
	limcounts *queue_lim = NULL;
	counts *cts;
	schd_error *prev_err = NULL;

	if (si == NULL || qi == NULL || rr == NULL)
		return SE_NONE;

	/*
	 * Check for  CHECK_CUMULATIVE_LIMIT is needed because we  must have
	 * already run through the same loop before while calling check_limits
	 * from is_ok_to_run.
	 * We do not need to run into the same loop again.
	 */
	if (si->calendar != NULL && !(flags & CHECK_CUMULATIVE_LIMIT)) {
		long time_left;
		if (rr->duration != rr->hard_duration &&
		    exists_resv_event(si->calendar, si->server_time + rr->hard_duration))
			time_left = calc_time_left(rr, 1);
		else
			time_left = calc_time_left(rr, 0);

		auto end = si->server_time + time_left;

		if (exists_run_event(si->calendar, end)) {
			if (si->has_hard_limit) {
				svr_counts_max = new limcounts(si->user_counts,
					si->group_counts,
					si->project_counts,
					si->alljobcounts);

				svr_counts = new limcounts(si->user_counts,
					si->group_counts,
					si->project_counts,
					si->alljobcounts);
			}

			if (qi->has_hard_limit) {
				que_counts_max = new limcounts(qi->user_counts,
					qi->group_counts,
					qi->project_counts,
					qi->alljobcounts);

				que_counts = new limcounts(qi->user_counts,
					qi->group_counts,
					qi->project_counts,
					qi->alljobcounts);
			}

			auto te = get_next_event(si->calendar);
			const auto event_mask = TIMED_RUN_EVENT|TIMED_END_EVENT;
			bool error = false;
			for (te = find_init_timed_event(te, IGNORE_DISABLED_EVENTS, event_mask);
			     te != NULL && te->event_time < end;
			     te = find_next_timed_event(te, IGNORE_DISABLED_EVENTS, event_mask)) {
				auto te_rr = static_cast<resource_resv *>(te->event_ptr);
				if ((te_rr != rr) && te_rr->is_job) {
					if (te->event_type == TIMED_RUN_EVENT) {
						if (svr_counts != NULL) {
							cts = find_alloc_counts(svr_counts->user, te_rr->user);
							update_counts_on_run(cts, te_rr->resreq);
							counts_max(svr_counts_max->user, cts);
							if (svr_counts_max->user.size() == 0) {
								error = 1;
								break;
							}

							cts = find_alloc_counts(svr_counts->group, te_rr->group);
							update_counts_on_run(cts, te_rr->resreq);
							counts_max(svr_counts_max->group, cts);
							if (svr_counts_max->group.size() == 0) {
								error = 1;
								break;
							}

							cts = find_alloc_counts(svr_counts->project, te_rr->project);
							update_counts_on_run(cts, te_rr->resreq);
							counts_max(svr_counts_max->project, cts);
							if (svr_counts_max->project.size() == 0) {
								error = 1;
								break;
							}

							cts = find_alloc_counts(svr_counts->all, PBS_ALL_ENTITY);
							update_counts_on_run(cts, te_rr->resreq);
							counts_max(svr_counts_max->all, svr_counts->all);
							if (svr_counts_max->all.size() == 0) {
								error = 1;
								break;
							}
						}

						if (que_counts != NULL) {
							if (te_rr->is_job && te_rr->job != NULL) {
								if (te_rr->job->queue == qi) {
									cts = find_alloc_counts(que_counts->user, te_rr->user);
									update_counts_on_run(cts, te_rr->resreq);
									counts_max(que_counts_max->user, cts);
									if (que_counts_max->user.size() == 0) {
										error = 1;
										break;
									}

									cts = find_alloc_counts(que_counts->group, te_rr->group);
									update_counts_on_run(cts, te_rr->resreq);
									counts_max(que_counts_max->group, cts);
									if (que_counts_max->group.size() == 0) {
										error = 1;
										break;
									}

									cts = find_alloc_counts(que_counts->project, te_rr->project);
									update_counts_on_run(cts, te_rr->resreq);
									counts_max(que_counts_max->project, cts);
									if (que_counts_max->project.size() == 0) {
										error = 1;
										break;
									}

									cts = find_alloc_counts(que_counts->all, PBS_ALL_ENTITY);
									update_counts_on_run(cts, te_rr->resreq);
									counts_max(que_counts_max->all, que_counts->all);
									if (que_counts_max->all.size() == 0) {
										error = 1;
										break;
									}
								}
							}
						}
					}
					else if (te->event_type == TIMED_END_EVENT) {
						if (svr_counts != NULL) {
							cts = find_alloc_counts(svr_counts->user, te_rr->user);
							update_counts_on_end(cts, te_rr->resreq);
							cts = find_alloc_counts(svr_counts->group, te_rr->group);
							update_counts_on_end(cts, te_rr->resreq);
							cts = find_alloc_counts(svr_counts->project, te_rr->project);
							update_counts_on_end(cts, te_rr->resreq);
							cts = find_alloc_counts(svr_counts->all, PBS_ALL_ENTITY);
							update_counts_on_end(cts, te_rr->resreq);
						}
						if (que_counts != NULL) {
							if (te_rr->is_job && te_rr->job != NULL) {
								if (te_rr->job->queue == qi) {
									cts = find_alloc_counts(que_counts->user, te_rr->user);
									update_counts_on_end(cts, te_rr->resreq);
									cts = find_alloc_counts(que_counts->group, te_rr->group);
									update_counts_on_end(cts, te_rr->resreq);
									cts = find_alloc_counts(que_counts->project, te_rr->project);
									update_counts_on_end(cts, te_rr->resreq);
									cts = find_alloc_counts(que_counts->all, PBS_ALL_ENTITY);
									update_counts_on_end(cts, te_rr->resreq);
								}
							}
						}
					}
				}
			}
			delete svr_counts;
			delete que_counts;
			if (error) {
				delete svr_counts_max;
				delete que_counts_max;
				return SE_NONE;
			}
		}

	}
	if ((flags & CHECK_LIMIT)) {
		if (svr_counts_max != NULL) {
			server_lim = svr_counts_max;
		}
		else {
			server_lim = new limcounts(si->user_counts,
				si->group_counts,
				si->project_counts,
				si->alljobcounts);
		}
		if (que_counts_max != NULL) {
			queue_lim = que_counts_max;
		}
		else {
			queue_lim = new limcounts(qi->user_counts,
				qi->group_counts,
				qi->project_counts,
				qi->alljobcounts);
		}
	} else if ((flags & CHECK_CUMULATIVE_LIMIT)) {
		if (!si->has_hard_limit && !qi->has_hard_limit)
			return SE_NONE;
		server_lim = new limcounts(si->total_user_counts,
			si->total_group_counts,
			si->total_project_counts,
			si->total_alljobcounts);
		queue_lim = new limcounts(qi->total_user_counts,
			qi->total_group_counts,
			qi->total_project_counts,
			qi->total_alljobcounts);
	}
	for (i = 0; i < sizeof(limfuncs) / sizeof(limfuncs[0]); i++) {
		rc = static_cast<enum sched_error_code>((limfuncs[i])(si, qi, rr, server_lim, queue_lim, err));
		if (rc != SE_NONE) {
			if ((flags & RETURN_ALL_ERR)) {
				if (any_fail_rc == 0)
					any_fail_rc = rc;
				set_schd_error_codes(err, NOT_RUN, rc);
				err->next = new_schd_error();
				prev_err = err;
				err = err->next;
				if(err == NULL) {
					delete server_lim;
					delete queue_lim;
					return SCHD_ERROR;
				}
			} else {
				set_schd_error_codes(err, NOT_RUN, rc);
				break;
			}
		}
	}

	delete server_lim;
	delete queue_lim;

	if (flags & RETURN_ALL_ERR) {
		if (prev_err != NULL) {
			free_schd_error(err);
			prev_err->next = NULL;
		}
	}

	if (any_fail_rc)
		return any_fail_rc;

	return rc;
}

/**
 * @brief
 *		update_soft_limits - check the soft limit using soft limit function.
 *
 * @param[in]	si	-	server info.
 * @param[in]	qi	-	queue info
 * @param[in]	rr	-	Resource reservation structure
 *
 * @return	void
 */
void update_soft_limits(server_info *si, queue_info *qi, resource_resv *rr)
{
	size_t i;
	for (i = 0; i < sizeof(softlimfuncs)/sizeof(softlimfuncs[0]); i++)
		softlimfuncs[i](si, qi, rr);
	return;
}

/**
 * @brief	find the value of preempt bit with matching entity and resource in
 *		the counts structure
 * @param[in]	entity_counts	-   Counts structure where entity information is stored
 * @param[in]	entity_name	-   Name of the entity
 * @param[in]	rr		-   job structure
 *
 * @return	int
 * @retval	Accumulated preempt_bits matching the entity
 */
int find_preempt_bits(counts_umap &entity_counts, const std::string &entity_name, resource_resv *rr)
{
	counts *cnt = NULL;
	resource_count *res_c;
	int rc = 0;

	if (entity_name.empty())
	    return rc;

	find_counts_elm(entity_counts, entity_name, NULL, &cnt, NULL);
	if (cnt == NULL)
	    return rc;

	rc |= cnt->soft_limit_preempt_bit;
	for (res_c = cnt->rescts; res_c != NULL; res_c = res_c->next) {
		auto req = find_resource_req(rr->resreq, res_c->def);
		if (req != NULL)
			rc |= res_c->soft_limit_preempt_bit;
	}
	return rc;
}

/**
 * @brief
 * 		check_soft_limits - check the soft limit using soft limit function.
 *
 * @param[in]	si	-	server info.
 * @param[in]	qi	-	queue info
 * @param[in]	rr	-	Resource reservation structure
 *
 * @return	return code for soft limits
 */
int
check_soft_limits(server_info *si, queue_info *qi, resource_resv *rr)
{
	int	rc = 0;

	if (si == NULL || qi == NULL || rr == NULL)
		return 0;

#ifdef NAS /* localmod 097 */
	if (!si->has_soft_limit) {
		return rc;
	}
#endif /* localmod 097 */
	if (si->has_soft_limit) {
		if (si->has_user_limit)
			rc |= find_preempt_bits(si->user_counts, rr->user, rr);
		if (si->has_grp_limit)
			rc |= find_preempt_bits(si->group_counts, rr->group, rr);
		if (si->has_proj_limit)
			rc |= find_preempt_bits(si->project_counts, rr->project, rr);
		if (si->has_all_limit)
			rc |= find_preempt_bits(si->alljobcounts, PBS_ALL_ENTITY, rr);
	}
	if (qi->has_soft_limit) {
		if (qi->has_user_limit)
			rc |= find_preempt_bits(qi->user_counts, rr->user, rr);
		if (qi->has_grp_limit)
			rc |= find_preempt_bits(qi->group_counts, rr->group, rr);
		if (qi->has_proj_limit)
			rc |= find_preempt_bits(qi->project_counts, rr->project, rr);
		if (qi->has_all_limit)
			rc |= find_preempt_bits(qi->alljobcounts, PBS_ALL_ENTITY, rr);
	}

	return (rc);
}

/**
 * @brief
 *		check_server_max_user_run	hard limit checking function for
 *					user server run limits
 *
 * @param[in]	si	-	server_info structure to use for limit evaluation
 * @param[in]	qi	-	queue_info structure to use for limit evaluation
 * @param[in]	rr	-	resource_resv structure to use for limit evaluation
 * @param[in]	sc	-	limcounts struct for server count/total_count maxes over job run
 * @param[in]	qc	-	limcounts struct for queue count/total_count maxes over job run
 * @param[out]	err	-	schd_error structure to return error information
 *
 * @return	integer indicating failing limit test if limit is exceeded
 * @retval	0	: if limit is not exceeded
 * @retval	sched_error_code enum	: if limit is exceeded
 * @retval	SCHD_ERROR	: on error
 *
 * @see	#sched_error_code in constant.h
 */
static int
check_server_max_user_run(server_info *si, queue_info *qi, resource_resv *rr,
	limcounts *sc, limcounts *qc, schd_error *err)
{
	char		*key;
	std::string	user = rr->user;
	int		used;
	int		max_user_run, max_genuser_run;

	if ((si == NULL) || (user.empty()) || (sc == NULL))
		return (SCHD_ERROR);

	if (!si->has_user_limit)
	    return (0);

	auto &cts = sc->user;

	if ((key = entlim_mk_runkey(LIM_USER, user.c_str())) == NULL)
		return (SCHD_ERROR);
	max_user_run = (int) lim_get(key, LI2RUNCTX(si->liminfo));
	free(key);

	if ((key = entlim_mk_runkey(LIM_USER, genparam)) == NULL)
		return (SCHD_ERROR);
	max_genuser_run = (int) lim_get(key, LI2RUNCTX(si->liminfo));
	free(key);

	if ((max_user_run == SCHD_INFINITY) &&
		(max_genuser_run == SCHD_INFINITY))
		return (0);


	/* at this point, we know a generic or individual limit is set */
	used = find_counts_elm(cts, user, NULL, NULL, NULL);
	log_eventf(PBSEVENT_DEBUG4, PBS_EVENTCLASS_JOB, LOG_DEBUG, __func__,
		"user %s max_*user_run (%d, %d), used %d",
		user.c_str(), max_user_run, max_genuser_run, used);

	if (max_user_run != SCHD_INFINITY) {
		if (max_user_run <= used) {
			schderr_args_server(user, err);
			return (SERVER_BYUSER_JOB_LIMIT_REACHED);
		} else
			return (0);	/* ignore a generic limit */
	} else if (max_genuser_run <= used) {
		schderr_args_server(NULL, err);
		return (SERVER_USER_LIMIT_REACHED);
	} else
		return (0);
}
/**
 * @brief
 *		check_server_max_group_run	hard limit checking function for
 *					group server resource limits
 *
 * @param[in]	si	-	server_info structure to use for limit evaluation
 * @param[in]	qi	-	queue_info structure to use for limit evaluation
 * @param[in]	rr	-	resource_resv structure to use for limit evaluation
 * @param[in]	sc	-	limcounts struct for server count/total_count maxes over job run
 * @param[in]	qc	-	limcounts struct for queue count/total_count maxes over job run
 * @param[out]	err	-	schd_error structure to return error information
 *
 * @return	integer indicating failing limit test if limit is exceeded
 * @retval	0	: if limit is not exceeded
 * @retval	sched_error_code enum	: if limit is exceeded
 * @retval	SCHD_ERROR	: on error
 *
 * @see	#sched_error_code in constant.h
 */
static int
check_server_max_group_run(server_info *si, queue_info *qi, resource_resv *rr,
	limcounts *sc, limcounts *qc, schd_error *err)
{
	char		*key;
	std::string	group = rr->group;
	int		used;
	int		max_group_run, max_gengroup_run;

	if ((si == NULL) || (group.empty()) || (sc == NULL))
		return (SCHD_ERROR);

	if (!si->has_grp_limit)
	    return (0);

	auto &cts = sc->group;

	if ((key = entlim_mk_runkey(LIM_GROUP, group.c_str())) == NULL)
		return (SCHD_ERROR);
	max_group_run = (int) lim_get(key, LI2RUNCTX(si->liminfo));
	free(key);

	if ((key = entlim_mk_runkey(LIM_GROUP, genparam)) == NULL)
		return (SCHD_ERROR);
	max_gengroup_run = (int) lim_get(key, LI2RUNCTX(si->liminfo));
	free(key);

	if ((max_group_run == SCHD_INFINITY) &&
		(max_gengroup_run == SCHD_INFINITY))
		return (0);


	/* at this point, we know a generic or individual limit is set */
	used = find_counts_elm(cts, group, NULL, NULL, NULL);
	log_eventf(PBSEVENT_DEBUG4, PBS_EVENTCLASS_JOB, LOG_DEBUG, rr->name,
		"group %s max_*group_run (%d, %d), used %d",
		group.c_str(), max_group_run, max_gengroup_run, used);

	if (max_group_run != SCHD_INFINITY) {
		if (max_group_run <= used) {
			schderr_args_server(group, err);
			return (SERVER_BYGROUP_JOB_LIMIT_REACHED);
		} else
			return (0);	/* ignore a generic limit */
	} else if (max_gengroup_run <= used) {
		schderr_args_server(NULL, err);
		return (SERVER_GROUP_LIMIT_REACHED);
	} else
		return (0);
}

/**
 * @brief
 *		check_server_max_user_res	hard limit checking function for
 *					user server resource limits
 *
 * @param[in]	si	-	server_info structure to use for limit evaluation
 * @param[in]	qi	-	queue_info structure to use for limit evaluation
 * @param[in]	rr	-	resource_resv structure to use for limit evaluation
 * @param[in]	sc	-	limcounts struct for server count/total_count maxes over job run
 * @param[in]	qc	-	limcounts struct for queue count/total_count maxes over job run
 * @param[out]	err	-	schd_error structure to return error information
 *
 * @return	integer indicating failing limit test if limit is exceeded
 * @retval	0	: if limit is not exceeded
 * @retval	sched_error_code enum	: if limit is exceeded
 * @retval	SCHD_ERROR	: on error
 *
 * @see	#sched_error_code in constant.h
 */
static int
check_server_max_user_res(server_info *si, queue_info *qi, resource_resv *rr,
	limcounts *sc, limcounts *qc, schd_error *err)
{
	int		ret;
	resdef		*rdef = NULL;

	if ((si == NULL) || (rr == NULL) ||(sc==NULL))
		return (SCHD_ERROR);

	if (!si->has_user_limit)
	    return (0);

	auto &cts = sc->user;

	ret = check_max_user_res(rr, cts, &rdef,
		LI2RESCTX(si->liminfo));
	if (ret != 0)
		log_eventf(PBSEVENT_DEBUG4, PBS_EVENTCLASS_JOB, LOG_DEBUG, rr->name,
			   "check_max_user_res returned %d", ret);
	switch (ret) {
		default:
		case -1:
			return (SCHD_ERROR);
		case 0:
			return (0);
		case 1:	/* generic user limit exceeded */
			err->rdef = rdef;
			return (SERVER_USER_RES_LIMIT_REACHED);
		case 2:	/* individual user limit exceeded */
			schderr_args_server_res(rr->user, NULL, err);
			err->rdef = rdef;
			return (SERVER_BYUSER_RES_LIMIT_REACHED);
	}
}

/**
 * @brief
 *		check_server_max_group_res	hard limit checking function for
 *					group server resource limits
 *
 * @param[in]	si	-	server_info structure to use for limit evaluation
 * @param[in]	qi	-	queue_info structure to use for limit evaluation
 * @param[in]	rr	-	resource_resv structure to use for limit evaluation
 * @param[in]	sc	-	limcounts struct for server count/total_count maxes over job run
 * @param[in]	qc	-	limcounts struct for queue count/total_count maxes over job run
 * @param[out]	err	-	schd_error structure to return error information
 *
 * @return	integer indicating failing limit test if limit is exceeded
 * @retval	0	: if limit is not exceeded
 * @retval	sched_error_code enum	: if limit is exceeded
 * @retval	SCHD_ERROR	: on error
 *
 * @see		#sched_error_code in constant.h
 */
static int
check_server_max_group_res(server_info *si, queue_info *qi, resource_resv *rr,
	limcounts *sc, limcounts *qc, schd_error *err)
{
	int		ret;
	resdef		*rdef = NULL;

	if ((si == NULL) || (rr == NULL) || (sc == NULL))
		return (SCHD_ERROR);

	if (!si->has_grp_limit)
	    return (0);

	auto &cts = sc->group;

	ret = check_max_group_res(rr, cts,
		&rdef, LI2RESCTX(si->liminfo));
	if (ret != 0)
		log_eventf(PBSEVENT_DEBUG4, PBS_EVENTCLASS_JOB, LOG_DEBUG, rr->name,
			"check_max_group_res returned %d", ret);
	switch (ret) {
		default:
		case -1:
			return (SCHD_ERROR);
		case 0:
			return (0);
		case 1:	/* generic group limit exceeded */
			err->rdef = rdef;
			return (SERVER_GROUP_RES_LIMIT_REACHED);
		case 2:	/* individual group limit exceeded */
			schderr_args_server_res(rr->group, NULL, err);
			err->rdef = rdef;
			return (SERVER_BYGROUP_RES_LIMIT_REACHED);
	}
}

/**
 * @brief
 *		check_queue_max_user_run	hard limit checking function for
 *					user queue run limits
 *
 * @param[in]	si	-	server_info structure to use for limit evaluation
 * @param[in]	qi	-	queue_info structure to use for limit evaluation
 * @param[in]	rr	-	resource_resv structure to use for limit evaluation
 * @param[in]	sc	-	limcounts struct for server count/total_count maxes over job run
 * @param[in]	qc	-	limcounts struct for queue count/total_count maxes over job run
 * @param[out]	err	-	schd_error structure to return error information
 *
 * @return	integer indicating failing limit test if limit is exceeded
 * @retval	0	-	if limit is not exceeded
 * @retval	sched_error_code enum	-	if limit is exceeded
 * @retval	SCHD_ERROR	-	on error
 * @see	#sched_error_code in constant.h
 */
static int
check_queue_max_user_run(server_info *si, queue_info *qi, resource_resv *rr,
	limcounts *sc, limcounts *qc, schd_error *err)
{
	char		*key;
	std::string	user = rr->user;
	int		used;
	int		max_user_run, max_genuser_run;

	if ((qi == NULL) || (user.empty()) || (qc == NULL))
		return (SCHD_ERROR);

	if (!qi->has_user_limit)
	    return (0);

	auto &cts = qc->user;

	if ((key = entlim_mk_runkey(LIM_USER, user.c_str())) == NULL)
		return (SCHD_ERROR);
	max_user_run = (int) lim_get(key, LI2RUNCTX(qi->liminfo));
	free(key);

	if ((key = entlim_mk_runkey(LIM_USER, genparam)) == NULL)
		return (SCHD_ERROR);
	max_genuser_run = (int) lim_get(key, LI2RUNCTX(qi->liminfo));
	free(key);

	if ((max_user_run == SCHD_INFINITY) &&
		(max_genuser_run == SCHD_INFINITY))
		return (0);


	/* at this point, we know a generic or individual limit is set */
	used = find_counts_elm(cts,  user, NULL, NULL, NULL);
	log_eventf(PBSEVENT_DEBUG4, PBS_EVENTCLASS_JOB, LOG_DEBUG, rr->name,
		"user %s max_*user_run (%d, %d), used %d",
		user.c_str(), max_user_run, max_genuser_run, used);

	if (max_user_run != SCHD_INFINITY) {
		if (max_user_run <= used) {
			schderr_args_q(qi->name, user, err);
			return (QUEUE_BYUSER_JOB_LIMIT_REACHED);
		} else
			return (0);	/* ignore a generic limit */
	} else if (max_genuser_run <= used) {
		schderr_args_q(qi->name, NULL, err);
		return (QUEUE_USER_LIMIT_REACHED);
	} else
		return (0);
}

/**
 * @brief
 *		check_queue_max_group_run	hard limit checking function for
 *					group queue run limits
 *
 * @param[in]	si	-	server_info structure to use for limit evaluation
 * @param[in]	qi	-	queue_info structure to use for limit evaluation
 * @param[in]	rr	-	resource_resv structure to use for limit evaluation
 * @param[in]	sc	-	limcounts struct for server count/total_count maxes over job run
 * @param[in]	qc	-	limcounts struct for queue count/total_count maxes over job run
 * @param[out]	err	-	schd_error structure to return error information
 *
 * @return	integer indicating failing limit test if limit is exceeded
 * @retval	0	: if limit is not exceeded
 * @retval	sched_error_code enum	: if limit is exceeded
 * @retval	SCHD_ERROR	: on error
 * @see	#sched_error	: in constant.h
 */
static int
check_queue_max_group_run(server_info *si, queue_info *qi, resource_resv *rr,
	limcounts *sc, limcounts *qc, schd_error *err)
{
	char		*key;
	std::string	group = rr->group;
	int		used;
	int		max_group_run, max_gengroup_run;

	if ((qi == NULL) || (group.empty()) || (qc == NULL))
		return (SCHD_ERROR);

	if (!qi->has_grp_limit)
	    return (0);

	auto &cts = qc->group;

	if ((key = entlim_mk_runkey(LIM_GROUP, group.c_str())) == NULL)
		return (SCHD_ERROR);
	max_group_run = (int) lim_get(key, LI2RUNCTX(qi->liminfo));
	free(key);

	if ((key = entlim_mk_runkey(LIM_GROUP, genparam)) == NULL)
		return (SCHD_ERROR);
	max_gengroup_run = (int) lim_get(key, LI2RUNCTX(qi->liminfo));
	free(key);

	if ((max_group_run == SCHD_INFINITY) &&
		(max_gengroup_run == SCHD_INFINITY))
		return (0);

	/* at this point, we know a generic or individual limit is set */
	used = find_counts_elm(cts, group, NULL, NULL, NULL);
	log_eventf(PBSEVENT_DEBUG4, PBS_EVENTCLASS_JOB, LOG_DEBUG, rr->name,
		"group %s max_*group_run (%d, %d), used %d",
		group.c_str(), max_group_run, max_gengroup_run, used);

	if (max_group_run != SCHD_INFINITY) {
		if (max_group_run <= used) {
			schderr_args_q(qi->name, group, err);
			return (QUEUE_BYGROUP_JOB_LIMIT_REACHED);
		} else
			return (0);	/* ignore a generic limit */
	} else if (max_gengroup_run <= used) {
		schderr_args_q(qi->name, NULL, err);
		return (QUEUE_GROUP_LIMIT_REACHED);
	} else
		return (0);
}

/**
 * @brief
 *		check_queue_max_user_res	hard limit checking function for
 *					user queue resource limits
 *
 * @param[in]	si	-	server_info structure to use for limit evaluation
 * @param[in]	qi	-	queue_info structure to use for limit evaluation
 * @param[in]	rr	-	resource_resv structure to use for limit evaluation
 * @param[in]	sc	-	limcounts struct for server count/total_count maxes over job run
 * @param[in]	qc	-	limcounts struct for queue count/total_count maxes over job run
 * @param[out]	err	-	schd_error structure to return error information
 *
 * @return	integer	: indicating failing limit test if limit is exceeded
 * @retval	0	: if limit is not exceeded
 * @retval	sched_error_code enum	: if limit is exceeded
 * @retval	SCHD_ERROR	: on error
 *
 * @see	#sched_error	: in constant.h
 */
static int
check_queue_max_user_res(server_info *si, queue_info *qi, resource_resv *rr,
	limcounts *sc, limcounts *qc, schd_error *err)
{
	int		ret;
	resdef		*rdef = NULL;

	if ((qi == NULL) || (rr == NULL) || (qc == NULL))
		return (SCHD_ERROR);

	if (!qi->has_user_limit)
	    return (0);

	auto &cts = qc->user;

	ret = check_max_user_res(rr, cts, &rdef, LI2RESCTX(qi->liminfo));
	if (ret != 0)
		log_eventf(PBSEVENT_DEBUG4, PBS_EVENTCLASS_JOB, LOG_DEBUG, rr->name,
			"check_max_user_res returned %d", ret);

	switch (ret) {
		default:
		case -1:
			return (SCHD_ERROR);
		case 0:
			return (0);
		case 1:	/* generic user limit exceeded */
			schderr_args_q_res(qi->name, NULL, NULL, err);
			err->rdef = rdef;
			return (QUEUE_USER_RES_LIMIT_REACHED);
		case 2:	/* individual user limit exceeded */
			schderr_args_q_res(qi->name, rr->user, NULL, err);
			err->rdef = rdef;
			return (QUEUE_BYUSER_RES_LIMIT_REACHED);
	}
}

/**
 * @brief
 *		check_queue_max_group_res	hard limit checking function for
 *					group queue resource limits
 *
 * @param[in]	si	-	server_info structure to use for limit evaluation
 * @param[in]	qi	-	queue_info structure to use for limit evaluation
 * @param[in]	rr	-	resource_resv structure to use for limit evaluation
 * @param[in]	sc	-	limcounts struct for server count/total_count maxes over job run
 * @param[in]	qc	-	limcounts struct for queue count/total_count maxes over job run
 * @param[out]	err	-	schd_error structure to return error information
 *
 * @return	integer indicating failing limit test if limit is exceeded
 * @retval	0	: if limit is not exceeded
 * @retval	sched_error_code enum	: if limit is exceeded
 * @retval	SCHD_ERROR	: on error
 *
 * @see	#sched_error_code in constant.h
 */
static int
check_queue_max_group_res(server_info *si, queue_info *qi, resource_resv *rr,
	limcounts *sc, limcounts *qc, schd_error *err)
{
	int		ret;
	resdef		*rdef = NULL;

	if ((qi == NULL) || (rr == NULL) || (qc == NULL))
		return (SCHD_ERROR);

	if (!qi->has_grp_limit)
	    return (0);

	auto &cts = qc->group;

	ret = check_max_group_res(rr, cts, &rdef, LI2RESCTX(qi->liminfo));
	if (ret != 0)
		log_eventf(PBSEVENT_DEBUG4, PBS_EVENTCLASS_JOB, LOG_DEBUG, rr->name,
			"check_max_group_res returned %d", ret);

	switch (ret) {
		default:
		case -1:
			return (SCHD_ERROR);
		case 0:
			return (0);
		case 1:	/* generic group limit exceeded */
			schderr_args_q_res(qi->name, NULL, NULL, err);
			err->rdef = rdef;
			return (QUEUE_GROUP_RES_LIMIT_REACHED);
		case 2:	/* individual group limit exceeded */
			schderr_args_q_res(qi->name, rr->group, NULL, err);
			err->rdef = rdef;
			return (QUEUE_BYGROUP_RES_LIMIT_REACHED);
	}
}

/**
 * @brief
 *		check_queue_max_res	hard limit checking function for overall queue
 *				resource limits
 *
 * @param[in]	si	-	server_info structure to use for limit evaluation
 * @param[in]	qi	-	queue_info structure to use for limit evaluation
 * @param[in]	rr	-	resource_resv structure to use for limit evaluation
 * @param[in]	sc	-	limcounts struct for server count/total_count maxes over job run
 * @param[in]	qc	-	limcounts struct for queue count/total_count maxes over job run
 * @param[out]	err	-	schd_error structure to return error information
 *
 * @return	integer indicating failing limit test if limit is exceeded
 * @retval	0	: if limit is not exceeded
 * @retval	sched_error_code enum	: if limit is exceeded
 * @retval	SCHD_ERROR	: on error
 *
 * @see	#sched_error_code in constant.h
 */
static int
check_queue_max_res(server_info *si, queue_info *qi, resource_resv *rr,
	limcounts *sc, limcounts *qc, schd_error *err)
{
	char		*reskey;
	sch_resource_t	max_res;
	sch_resource_t	used;
	schd_resource	*res;
	resource_count	*used_res;
	counts		*c;

	if ((qi == NULL) || (rr == NULL))
		return (SCHD_ERROR);

	if (qc == NULL)
		return (0);

	auto &cts = qc->all;

	c = find_counts(cts, PBS_ALL_ENTITY);
	if (c == NULL)
		return (0);

	for (res = limres; res != NULL; res = res->next) {
		resource_req *req;
		if ((req = find_resource_req(rr->resreq, res->def)) == NULL)
			continue;

		if ((reskey = entlim_mk_reskey(LIM_OVERALL, allparam,
			res->name)) == NULL)
			return (SCHD_ERROR);
		max_res = lim_get(reskey, LI2RESCTX(qi->liminfo));
		free(reskey);

		if (max_res == SCHD_INFINITY)
			continue;

		if ((used_res = find_resource_count(c->rescts, res->def)) == NULL)
			used = 0;
		else
			used = used_res->amount;

		log_eventf(PBSEVENT_DEBUG4, PBS_EVENTCLASS_JOB, LOG_DEBUG, rr->name,
			"max_res.%s %.1lf, used %.1lf", res->name, max_res, used);
		if (used + req->amount > max_res) {
			schderr_args_q_res(qi->name, NULL, NULL, err);
			err->rdef = res->def;
			return (QUEUE_RESOURCE_LIMIT_REACHED);
		}
	}

	return (0);
}

/**
 * @brief
 *		check_server_max_res	hard limit checking function for overall server
 *				resource limits
 *
 * @param[in]	si	-	server_info structure to use for limit evaluation
 * @param[in]	qi	-	queue_info structure to use for limit evaluation
 * @param[in]	rr	-	resource_resv structure to use for limit evaluation
 * @param[in]	sc	-	limcounts struct for server count/total_count maxes over job run
 * @param[in]	qc	-	limcounts struct for queue count/total_count maxes over job run
 * @param[out]	err	-	schd_error structure to return error information
 *
 * @return	integer indicating failing limit test if limit is exceeded
 * @retval	0	: if limit is not exceeded
 * @retval	sched_error_code enum	: if limit is exceeded
 * @retval	SCHD_ERROR	: on error
 *
 * @see	#sched_error_code in constant.h
 */
static int
check_server_max_res(server_info *si, queue_info *qi, resource_resv *rr,
	limcounts *sc, limcounts *qc, schd_error *err)
{
	char		*reskey;
	sch_resource_t	max_res;
	sch_resource_t	used;
	schd_resource	*res;
	resource_count	*used_res;
	counts		*c;

	if ((si == NULL) || (rr == NULL))
		return (SCHD_ERROR);

	if (sc == NULL)
		return (0);

	auto &cts = sc->all;

	c = find_counts(cts, PBS_ALL_ENTITY);
	if (c == NULL)
		return (0);

	for (res = limres; res != NULL; res = res->next) {
		resource_req *req;

		if ((req = find_resource_req(rr->resreq, res->def)) == NULL)
			continue;

		if ((reskey = entlim_mk_reskey(LIM_OVERALL, allparam,
			res->name)) == NULL)
			return (SCHD_ERROR);
		max_res = lim_get(reskey, LI2RESCTX(si->liminfo));
		free(reskey);

		if (max_res == SCHD_INFINITY)
			continue;

		if ((used_res = find_resource_count(c->rescts, res->def)) == NULL)
			used = 0;
		else
			used = used_res->amount;

		log_eventf(PBSEVENT_DEBUG4, PBS_EVENTCLASS_JOB, LOG_DEBUG, rr->name,
			"max_res.%s %.1lf, used %.1lf", res->name, max_res, used);
		if (used + req->amount > max_res) {
			err->rdef = res->def;
			return (SERVER_RESOURCE_LIMIT_REACHED);
		}
	}

	return (0);
}

/**
 * @brief
 *		check_server_max_run	hard limit checking function for
 *				server run limits
 *
 * @param[in]	si	-	server_info structure to use for limit evaluation
 * @param[in]	qi	-	queue_info structure to use for limit evaluation
 * @param[in]	rr	-	resource_resv structure to use for limit evaluation
 * @param[in]	sc	-	limcounts struct for server count/total_count maxes over job run
 * @param[in]	qc	-	limcounts struct for queue count/total_count maxes over job run
 * @param[out]	err	-	schd_error structure to return error information
 *
 * @return	integer indicating failing limit test if limit is exceeded
 * @retval	0	: if limit is not exceeded
 * @retval	sched_error_code enum	: if limit is exceeded
 * @retval	SCHD_ERROR	: on error
 *
 * @see	#sched_error_code in constant.h
 */
static int
check_server_max_run(server_info *si, queue_info *qi, resource_resv *rr,
	limcounts *sc, limcounts *qc, schd_error *err)
{
	int	max_running;
	char	*key;
	int	running;

	if (si == NULL)
		return (SCHD_ERROR);

	if (sc == NULL)
		return (0);

	auto &cts = sc->all;

	if ((key = entlim_mk_runkey(LIM_OVERALL, allparam)) == NULL)
		return (SCHD_ERROR);
	max_running = (int) lim_get(key, LI2RUNCTX(si->liminfo));
	free(key);


	running = find_counts_elm(cts, PBS_ALL_ENTITY, NULL, NULL, NULL);

	if ((max_running == SCHD_INFINITY) ||
		(max_running > running))
		return (0);
	else {
		schderr_args_server(NULL, err);
		return (SERVER_JOB_LIMIT_REACHED);
	}

}

/**
 * @brief
 *		check_queue_max_run	hard limit checking function for
 *				queue run limits
 *
 * @param[in]	si	-	server_info structure to use for limit evaluation
 * @param[in]	qi	-	queue_info structure to use for limit evaluation
 * @param[in]	rr	-	resource_resv structure to use for limit evaluation
 * @param[in]	sc	-	limcounts struct for server count/total_count maxes over job run
 * @param[in]	qc	-	limcounts struct for queue count/total_count maxes over job run
 * @param[out]	err	-	schd_error structure to return error information
 *
 * @return	integer indicating failing limit test if limit is exceeded
 * @retval	0	: if limit is not exceeded
 * @retval	sched_error_code enum	: if limit is exceeded
 * @retval	SCHD_ERROR	: on error
 *
 * @see	#sched_error_code in constant.h
 */
static int
check_queue_max_run(server_info *si, queue_info *qi, resource_resv *rr,
	limcounts *sc, limcounts *qc, schd_error *err)
{
	int	max_running;
	char	*key;
	int	running;

	if (qi == NULL)
		return (SCHD_ERROR);

	if (qc == NULL)
		return (0);

	auto &cts = qc->all;

	if ((key = entlim_mk_runkey(LIM_OVERALL, allparam)) == NULL)
		return (SCHD_ERROR);

	max_running = (int) lim_get(key, LI2RUNCTX(qi->liminfo));
	free(key);


	running = find_counts_elm(cts, PBS_ALL_ENTITY, NULL, NULL, NULL);

	if ((max_running == SCHD_INFINITY) ||
		(max_running > running))
		return (0);
	else {
		schderr_args_q(qi->name, NULL, err);
		return (QUEUE_JOB_LIMIT_REACHED);
	}

}

/**
 * @brief
 *		check_queue_max_run_soft	soft limit checking function for
 *					queue run limits
 *
 * @param[in]	si	-	server_info structure to use for limit evaluation
 * @param[in]	qi	-	queue_info structure to use for limit evaluation
 * @param[in]	rr	-	resource_resv structure to use for limit evaluation
 *
 * @return	integer indicating failing limit test if limit is exceeded
 * @retval	0	: if limit is not exceeded
 * @retval	PREEMPT_TO_BIT(preempt enum)	: if limit is exceeded
 * @retval	PREEMPT_TO_BIT(PREEMPT_ERR)	: on error
 *
 * @see		#preempt enum in constant.h
 */
static int
check_queue_max_run_soft(server_info *si, queue_info *qi, resource_resv *rr)
{
	int	max_running;
	char	*key;
	counts	*cnt = NULL;
	int used = 0;


	if (qi == NULL)
		return (PREEMPT_TO_BIT(PREEMPT_ERR));

	if (!qi->has_all_limit)
	    return (0);

	if ((key = entlim_mk_runkey(LIM_OVERALL, allparam)) == NULL)
		return (PREEMPT_TO_BIT(PREEMPT_ERR));
	max_running = (int) lim_get(key, LI2RUNCTXSOFT(qi->liminfo));
	free(key);

	/* at this point, we know a limit is set for PBS_ALL*/
	used = find_counts_elm(qi->alljobcounts, PBS_ALL_ENTITY, NULL, &cnt, NULL);
	if (max_running != SCHD_INFINITY && used > max_running) {
		if (cnt != NULL)
			cnt->soft_limit_preempt_bit = PREEMPT_TO_BIT(PREEMPT_OVER_QUEUE_LIMIT);
		return (PREEMPT_TO_BIT(PREEMPT_OVER_QUEUE_LIMIT));
	} else {
		if (cnt != NULL)
			cnt->soft_limit_preempt_bit = 0;
		return (0);
	}

}

/**
 * @brief
 *		check_queue_max_user_run_soft	soft limit checking function for
 *					user queue run limits
 *
 * @param[in]	si	-	server_info structure to use for limit evaluation
 * @param[in]	qi	-	queue_info structure to use for limit evaluation
 * @param[in]	rr	-	resource_resv structure to use for limit evaluation
 *
 * @return	integer indicating failing limit test if limit is exceeded
 * @retval	0	: if limit is not exceeded
 * @retval	PREEMPT_TO_BIT(preempt enum)	: if limit is exceeded
 * @retval	PREEMPT_TO_BIT(PREEMPT_ERR)	: on error
 *
 * @see	#preempt enum in constant.h
 */
static int
check_queue_max_user_run_soft(server_info *si, queue_info *qi, resource_resv *rr)
{
	char		*key;
	std::string	user = rr->user;
	int		used;
	int		max_user_run_soft, max_genuser_run_soft;
	counts		*cnt = NULL;

	if ((qi == NULL) || (user.empty()))
		return (PREEMPT_TO_BIT(PREEMPT_ERR));

	if (!qi->has_user_limit)
	    return (0);

	if ((key = entlim_mk_runkey(LIM_USER, user.c_str())) == NULL)
		return (PREEMPT_TO_BIT(PREEMPT_ERR));
	max_user_run_soft = (int) lim_get(key, LI2RUNCTXSOFT(qi->liminfo));
	free(key);

	if ((key = entlim_mk_runkey(LIM_USER, genparam)) == NULL)
		return (PREEMPT_TO_BIT(PREEMPT_ERR));
	max_genuser_run_soft = (int) lim_get(key, LI2RUNCTXSOFT(qi->liminfo));
	free(key);

	if ((max_user_run_soft == SCHD_INFINITY) &&
		(max_genuser_run_soft == SCHD_INFINITY))
		return (0);

	/* at this point, we know a generic or individual limit is set */
	used = find_counts_elm(qi->user_counts, user, NULL, &cnt, NULL);

	log_eventf(PBSEVENT_DEBUG4, PBS_EVENTCLASS_JOB, LOG_DEBUG, rr->name,
		"user %s max_*user_run_soft (%d, %d), used %d",
		user.c_str(), max_user_run_soft, max_genuser_run_soft, used);

	if (max_user_run_soft != SCHD_INFINITY) {
		if (max_user_run_soft < used) {
			if (cnt != NULL)
				cnt->soft_limit_preempt_bit = PREEMPT_TO_BIT(PREEMPT_OVER_QUEUE_LIMIT);
			return (PREEMPT_TO_BIT(PREEMPT_OVER_QUEUE_LIMIT));
		} else {
			if (cnt != NULL)
				cnt->soft_limit_preempt_bit = 0;
			return (0);	/* ignore a generic limit */
		}
	} else if (max_genuser_run_soft < used) {
		if (cnt != NULL)
			cnt->soft_limit_preempt_bit = PREEMPT_TO_BIT(PREEMPT_OVER_QUEUE_LIMIT);
		return (PREEMPT_TO_BIT(PREEMPT_OVER_QUEUE_LIMIT));
	} else {
		if (cnt != NULL)
			cnt->soft_limit_preempt_bit = 0;
		return (0);
	}
}

/**
 * @brief
 *		check_queue_max_group_run_soft	soft limit checking function for
 *					group queue run limits
 *
 * @param[in]	si	-	server_info structure to use for limit evaluation
 * @param[in]	qi	-	queue_info structure to use for limit evaluation
 * @param[in]	rr	-	resource_resv structure to use for limit evaluation
 *
 * @return	integer indicating failing limit test if limit is exceeded
 * @retval	0	: if limit is not exceeded
 * @retval	PREEMPT_TO_BIT(preempt enum)	: if limit is exceeded
 * @retval	PREEMPT_TO_BIT(PREEMPT_ERR)	: on error
 *
 * @see	#preempt enum in constant.h
 */
static int
check_queue_max_group_run_soft(server_info *si, queue_info *qi,
	resource_resv *rr)
{
	char		*key;
	std::string	group = rr->group;
	int		used;
	int		max_group_run_soft, max_gengroup_run_soft;
	counts		*cnt = NULL;

	if ((qi == NULL) || (group.empty()))
		return (PREEMPT_TO_BIT(PREEMPT_ERR));

	if (!qi->has_grp_limit)
	    return (0);

	if ((key = entlim_mk_runkey(LIM_GROUP, group.c_str())) == NULL)
		return (PREEMPT_TO_BIT(PREEMPT_ERR));
	max_group_run_soft = (int) lim_get(key, LI2RUNCTXSOFT(qi->liminfo));
	free(key);

	if ((key = entlim_mk_runkey(LIM_GROUP, genparam)) == NULL)
		return (PREEMPT_TO_BIT(PREEMPT_ERR));
	max_gengroup_run_soft = (int) lim_get(key, LI2RUNCTXSOFT(qi->liminfo));
	free(key);

	if ((max_group_run_soft == SCHD_INFINITY) &&
		(max_gengroup_run_soft == SCHD_INFINITY))
		return (0);

	used = find_counts_elm(qi->group_counts, group, NULL, &cnt, NULL);
	log_eventf(PBSEVENT_DEBUG4, PBS_EVENTCLASS_JOB, LOG_DEBUG, rr->name,
		"group %s max_*group_run_soft (%d, %d), used %d",
		group.c_str(), max_group_run_soft, max_gengroup_run_soft, used);

	if (max_group_run_soft != SCHD_INFINITY) {
		if (max_group_run_soft < used) {
			if (cnt != NULL)
				cnt->soft_limit_preempt_bit = PREEMPT_TO_BIT(PREEMPT_OVER_QUEUE_LIMIT);
			return (PREEMPT_TO_BIT(PREEMPT_OVER_QUEUE_LIMIT));
		}
		else {
			if (cnt != NULL)
				cnt->soft_limit_preempt_bit = 0;
			return (0);	/* ignore a generic limit */
		}
	} else if (max_gengroup_run_soft < used) {
		if (cnt != NULL)
			cnt->soft_limit_preempt_bit = PREEMPT_TO_BIT(PREEMPT_OVER_QUEUE_LIMIT);
		return (PREEMPT_TO_BIT(PREEMPT_OVER_QUEUE_LIMIT));
	} else {
		if (cnt != NULL)
			cnt->soft_limit_preempt_bit = 0;
		return (0);
	}
}

/**
 * @brief
 *		check_queue_max_user_res_soft	soft limit checking function for
 *					user queue resource limits
 *
 * @param[in]	si	-	server_info structure to use for limit evaluation
 * @param[in]	qi	-	queue_info structure to use for limit evaluation
 * @param[in]	rr	-	resource_resv structure to use for limit evaluation
 *
 * @return	integer indicating failing limit test if limit is exceeded
 * @retval	0	: if limit is not exceeded
 * @retval	PREEMPT_TO_BIT(preempt enum)	: if limit is exceeded
 * @retval	PREEMPT_TO_BIT(PREEMPT_ERR)	: on error
 *
 * @see		#preempt enum in constant.h
 */
static int
check_queue_max_user_res_soft(server_info *si, queue_info *qi, resource_resv *rr)
{
	if ((qi == NULL) || (rr == NULL))
		return (PREEMPT_TO_BIT(PREEMPT_ERR));

	if (!qi->has_user_limit)
	    return (0);

	return (check_max_user_res_soft(qi->running_jobs, rr, qi->user_counts,
		LI2RESCTXSOFT(qi->liminfo), PREEMPT_TO_BIT(PREEMPT_OVER_QUEUE_LIMIT)));
}

/**
 * @brief
 *		check_queue_max_group_res_soft	soft limit checking function for
 *					group queue resource limits
 *
 * @param[in]	si	-	server_info structure to use for limit evaluation
 * @param[in]	qi	-	queue_info structure to use for limit evaluation
 * @param[in]	rr	-	resource_resv structure to use for limit evaluation
 *
 * @return	integer indicating failing limit test if limit is exceeded
 * @retval	0	: if limit is not exceeded
 * @retval	PREEMPT_TO_BIT(preempt enum)	: if limit is exceeded
 * @retval	PREEMPT_TO_BIT(PREEMPT_ERR)	: on error
 *
 * @see	#preempt enum in constant.h
 */
static int
check_queue_max_group_res_soft(server_info *si, queue_info *qi,
	resource_resv *rr)
{
	if ((qi == NULL) || (rr == NULL))
		return (PREEMPT_TO_BIT(PREEMPT_ERR));

	if (!qi->has_grp_limit)
	    return (0);

	return (check_max_group_res_soft(rr, qi->group_counts,
		LI2RESCTXSOFT(qi->liminfo), PREEMPT_TO_BIT(PREEMPT_OVER_QUEUE_LIMIT)));
}

/**
 * @brief
 *		check_server_max_run_soft	soft limit checking function for
 *					server run limits
 *
 * @param[in]	si	-	server_info structure to use for limit evaluation
 * @param[in]	qi	-	queue_info structure to use for limit evaluation
 * @param[in]	rr	-	resource_resv structure to use for limit evaluation
 *
 * @return	integer indicating failing limit test if limit is exceeded
 * @retval	0	: if limit is not exceeded
 * @retval	PREEMPT_TO_BIT(preempt enum)	: if limit is exceeded
 * @retval	PREEMPT_TO_BIT(PREEMPT_ERR)	: on error
 *
 * @see	#preempt enum in constant.h
 */
static int
check_server_max_run_soft(server_info *si, queue_info *qi, resource_resv *rr)
{
	int	max_running;
	char	*key;
	counts	*cnt = NULL;
	int used = 0;

	if (si == NULL)
		return (PREEMPT_TO_BIT(PREEMPT_ERR));

	if (!si->has_all_limit)
	    return (0);

	if ((key = entlim_mk_runkey(LIM_OVERALL, allparam)) == NULL)
		return (PREEMPT_TO_BIT(PREEMPT_ERR));
	max_running = (int) lim_get(key, LI2RUNCTXSOFT(si->liminfo));
	free(key);

	/* at this point, we know a limit is set for PBS_ALL*/
	used = find_counts_elm(si->alljobcounts, PBS_ALL_ENTITY, NULL, &cnt, NULL);
	if (max_running != SCHD_INFINITY && used > max_running) {
		if (cnt != NULL)
			cnt->soft_limit_preempt_bit = PREEMPT_TO_BIT(PREEMPT_OVER_SERVER_LIMIT);
		return (PREEMPT_TO_BIT(PREEMPT_OVER_SERVER_LIMIT));
	} else {
		if (cnt != NULL)
			cnt->soft_limit_preempt_bit = 0;
		return (0);
	}
}

/**
 * @brief
 *		check_server_max_user_run_soft	soft limit checking function for
 *					user server run limits
 *
 * @param [in]	si	-	server_info structure to use for limit evaluation
 * @param [in]	qi	-	queue_info structure to use for limit evaluation
 * @param [in]	rr	-	resource_resv structure to use for limit evaluation
 *
 * @return	integer indicating failing limit test if limit is exceeded
 * @retval	0	: if limit is not exceeded
 * @retval	PREEMPT_TO_BIT(preempt enum)	: if limit is exceeded
 * @retval	PREEMPT_TO_BIT(PREEMPT_ERR)	: on error
 *
 * @see	#preempt enum in constant.h
 */
static int
check_server_max_user_run_soft(server_info *si, queue_info *qi,
	resource_resv *rr)
{
	char		*key;
	std::string	user = rr->user;
	int		used;
	int		max_user_run_soft, max_genuser_run_soft;
	counts		*cnt = NULL;

	if ((si == NULL) || (user.empty()))
		return (PREEMPT_TO_BIT(PREEMPT_ERR));

	if (!si->has_user_limit)
	    return (0);

	if ((key = entlim_mk_runkey(LIM_USER, user.c_str())) == NULL)
		return (PREEMPT_TO_BIT(PREEMPT_ERR));
	max_user_run_soft = (int) lim_get(key, LI2RUNCTXSOFT(si->liminfo));
	free(key);

	if ((key = entlim_mk_runkey(LIM_USER, genparam)) == NULL)
		return (PREEMPT_TO_BIT(PREEMPT_ERR));
	max_genuser_run_soft = (int) lim_get(key, LI2RUNCTXSOFT(si->liminfo));
	free(key);

	if ((max_user_run_soft == SCHD_INFINITY) &&
		(max_genuser_run_soft == SCHD_INFINITY))
		return (0);

	/* at this point, we know a generic or individual limit is set */
	used = find_counts_elm(si->user_counts, user, NULL, &cnt, NULL);
	log_eventf(PBSEVENT_DEBUG4, PBS_EVENTCLASS_JOB, LOG_DEBUG, rr->name,
		"user %s max_*user_run_soft (%d, %d), used %d",
		user.c_str(), max_user_run_soft, max_genuser_run_soft, used);

	if (max_user_run_soft != SCHD_INFINITY) {
		if (max_user_run_soft < used) {
			if (cnt != NULL)
				cnt->soft_limit_preempt_bit = PREEMPT_TO_BIT(PREEMPT_OVER_SERVER_LIMIT);
			return (PREEMPT_TO_BIT(PREEMPT_OVER_SERVER_LIMIT));
		} else {
			if (cnt != NULL)
				cnt->soft_limit_preempt_bit = 0;
			return (0);	/* ignore a generic limit */
		}
	} else if (max_genuser_run_soft < used) {
		if (cnt != NULL)
			cnt->soft_limit_preempt_bit = PREEMPT_TO_BIT(PREEMPT_OVER_SERVER_LIMIT);
		return ((PREEMPT_TO_BIT(PREEMPT_OVER_SERVER_LIMIT)));
	} else {
		if (cnt != NULL)
			cnt->soft_limit_preempt_bit = 0;
		return (0);
	}
}

/**
 * @brief
 *		check_server_max_group_run_soft	soft limit checking function for
 *					group server run limits
 *
 * @param[in]	si	-	server_info structure to use for limit evaluation
 * @param[in]	qi	-	queue_info structure to use for limit evaluation
 * @param[in]	rr	-	resource_resv structure to use for limit evaluation
 *
 * @return	integer indicating failing limit test if limit is exceeded
 * @retval	0	: if limit is not exceeded
 * @retval	PREEMPT_TO_BIT(preempt enum)	: if limit is exceeded
 * @retval	PREEMPT_TO_BIT(PREEMPT_ERR)	: on error
 *
 * @see	#preempt enum in constant.h
 */
static int
check_server_max_group_run_soft(server_info *si, queue_info *qi,
	resource_resv *rr)
{
	char		*key;
	std::string	group = rr->group;
	int		used;
	int		max_group_run_soft, max_gengroup_run_soft;
	counts		*cnt = NULL;

	if ((si == NULL) || (group.empty()))
		return (PREEMPT_TO_BIT(PREEMPT_ERR));

	if (!si->has_grp_limit)
	    return (0);

	if ((key = entlim_mk_runkey(LIM_GROUP, group.c_str())) == NULL)
		return (PREEMPT_TO_BIT(PREEMPT_ERR));
	max_group_run_soft = (int) lim_get(key, LI2RUNCTXSOFT(si->liminfo));
	free(key);

	if ((key = entlim_mk_runkey(LIM_GROUP, genparam)) == NULL)
		return (PREEMPT_TO_BIT(PREEMPT_ERR));
	max_gengroup_run_soft = (int) lim_get(key, LI2RUNCTXSOFT(si->liminfo));
	free(key);

	if ((max_group_run_soft == SCHD_INFINITY) &&
		(max_gengroup_run_soft == SCHD_INFINITY))
		return (0);

	/* at this point, we know a generic or individual limit is set */
	used = find_counts_elm(si->group_counts, group, NULL, &cnt, NULL);

	log_eventf(PBSEVENT_DEBUG4, PBS_EVENTCLASS_JOB, LOG_DEBUG, rr->name,
		"group %s max_*group_run_soft (%d, %d), used %d",
		group.c_str(), max_group_run_soft, max_gengroup_run_soft, used);

	if (max_group_run_soft != SCHD_INFINITY) {
		if (max_group_run_soft < used) {
			if (cnt != NULL)
				cnt->soft_limit_preempt_bit = PREEMPT_TO_BIT(PREEMPT_OVER_SERVER_LIMIT);
			return (PREEMPT_TO_BIT(PREEMPT_OVER_SERVER_LIMIT));
		} else {
			if (cnt != NULL)
				cnt->soft_limit_preempt_bit = 0;
			return (0);	/* ignore a generic limit */
		}
	} else if (max_gengroup_run_soft < used) {
		if (cnt != NULL)
			cnt->soft_limit_preempt_bit = PREEMPT_TO_BIT(PREEMPT_OVER_SERVER_LIMIT);
		return (PREEMPT_TO_BIT(PREEMPT_OVER_SERVER_LIMIT));
	} else {
		if (cnt != NULL)
			cnt->soft_limit_preempt_bit = 0;
		return (0);
	}
}

/**
 * @brief
 *		check_server_max_user_res_soft	soft limit checking function for
 *					user server resource limits
 *
 * @param[in]	si	-	server_info structure to use for limit evaluation
 * @param [in]	qi	-	queue_info structure to use for limit evaluation
 * @param [in]	rr	-	resource_resv structure to use for limit evaluation
 *
 * @return	integer indicating failing limit test if limit is exceeded
 * @retval	0	: if limit is not exceeded
 * @retval	PREEMPT_TO_BIT(preempt enum)	: if limit is exceeded
 * @retval	PREEMPT_TO_BIT(PREEMPT_ERR)	: on error
 *
 * @see	#preempt enum in constant.h
 */
static int
check_server_max_user_res_soft(server_info *si, queue_info *qi,
	resource_resv *rr)
{
	if ((si == NULL) || (rr == NULL))
		return (PREEMPT_TO_BIT(PREEMPT_ERR));

	if (!si->has_user_limit)
	    return (0);

	return (check_max_user_res_soft(si->running_jobs, rr, si->user_counts,
		LI2RESCTXSOFT(si->liminfo), PREEMPT_TO_BIT(PREEMPT_OVER_SERVER_LIMIT)));
}

/**
 * @brief
 * 		check_server_max_group_res_soft	soft limit checking function for
 *					group server resource limits
 *
 * @param[in]	si	-	server_info structure to use for limit evaluation
 * @param[in]	qi	-	queue_info structure to use for limit evaluation
 * @param[in]	rr	-	resource_resv structure to use for limit evaluation
 *
 * @return	integer indicating failing limit test if limit is exceeded
 * @retval	0	: if limit is not exceeded
 * @retval	PREEMPT_TO_BIT(preempt enum)	: if limit is exceeded
 * @retval	PREEMPT_TO_BIT(PREEMPT_ERR)	: on error
 *
 * @see	#preempt enum in constant.h
 */
static int
check_server_max_group_res_soft(server_info *si, queue_info *qi,
	resource_resv *rr)
{
	if ((si == NULL) || (rr == NULL))
		return (PREEMPT_TO_BIT(PREEMPT_ERR));

	if (!si->has_grp_limit)
	    return (0);

	return (check_max_group_res_soft(rr, si->group_counts,
		LI2RESCTXSOFT(si->liminfo), PREEMPT_TO_BIT(PREEMPT_OVER_SERVER_LIMIT)));
}

/**
 * @brief
 *		check_server_max_res_soft	soft limit checking function for overall
 *					server resource limits
 *
 * @param[in]	si	-	server_info structure to use for limit evaluation
 * @param[in]	qi	-	queue_info structure to use for limit evaluation
 * @param[in]	rr	-	resource_resv structure to use for limit evaluation
 *
 * @return	integer indicating failing limit test if limit is exceeded
 * @retval	0	: if limit is not exceeded
 * @retval	PREEMPT_TO_BIT(preempt enum)	: if limit is exceeded
 * @retval	PREEMPT_TO_BIT(PREEMPT_ERR)	: on error
 * @see	#preempt enum in constant.h
 */
static int
check_server_max_res_soft(server_info *si, queue_info *qi, resource_resv *rr)
{
	char		*reskey;
	sch_resource_t	max_res_soft;
	sch_resource_t	used;
	schd_resource	*res;
	resource_count	*used_res;
	counts		*c;

	if ((si == NULL) || (rr == NULL))
		return (PREEMPT_TO_BIT(PREEMPT_ERR));

	c = find_counts(si->alljobcounts, PBS_ALL_ENTITY);
	if (c == NULL)
		return (0);

	for (res = limres; res != NULL; res = res->next) {
		/* If the job is not requesting the limit resource, it is not over its soft limit*/
		if (find_resource_req(rr->resreq, res->def) == NULL)
			continue;

		if ((reskey = entlim_mk_reskey(LIM_OVERALL, allparam,
			res->name)) == NULL)
			return (PREEMPT_TO_BIT(PREEMPT_ERR));
		max_res_soft = lim_get(reskey, LI2RESCTXSOFT(si->liminfo));
		free(reskey);

		if (max_res_soft == SCHD_INFINITY)
			continue;

		if ((used_res = find_resource_count(c->rescts, res->def)) == NULL)
			used = 0;
		else
			used = used_res->amount;

		log_eventf(PBSEVENT_DEBUG4, PBS_EVENTCLASS_JOB, LOG_DEBUG, rr->name,
			"max_res_soft.%s %.1lf, used %.1lf",
			res->name, max_res_soft, used);

		if (max_res_soft < used) {
			if (used_res != NULL)
				used_res->soft_limit_preempt_bit = PREEMPT_TO_BIT(PREEMPT_OVER_SERVER_LIMIT);
			return (PREEMPT_TO_BIT(PREEMPT_OVER_SERVER_LIMIT));
		} else {
			if (used_res != NULL)
				used_res->soft_limit_preempt_bit = 0;
		}
	}

	return (0);
}

/**
 * @brief
 *		check_queue_max_res_soft	soft limit checking function for overall
 *					queue resource limits
 *
 * @param[in]	si	-	server_info structure to use for limit evaluation
 * @param[in]	qi	-	queue_info structure to use for limit evaluation
 * @param[in]	rr	-	resource_resv structure to use for limit evaluation
 *
 * @return	integer indicating failing limit test if limit is exceeded
 * @retval	0	: if limit is not exceeded
 * @retval	PREEMPT_TO_BIT(preempt enum)	: if limit is exceeded
 * @retval	PREEMPT_TO_BIT(PREEMPT_ERR)	: on error
 *
 * @see	#preempt enum in constant.h
 */
static int
check_queue_max_res_soft(server_info *si, queue_info *qi, resource_resv *rr)
{
	char		*reskey;
	sch_resource_t	max_res_soft;
	sch_resource_t	used;
	schd_resource	*res;
	resource_count	*used_res;
	counts		*c;

	if ((qi == NULL) || (rr == NULL))
		return (PREEMPT_TO_BIT(PREEMPT_ERR));

	c = find_counts(qi->alljobcounts, PBS_ALL_ENTITY);
	if (c == NULL)
		return (0);

	for (res = limres; res != NULL; res = res->next) {
		/* If the job is not requesting the limit resource, it is not over its soft limit*/
		if (find_resource_req(rr->resreq, res->def) == NULL)
			continue;

		if ((reskey = entlim_mk_reskey(LIM_OVERALL, allparam,
			res->name)) == NULL)
			return (PREEMPT_TO_BIT(PREEMPT_ERR));
		max_res_soft = lim_get(reskey, LI2RESCTXSOFT(qi->liminfo));
		free(reskey);

		if (max_res_soft == SCHD_INFINITY)
			continue;

		if ((used_res = find_resource_count(c->rescts, res->def)) == NULL)
			used = 0;
		else
			used = used_res->amount;

		log_eventf(PBSEVENT_DEBUG4, PBS_EVENTCLASS_JOB, LOG_DEBUG, rr->name,
			"max_res_soft.%s %.1lf, used %.1lf",
			res->name, max_res_soft, used);

		if (max_res_soft < used) {
			if (used_res != NULL)
				used_res->soft_limit_preempt_bit = PREEMPT_TO_BIT(PREEMPT_OVER_QUEUE_LIMIT);
			return (PREEMPT_TO_BIT(PREEMPT_OVER_QUEUE_LIMIT));
		} else {
			if (used_res != NULL)
				used_res->soft_limit_preempt_bit = 0;
		}
	}

	return (0);
}

/**
 * @brief
 *		check_max_group_res	check to see whether the user can run a
 *				resource resv and still be within group max
 *				resource limits
 *
 * @param[in]	rr	-	resource_resv to run
 * @param[in]	cts_list	-	the user counts list
 * @param[out]  rdef -		resource definition of resource exceeding a limit
 * @param[in]	limitctx	-	the limit storage context
 *
 * @return	int
 * @retval	0	: if the group would be under or at its limits
 * @retval	1	: if a generic group limit would be exceeded
 * @retval	2	: if an individual group limit would be exceeded
 * @retval	-1	: on error
 */
static int
check_max_group_res(resource_resv *rr, counts_umap &cts_list,
	resdef **rdef, void *limitctx)
{
	char		*groupreskey;
	char		*gengroupreskey;
	std::string	group = rr->group;
	schd_resource	*res;
	sch_resource_t	max_group_res;
	sch_resource_t	max_gengroup_res;
	sch_resource_t	used = 0;

	if (rr == NULL)
		return (-1);
	if ((limres == NULL) || (rr->resreq == NULL))
		return (0);

	group = rr->group;

	for (res = limres; res != NULL; res = res->next) {
		resource_req *req;
		if ((req = find_resource_req(rr->resreq, res->def)) == NULL)
			continue;

		/* individual group limit check */
		if ((groupreskey = entlim_mk_reskey(LIM_GROUP, group.c_str(),
			res->name)) == NULL)
			return (-1);
		max_group_res = lim_get(groupreskey, limitctx);
		free(groupreskey);

		/* generic group limit check */
		if ((gengroupreskey = lim_gengroupreskey(res->name)) == NULL)
			return (-1);
		max_gengroup_res = lim_get(gengroupreskey, limitctx);
		free(gengroupreskey);

		if ((max_group_res == SCHD_INFINITY) &&
			(max_gengroup_res == SCHD_INFINITY))
			continue;

		/* at this point, we know a generic or individual limit is set */
		used = find_counts_elm(cts_list, group, res->def, NULL, NULL);

		log_eventf(PBSEVENT_DEBUG4, PBS_EVENTCLASS_JOB, LOG_DEBUG, rr->name,
			"group %s max_*group_res.%s (%.1lf, %.1lf), used %.1lf",
			group.c_str(), res->name, max_group_res, max_gengroup_res, used);

		if (max_group_res != SCHD_INFINITY) {
			if (used + req->amount > max_group_res) {
				*rdef = res->def;
				return (2);
			} else
				continue;	/* ignore a generic limit */
		} else if (used + req->amount > max_gengroup_res) {
			*rdef = res->def;
			return (1);
		}
	}

	return (0);
}

/**
 * @brief
 *		check_max_group_res_soft	check to see whether the user can run a
 *					resource resv and still be within group
 *					max resource limits
 *
 * @param[in]	rr	-	resource_resv to run
 * @param[in]	cts_list	-	the user counts list
 * @param[in]	limitctx	-	the limit storage context
 * @param[in]	preempt_bit	-	preempt bit value to set and return if limit is exceeded
 *
 * @return	int
 * @retval	0	: if the group would be under or at its limits
 * @retval	1	: if a generic group limit would be exceeded
 * @retval	-1	: on error
 */
static int
check_max_group_res_soft(resource_resv *rr, counts_umap &cts_list, void *limitctx, int preempt_bit)
{
	char		*groupreskey;
	char		*gengroupreskey;
	std::string	group = rr->group;
	schd_resource	*res;
	sch_resource_t	max_group_res_soft;
	sch_resource_t	max_gengroup_res_soft;
	sch_resource_t	used = 0;
	resource_count  *rescts;
	int		rc = 0;

	if (rr == NULL)
		return (-1);
	if ((limres == NULL) || (rr->resreq == NULL))
		return (0);

	group = rr->group;

	for (res = limres; res != NULL; res = res->next) {
		/* If the job is not requesting the limit resource, it is not over its soft limit*/
		if (find_resource_req(rr->resreq, res->def) == NULL)
			continue;

		/* individual group limit check */
		if ((groupreskey = entlim_mk_reskey(LIM_GROUP, group.c_str(),
			res->name)) == NULL)
			return (-1);
		max_group_res_soft = lim_get(groupreskey, limitctx);
		free(groupreskey);

		/* generic group limit check */
		if ((gengroupreskey = lim_gengroupreskey(res->name)) == NULL)
			return (-1);
		max_gengroup_res_soft = lim_get(gengroupreskey, limitctx);
		free(gengroupreskey);

		if ((max_group_res_soft == SCHD_INFINITY) &&
			(max_gengroup_res_soft == SCHD_INFINITY))
			continue;

		rescts = NULL;
		/* at this point, we know a generic or individual limit is set */
		used = find_counts_elm(cts_list, group, res->def, NULL, &rescts);
		log_eventf(PBSEVENT_DEBUG4, PBS_EVENTCLASS_JOB, LOG_DEBUG, rr->name,
			"group %s max_*group_res_soft.%s (%.1lf, %.1lf), used %.1lf",
			group.c_str(), res->name, max_group_res_soft, max_gengroup_res_soft, used);

		if (max_group_res_soft != SCHD_INFINITY) {
			if (max_group_res_soft < used) {
				if (rescts != NULL)
					rescts->soft_limit_preempt_bit = preempt_bit;
				rc = preempt_bit;
			} else {
				if (rescts != NULL)
					rescts->soft_limit_preempt_bit = 0;
				continue;	/* ignore a generic limit */
			}
		} else if (max_gengroup_res_soft < used) {
			if (rescts != NULL)
				rescts->soft_limit_preempt_bit = preempt_bit;
			rc = preempt_bit;
		} else {
			/* usage is under generic group soft limit, reset the preempt bit */
			if (rescts != NULL)
				rescts->soft_limit_preempt_bit = 0;
		}
	}

	return (rc);
}

/**
 * @brief
 *		check_max_user_res	check to see whether the user can run a job
 *				and still be within max resource limits
 *
 * @param[in]	rr	-	resource_resv to run
 * @param[in]	cts_list	-	the user counts list
 * @param[out]  rdef -		resource definition of resource exceeding a limit
 * @param [in]	limitctx	-	the limit storage context
 *
 * @return	int
 * @retval	0	: if the user would be under or at its limits
 * @retval	1	: if a generic user limit would be exceeded
 * @retval	2	: if an individual user limit would be exceeded
 * @retval	-1	: on error
 */
static int
check_max_user_res(resource_resv *rr, counts_umap &cts_list, resdef **rdef,
	void *limitctx)
{
	char		*userreskey;
	char		*genuserreskey;
	std::string	user = rr->user;
	schd_resource	*res;
	sch_resource_t	max_user_res;
	sch_resource_t	max_genuser_res;
	sch_resource_t	used = 0;

	if (rr == NULL)
		return (-1);
	if ((limres == NULL) || (rr->resreq == NULL))
		return (0);

	user = rr->user;

	for (res = limres; res != NULL; res = res->next) {
		resource_req *req;

		if ((req = find_resource_req(rr->resreq, res->def)) == NULL)
			continue;

		/* individual user limit check */
		if ((userreskey = entlim_mk_reskey(LIM_USER, user.c_str(),
			res->name)) == NULL)
			return (-1);
		max_user_res = lim_get(userreskey, limitctx);
		free(userreskey);

		/* generic user limit check */
		if ((genuserreskey = lim_genuserreskey(res->name)) == NULL)
			return (-1);
		max_genuser_res = lim_get(genuserreskey, limitctx);
		free(genuserreskey);

		if ((max_user_res == SCHD_INFINITY) &&
			(max_genuser_res == SCHD_INFINITY))
			continue;

		/* at this point, we know a generic or individual limit is set */
		used = find_counts_elm(cts_list, user, res->def, NULL, NULL);

		log_eventf(PBSEVENT_DEBUG4, PBS_EVENTCLASS_JOB, LOG_DEBUG, rr->name,
			"user %s max_*user_res.%s (%.1lf, %.1lf), used %.1lf",
			user.c_str(), res->name, max_user_res, max_genuser_res, used);

		if (max_user_res != SCHD_INFINITY) {
			if (used + req->amount > max_user_res) {
				*rdef = res->def;
				return (2);
			} else
				continue;	/* ignore a generic limit */
		} else if (used + req->amount > max_genuser_res) {
			*rdef = res->def;
			return (1);
		}
	}

	return (0);
}

/**
 * @brief
 *		check_max_user_res_soft		check to see whether the user can run a
 *					resource resv and still be within max
 *					user resource limits
 *
 * @param[in]	rr_arr	-	resource_resv array to count
 * @param[in]	rr	-	resource_resv to run
 * @param[in]	cts_list	-	the user counts list
 * @param[in]	limitctx	-	the limit storage context
 * @param[in]	preempt_bit	-	preempt bit value to set and return if limit is exceeded
 *
 * @return	int
 * @retval	0	: if the user would be under or at its limits
 * @retval	1	: if a generic user limit would be exceeded
 * @retval	-1	: on error
 */
static int
check_max_user_res_soft(resource_resv **rr_arr, resource_resv *rr,
	counts_umap &cts_list, void *limitctx, int preempt_bit)
{
	char		*userreskey;
	char		*genuserreskey;
	std::string	user = rr->user;
	schd_resource	*res;
	sch_resource_t	max_user_res_soft;
	sch_resource_t	max_genuser_res_soft;
	sch_resource_t	used = 0;
	resource_count  *rescts;
	int		rc = 0;

	if (rr == NULL)
		return (-1);
	if ((limres == NULL) || (rr->resreq == NULL))
		return (0);

	user = rr->user;

	for (res = limres; res != NULL; res = res->next) {
		/* If the job is not requesting the limit resource, it is not over its soft limit*/
		if (find_resource_req(rr->resreq, res->def) == NULL)
			continue;

		/* individual user limit check */
		if ((userreskey = entlim_mk_reskey(LIM_USER, user.c_str(),
			res->name)) == NULL)
			return (-1);
		max_user_res_soft = lim_get(userreskey, limitctx);
		free(userreskey);

		/* generic user limit check */
		if ((genuserreskey = lim_genuserreskey(res->name)) == NULL)
			return (-1);
		max_genuser_res_soft = lim_get(genuserreskey, limitctx);
		free(genuserreskey);

		if ((max_user_res_soft == SCHD_INFINITY) &&
			(max_genuser_res_soft == SCHD_INFINITY))
			continue;

		rescts = NULL;
		/* at this point, we know a generic or individual limit is set */
		used = find_counts_elm(cts_list, user, res->def, NULL, &rescts);

		log_eventf(PBSEVENT_DEBUG4, PBS_EVENTCLASS_JOB, LOG_DEBUG, rr->name,
			"user %s max_*user_res_soft (%.1lf, %.1lf), used %.1lf",
			user.c_str(), max_user_res_soft, max_genuser_res_soft, used);

		if (max_user_res_soft != SCHD_INFINITY) {
			if (max_user_res_soft < used) {
				if (rescts != NULL)
					rescts->soft_limit_preempt_bit = preempt_bit;
				rc = preempt_bit;
			} else {
				if (rescts != NULL)
					rescts->soft_limit_preempt_bit = 0;
				continue;	/* ignore a generic limit */
			}
		} else if (max_genuser_res_soft < used) {
			if (rescts != NULL)
				rescts->soft_limit_preempt_bit = preempt_bit;
			rc = preempt_bit;
		} else {
			/* usage is under generic user soft limit, reset the preempt bit */
			if (rescts != NULL)
				rescts->soft_limit_preempt_bit = 0;
		}
	}

	return (rc);
}

/**
 * @brief
 *		lim_setreslimits	set new-style resource limits
 * @par
 *		For the given attribute value and context, parse and set any limit
 *		directives found therein.  We expect the attribute's value to be a
 *		number.
 *
 * @param[in]	a	-	pointer to the attribute, whose value is a new-style
 *							limit attribute
 * @param[in]	ctx	-	the limit context into which the limits should be stored
 *
 * @return	int
 * @retval	0	: success
 * @retval	1	: failure
 */
static int
lim_setreslimits(const struct attrl *a, void *ctx)
{
	schd_resource	*r;

	/* remember resources that appear in a limit */
	r = find_alloc_resource_by_str(limres, a->resource);
	if (limres == NULL)
		limres = r;

	if (entlim_parse(a->value, a->resource, ctx, lim_callback) == 0)
		return (0);
	else {
		log_eventf(PBSEVENT_DEBUG, PBS_EVENTCLASS_SCHED, LOG_DEBUG, __func__,
			"entlim_parse(%s, %s) failed", a->value, a->resource);
		return (1);
	}
}

/**
 * @brief
 * 		free and clear saved limit resources.  Must be called whenever
 *		resource definitions are updated.
 *
 * @return void
 */
void
clear_limres(void)
{
	free_resource_list(limres);
	limres = NULL;
}

/**
 *      @brief returns a linked list of resources being limited.
 *
 *      @par This is to be treated as read-only.  Modifying this will adversely
 *           affect the limits code
 *
 *      @return schd_resource *
 */
schd_resource *
query_limres(void)
{
	return limres;
}

/**
 * @brief
 *		lim_setrunlimits	set new-style run limits
 * @par
 *		For the given attribute value and context, parse and set any limit
 *		directives found therein.  We expect the attribute's value to be a
 *		number.
 *
 * @param[in]	a	-	pointer to the attribute, whose value is a new-style
 *						limit attribute
 * @param [in]	ctx	-	the limit context into which the limits should be stored
 *
 * @return	int
 * @retval	0	: success
 * @retval	1	: failure
 */
static int
lim_setrunlimits(const struct attrl *a, void *ctx)
{

	if (entlim_parse(a->value, NULL, ctx, lim_callback) == 0)
		return (0);
	else {
		log_eventf(PBSEVENT_DEBUG, PBS_EVENTCLASS_SCHED, LOG_DEBUG, __func__, "entlim_parse(%s) failed", a->value);
		return (1);
	}
}

/**
 * @brief
 *		lim_setoldlimits	set old-style run limits
 * @par
 *		For the given attribute value and context, parse and set any limit
 *		directives found therein.  We expect the attribute's value to be a
 *		number.
 *
 * @param[in]	a	-	pointer to the attribute, whose value is an old-style
 *						limit attribute
 * @param[in]	ctx	-	the limit context into which the limits should be stored
 *
 * @return	int
 * @retval	0	: success
 * @retval	1	: failure
 * @retval	-1	: indicates an internal error (bad lim_param in old2new_soft[])
 */
static int
lim_setoldlimits(const struct attrl *a, void *ctx)
{
	size_t i;
	struct lim_old2new *avalue = NULL;
	enum lim_keytypes kt;
	const char *p;
	const char *e;

	/* first try soft limits ... */
	for (i = 0; i < sizeof(old2new_soft) / sizeof(old2new_soft[0]); i++) {
		if (!strcmp(a->name, old2new_soft[i].lim_attr)) {
			avalue = &old2new_soft[i];

			p = avalue->lim_param;
			if (*p == 'g')
				kt = LIM_GROUP;
			else if (*p == 'o')
				kt = LIM_OVERALL;
			else if (*p == 'u')
				kt = LIM_USER;
			else
				return(-1);

			/* e is PBS_GENERIC_ENTITY or PBS_ALL_ENTITY */
			e = p + 2;
			if (avalue->lim_isreslim) {
				schd_resource *r;

				/* remember resources that appear in a limit */
				r = find_alloc_resource_by_str(limres, a->resource);
				if (limres == NULL)
					limres = r;

				return(lim_callback(LI2RESCTXSOFT(ctx),
						kt, const_cast<char *>(avalue->lim_param),
						const_cast<char *>(e),
						a->resource, a->value));
			} else
				return(lim_callback(LI2RUNCTXSOFT(ctx),
						kt, const_cast<char *>(avalue->lim_param),
						const_cast<char *>(e),
						NULL, a->value));
		}
	}

	/* ... then hard */
	for (i = 0; i < sizeof(old2new) / sizeof(old2new[0]); i++) {
		if (!strcmp(a->name, old2new[i].lim_attr)) {
			avalue = &old2new[i];

			p = avalue->lim_param;
			if (*p == 'g')
				kt = LIM_GROUP;
			else if (*p == 'o')
				kt = LIM_OVERALL;
			else if (*p == 'u')
				kt = LIM_USER;
			else
				return(-1);

			/* e is PBS_GENERIC_ENTITY or PBS_ALL_ENTITY */
			e = p + 2;
			if (avalue->lim_isreslim) {
				schd_resource *r;

				/* remember resources that appear in a limit */
				r = find_alloc_resource_by_str(limres, a->resource);
				if (limres == NULL)
					limres = r;

				return(lim_callback(LI2RESCTX(ctx),
						kt, const_cast<char *>(avalue->lim_param),
						const_cast<char *>(e),
						a->resource,
						a->value));
			} else
				return(lim_callback(LI2RUNCTX(ctx),
						kt, const_cast<char *>(avalue->lim_param),
						const_cast<char *>(e),
						a->resource,
						a->value));
		}
	}

	return(1); /* attribute name not found in translation table */
}
/**
 * @brief
 *		lim_dup_ctx	duplicate all entries in a limit storage context
 *
 * @param[in]	ctx	-	the limit storage context
 *
 * @return	void *
 * @retval	the newly-allocated storage context	: on success
 * @retval	NULL	: on error
 */
static void *
lim_dup_ctx(void *ctx)
{
	void *newctx;
	char *key = NULL;
	char *value = NULL;

	if ((newctx = entlim_initialize_ctx()) == NULL) {
		log_err(errno, __func__, "malloc failed");
		return(NULL);
	}

	while ((value = static_cast<char *>(entlim_get_next(ctx, (void **)&key))) != NULL) {
		const char *newval;
		if ((newval = strdup(value)) == NULL) {
			log_event(PBSEVENT_DEBUG, PBS_EVENTCLASS_SCHED, LOG_ERR, __func__, "strdup value failed");
			(void) entlim_free_ctx(newctx, free);
			return NULL;
		} else if (entlim_add(key, newval, newctx) != 0) {
			log_eventf(PBSEVENT_DEBUG, PBS_EVENTCLASS_SCHED, LOG_ERR, __func__, "entlim_add(%s) failed", key);
			/*
			 *	One might think that we should free newval
			 *	on error as well.  We don't only because we
			 *	are uncertain whether the underneath entlim code
			 *	might have remembered the location of the key's
			 *	value in spite of having returned failure indication.
			 *	We choose to leak a small amount of memory (in
			 *	what we hope to be rare circumstances) rather
			 *	than have the scheduler crash when the system
			 *	memory allocation code detects and aborts due
			 *	to twice-freed memory.
			 */
			(void) entlim_free_ctx(newctx, free);
			return NULL;
		}
	}
	return newctx;
}

/**
 * @brief
 *		is_hardlimit	is the named attribute a new-style hard limit?
 *
 * @param[in]	a	-	pointer to the attribute, whose value is a limit attribute
 *
 * @return	int
 * @retval	0	: if the attrl pointer does not represent a hard lmit
 * @retval	1	: if the attrl pointer represents a soft lmit
 */
static int
is_hardlimit(const struct attrl *a)
{
	if (!strcmp(a->name, ATTR_max_run) ||
		!strcmp(a->name, ATTR_max_run_res))
		return (1);
	else
		return (0);
}

/**
 * @brief
 *		lim_gengroupreskey	special-purpose shortcut function to construct a
 *				generic group resource key
 *
 * @param[in]	res	-	the resource
 *
 * @return	a resource limit key if successful
 * @retval	NULL	: if not
 */
static char *
lim_gengroupreskey(const char *res)
{
	return (entlim_mk_reskey(LIM_GROUP, genparam, res));
}

/**
 * @brief
 *		lim_genprojectreskey	special-purpose shortcut function to construct a
 *				generic project resource key
 *
 * @param[in]	res	-	the resource
 *
 * @return	a resource limit key if successful
 * @retval	NULL	: if not
 */
static char *
lim_genprojectreskey(const char *res)
{
	return (entlim_mk_reskey(LIM_PROJECT, genparam, res));
}

/**
 * @brief
 *		lim_genuserreskey	special-purpose shortcut function to construct a
 *				generic user resource key
 *
 * @param[in]	res	-	the resource
 *
 * @return	a resource limit key on success
 * @retval	NULL	: on failure
 */
static char *
lim_genuserreskey(const char *res)
{
	return (entlim_mk_reskey(LIM_USER, genparam, res));
}

/**
 * @brief
 *		lim_callback install a new key of the given type and value
 *
 * @param[in]	ctx		the limit storage context
 * @param[in]	kt		the key type
 * @param[in]	param		entity type (unused - see entlim_parse_one())
 * @param[in]	namestring	entity name (see entlim_parse_one())
 * @param[in]	res		the limit resource for which a limit is being set
 * @param[in]	val		the value of the limit
 *
 * @return	int
 * @retval	0 if limit is successfully set
 * @retval	-1 on error.
 */
static int
lim_callback(void *ctx, enum lim_keytypes kt, char *param, char *namestring,
	char *res, char *val)
{
	char		*key = NULL;
	char		*v = NULL;

	if (res != NULL)
		key = entlim_mk_reskey(kt, namestring, res);
	else
		key = entlim_mk_runkey(kt, namestring);
	if (key == NULL) {
		log_eventf(PBSEVENT_SCHED, PBS_EVENTCLASS_SCHED, LOG_ERR, __func__,
			"key construction %d %s failed", (int) kt, namestring);
		return (-1);
	}

	if ((v = strdup(val)) == NULL) {
		log_eventf(PBSEVENT_SCHED, PBS_EVENTCLASS_SCHED, LOG_ERR, __func__,
			"strdup %s %s %s failed", key, res, val);
		free(key);
		return (-1);
	}

	if (entlim_add(key, v, ctx) != 0) {
		log_eventf(PBSEVENT_SCHED, PBS_EVENTCLASS_SCHED, LOG_ERR, __func__,
			"limit set %s %s %s failed", key, res, val);
		free(v);
		free(key);
		return (-1);
	} else {
		if (res != NULL)
			log_eventf(PBSEVENT_DEBUG4, PBS_EVENTCLASS_SCHED, LOG_DEBUG, __func__,
				"limit set %s %s %s", key, res, val);
		else
			log_eventf(PBSEVENT_DEBUG4, PBS_EVENTCLASS_SCHED, LOG_DEBUG, __func__,
				"limit set %s NULL %s", key, val);
		free(key);
		return (0);
	}
}

/**
 * @brief
 *		lim_get	fetch a limit value
 *
 * @param[in]	param	-	the requested limit
 * @param[in]	ctx	-	the limit storage context
 *
 * @return	sch_resource_t
 * @retval	the value of the limit, if no error occurs fetching it
 * @retval	SCHD_INFINITY if no such limit exists in the named context
 */
static sch_resource_t
lim_get(const char *param, void *ctx)
{
	char		*retptr;

	retptr = static_cast<char *>(entlim_get(param, ctx));
	if (retptr != NULL) {
		sch_resource_t	v;

		v = res_to_num(retptr, NULL);
		return (v);
	} else {
		return (SCHD_INFINITY);
	}
}

/**
 * @brief
 *		schderr_args_q	log a queue-related run limit exceeded message
 *
 * @param[in]	qname	-	name of the queue
 * @param[in]	entity	-	name of the group or user, or NULL if unneeded by fmt
 * @param[out]	err	-	schd_error structure to return error information
 */
static void
schderr_args_q(const std::string& qname, const char *entity, schd_error *err)
{
	set_schd_error_arg(err, ARG1, qname.c_str());
	if (entity != NULL)
		set_schd_error_arg(err, ARG2, entity);
}
// overloaded
static void
schderr_args_q(const std::string& qname, const std::string& entity, schd_error *err)
{
	set_schd_error_arg(err, ARG1, qname.c_str());
	if (!entity.empty())
		set_schd_error_arg(err, ARG2, entity.c_str());
}

/**
 * @brief
 * 		schderr_args_q_res log a queue-related resource limit exceeded message
 *
 * @param [in]	qname	-	name of the queue
 * @param [in]	entity	-	name of the group or user, or NULL if unneeded by fmt
 * @param [in]	res	-	name of the resource
 * @param [out]	err	-	schd_error structure to return error information
 */
static void
schderr_args_q_res(const std::string& qname, const char *entity, char *res,
	schd_error *err)
{
	set_schd_error_arg(err, ARG1, qname.c_str());
	set_schd_error_arg(err, ARG2, res);
	if (entity != NULL)
		set_schd_error_arg(err, ARG3, (char*)entity);
}
//overload
static void
schderr_args_q_res(const std::string& qname, const std::string& entity, char *res,
	schd_error *err)
{
	set_schd_error_arg(err, ARG1, qname.c_str());
	set_schd_error_arg(err, ARG2, res);
	if (!entity.empty())
		set_schd_error_arg(err, ARG3, entity.c_str());
}

/**
 * @brief
 *		schderr_args_server	log a server-related run limit exceeded message
 *
 * @param [in]	entity	-	name of the group or user, or NULL if unneeded
 * @param [out]	err	-	schd_error structure to return error information
 */
static void
schderr_args_server(const char *entity, schd_error *err)
{
	set_schd_error_arg(err, ARG1, (char*)entity);
}
// overloaded
static void
schderr_args_server(const std::string &entity, schd_error *err)
{
	set_schd_error_arg(err, ARG1, entity.c_str());
}
/**
 * @brief
 *		schderr_args_server_res	log a server-related resource limit exceeded
 *				message
 *
 * @param[in]	entity	-	name of the group or user, or NULL if unneeded by fmt
 * @param[in]	res	-	name of the resource
 * @param[out]	err	-	schd_error structure to return error information
 */
static void
schderr_args_server_res(std::string &entity, const char *res, schd_error *err)
{
	set_schd_error_arg(err, ARG1, (char*)res);
	if (!entity.empty())
		set_schd_error_arg(err, ARG2, entity.c_str());
}



/**
 * @brief
 *		check_max_project_res	check to see whether the user can run a
 *				resource resv and still be within project max
 *				resource limits
 *
 * @param[in]	rr	-	resource_resv to run
 * @param[in]	cts_list	-	the user counts list
 * @param[out]  rdef -		resource definition of resource exceeding a limit
 * @param[in]	limitctx	-	the limit storage context
 *
 * @return	int
 * @retval	0	: if the project would be under or at its limits
 * @retval	1	: if a generic project limit would be exceeded
 * @retval	2	: if an individual project limit would be exceeded
 * @retval	-1	: on error
 */
static int
check_max_project_res(resource_resv *rr, counts_umap &cts_list,
	resdef **rdef, void *limitctx)
{
	char		*projectreskey;
	char		*genprojectreskey;
	schd_resource	*res;
	std::string	project;
	sch_resource_t	max_project_res;
	sch_resource_t	max_genproject_res;
	sch_resource_t	used = 0;

	if (rr == NULL)
		return (-1);
	if ((limres == NULL) || (rr->resreq == NULL) || (rr->project.empty()))
		return (0);

	project = rr->project;
	for (res = limres; res != NULL; res = res->next) {
		resource_req *req;
		if ((req = find_resource_req(rr->resreq, res->def)) == NULL)
			continue;

		/* individual project limit check */
		if ((projectreskey = entlim_mk_reskey(LIM_PROJECT,
			project.c_str(), res->name)) == NULL)
			return (-1);
		max_project_res = lim_get(projectreskey, limitctx);
		free(projectreskey);

		/* generic project limit check */
		if ((genprojectreskey = lim_genprojectreskey(res->name)) == NULL)
			return (-1);
		max_genproject_res = lim_get(genprojectreskey, limitctx);
		free(genprojectreskey);

		if ((max_project_res == SCHD_INFINITY) &&
			(max_genproject_res == SCHD_INFINITY))
			continue;

		/* at this point, we know a generic or individual limit is set */
		used = find_counts_elm(cts_list, project, res->def, NULL, NULL);

		log_eventf(PBSEVENT_DEBUG4, PBS_EVENTCLASS_JOB, LOG_DEBUG, rr->name,
			"project %s max_*project_res.%s (%.1lf, %.1lf), used %.1lf",
			project.c_str(), res->name, max_project_res, max_genproject_res, used);

		if (max_project_res != SCHD_INFINITY) {
			if (used + req->amount > max_project_res) {
				*rdef = res->def;
				return (2);
			} else
				continue;	/* ignore a generic limit */
		} else if (used + req->amount > max_genproject_res) {
			*rdef = res->def;
			return (1);
		}
	}

	return (0);
}

/**
 * @brief
 *		check_max_project_res_soft	check to see whether the user can run a
 *					resource resv and still be within project
 *					max resource soft limits
 *
 * @param[in]	rr	-	resource_resv to run
 * @param[in]	cts_list	-	the user counts list
 * @param[in]	limitctx	-	the limit storage context
 * @param[in]	preempt_bit	-	preempt bit value to set and return if limit is exceeded
 *
 * @return	int
 * @retval	0	: if the project would be under or at its limits
 * @retval	1	: if a generic or individual project limit would be exceeded
 * @retval	-1	: on error
 */
static int
check_max_project_res_soft(resource_resv *rr, counts_umap &cts_list, void *limitctx, int preempt_bit)
{
	char		*projectreskey;
	char		*genprojectreskey;
	std::string	project;
	schd_resource	*res;
	sch_resource_t	max_project_res_soft;
	sch_resource_t	max_genproject_res_soft;
	sch_resource_t	used = 0;
	resource_count  *rescts;
	int		rc = 0;

	if (rr == NULL)
		return (-1);
	if ((limres == NULL) || (rr->resreq == NULL) || (rr->project.empty()))
		return (0);

	project = rr->project;
	for (res = limres; res != NULL; res = res->next) {
		/* If the job is not requesting the limit resource, it is not over its soft limit*/
		if (find_resource_req(rr->resreq, res->def) == NULL)
			continue;

		/* individual project limit check */
		if ((projectreskey = entlim_mk_reskey(LIM_PROJECT, project.c_str(),
			res->name)) == NULL)
			return (-1);
		max_project_res_soft = lim_get(projectreskey, limitctx);
		free(projectreskey);

		/* generic project limit check */
		if ((genprojectreskey = lim_genprojectreskey(res->name)) == NULL)
			return (-1);
		max_genproject_res_soft = lim_get(genprojectreskey, limitctx);
		free(genprojectreskey);

		if ((max_project_res_soft == SCHD_INFINITY) &&
			(max_genproject_res_soft == SCHD_INFINITY))
			continue;

		rescts = NULL;
		/* at this point, we know a generic or individual limit is set */
		used = find_counts_elm(cts_list, project, res->def, NULL, &rescts);
		log_eventf(PBSEVENT_DEBUG4, PBS_EVENTCLASS_JOB, LOG_DEBUG, rr->name,
			"project %s max_*project_res_soft.%s (%.1lf, %.1lf), used %.1lf",
			project.c_str(), res->name, max_project_res_soft, max_genproject_res_soft, used);

		if (max_project_res_soft != SCHD_INFINITY) {
			if (max_project_res_soft < used){
				if (rescts != NULL)
					rescts->soft_limit_preempt_bit = preempt_bit;
				rc = preempt_bit;
			} else {
				if (rescts != NULL)
					rescts->soft_limit_preempt_bit = 0;
				continue;	/* ignore a generic limit */
			}
		} else if (max_genproject_res_soft < used) {
			if (rescts != NULL)
				rescts->soft_limit_preempt_bit = preempt_bit;
			rc = preempt_bit;
		} else {
			/* usage is under generic project soft limit, reset the preempt bit */
			if (rescts != NULL)
				rescts->soft_limit_preempt_bit = 0;
		}
	}

	return (rc);
}


/**
 * @brief
 *		check_server_max_project_res	hard limit checking function for
 *					project server resource limits
 *
 * @param[in]	si	-	server_info structure to use for limit evaluation
 * @param[in]	qi	-	queue_info structure to use for limit evaluation
 * @param[in]	rr	-	resource_resv structure to use for limit evaluation
 * @param[in]	sc	-	limcounts struct for server count/total_count maxes over job run
 * @param[in]	qc	-	limcounts struct for queue count/total_count maxes over job run
 * @param[out]	err	-	schd_error structure to return error information
 *
 * @return	integer indicating failing limit test if limit is exceeded
 * @retval	0	: if limit is not exceeded
 * @retval	sched_error_code enum	: if limit is exceeded
 * @retval	SCHD_ERROR	: on error
 *
 * @see	#sched_error_code in constant.h
 */
static int
check_server_max_project_res(server_info *si, queue_info *qi, resource_resv *rr,
	limcounts *sc, limcounts *qc, schd_error *err)
{
	int		ret;
	resdef		*rdef = NULL;

	if ((si == NULL) || (rr == NULL) || (sc == NULL))
		return (SCHD_ERROR);

	if (rr->project.empty())
		return 0;

	if (!si->has_proj_limit)
	    return (0);

	auto &cts = sc->project;

	ret = check_max_project_res(rr, cts,
		&rdef, LI2RESCTX(si->liminfo));
	if (ret != 0) {
		log_eventf(PBSEVENT_DEBUG4, PBS_EVENTCLASS_JOB, LOG_DEBUG, rr->name,
			"check_max_project_res returned %d", ret);
	}
	switch (ret) {
		default:
		case -1:
			return (SCHD_ERROR);
		case 0:
			return (0);
		case 1:	/* generic project limit exceeded */
			err->rdef = rdef;
			return (SERVER_PROJECT_RES_LIMIT_REACHED);
		case 2:	/* individual project limit exceeded */
			schderr_args_server_res(rr->project, NULL, err);
			err->rdef = rdef;
			return (SERVER_BYPROJECT_RES_LIMIT_REACHED);
	}
}


/**
 * @brief
 *		check_server_max_project_run_soft	soft limit checking function for
 *					project server run limits
 *
 * @param[in]	si	-	server_info structure to use for limit evaluation
 * @param[in]	qi	-	queue_info structure to use for limit evaluation
 * @param[in]	rr	-	resource_resv structure to use for limit evaluation
 *
 * @return	integer indicating failing limit test if limit is exceeded
 * @retval	0	: if limit is not exceeded
 * @retval	PREEMPT_TO_BIT(preempt enum)	: if limit is exceeded
 * @retval	PREEMPT_TO_BIT(PREEMPT_ERR)	: on error
 *
 * @see	#preempt enum in constant.h
 */
static int
check_server_max_project_run_soft(server_info *si, queue_info *qi,
	resource_resv *rr)
{
	char		*key;
	std::string	project;
	int		used;
	int		max_project_run_soft, max_genproject_run_soft;
	counts		*cnt = NULL;

	if (si == NULL)
		return (PREEMPT_TO_BIT(PREEMPT_ERR));

	if (rr->project.empty())
		return 0;

	if (!si->has_proj_limit)
	    return (0);

	project = rr->project;
	if ((key = entlim_mk_runkey(LIM_PROJECT, project.c_str())) == NULL)
		return (PREEMPT_TO_BIT(PREEMPT_ERR));
	max_project_run_soft = (int) lim_get(key, LI2RUNCTXSOFT(si->liminfo));
	free(key);

	if ((key = entlim_mk_runkey(LIM_PROJECT, genparam)) == NULL)
		return (PREEMPT_TO_BIT(PREEMPT_ERR));
	max_genproject_run_soft = (int) lim_get(key, LI2RUNCTXSOFT(si->liminfo));
	free(key);

	if ((max_project_run_soft == SCHD_INFINITY) &&
		(max_genproject_run_soft == SCHD_INFINITY))
		return (0);


	/* at this point, we know a generic or individual limit is set */
	used = find_counts_elm(si->project_counts, project, NULL, &cnt, NULL);
	log_eventf(PBSEVENT_DEBUG4, PBS_EVENTCLASS_JOB, LOG_DEBUG, rr->name,
		"project %s max_*project_run_soft (%d, %d), used %d",
		project.c_str(), max_project_run_soft, max_genproject_run_soft, used);

	if (max_project_run_soft != SCHD_INFINITY) {
		if (max_project_run_soft < used) {
			if (cnt != NULL)
				cnt->soft_limit_preempt_bit = PREEMPT_TO_BIT(PREEMPT_OVER_SERVER_LIMIT);
			return (PREEMPT_TO_BIT(PREEMPT_OVER_SERVER_LIMIT));
		} else {
			if (cnt != NULL)
				cnt->soft_limit_preempt_bit = 0;
			return (0);	/* ignore a generic limit */
		}
	} else if (max_genproject_run_soft < used) {
		if (cnt != NULL)
			cnt->soft_limit_preempt_bit = PREEMPT_TO_BIT(PREEMPT_OVER_SERVER_LIMIT);
		return (PREEMPT_TO_BIT(PREEMPT_OVER_SERVER_LIMIT));
	} else {
		if (cnt != NULL)
			cnt->soft_limit_preempt_bit = 0;
		return (0);
	}
}

/**
 * @brief
 *		check_server_max_project_res_soft	soft limit checking function for
 *					project server resource limits
 *
 * @param[in]	si	-	server_info structure to use for limit evaluation
 * @param[in]	qi	-	queue_info structure to use for limit evaluation
 * @param[in]	rr	-	resource_resv structure to use for limit evaluation
 *
 * @return	integer indicating failing limit test if limit is exceeded
 * @retval	0	: if limit is not exceeded
 * @retval	PREEMPT_TO_BIT(preempt enum)	: if limit is exceeded
 * @retval	PREEMPT_TO_BIT(PREEMPT_ERR)	: on error
 *
 * @see	#preempt enum in constant.h
 */
static int
check_server_max_project_res_soft(server_info *si, queue_info *qi,
	resource_resv *rr)
{
	if ((si == NULL) || (rr == NULL))
		return (PREEMPT_TO_BIT(PREEMPT_ERR));

	if (!si->has_proj_limit)
	    return (0);

	return (check_max_project_res_soft(rr, si->project_counts,
		LI2RESCTXSOFT(si->liminfo), PREEMPT_TO_BIT(PREEMPT_OVER_SERVER_LIMIT)));
}

/**
 * @brief
 *		check_queue_max_project_res	hard limit checking function for
 *					project queue resource limits
 *
 * @param[in]	si	-	server_info structure to use for limit evaluation
 * @param[in]	qi	-	queue_info structure to use for limit evaluation
 * @param[in]	rr	-	resource_resv structure to use for limit evaluation
 * @param[in]	sc	-	limcounts struct for server count/total_count maxes over job run
 * @param[in]	qc	-	limcounts struct for queue count/total_count maxes over job run
 * @param[out]	err	-	schd_error structure to return error information
 *
 * @return	integer indicating failing limit test if limit is exceeded
 * @retval	0	: if limit is not exceeded
 * @retval	sched_error_code enum	: if limit is exceeded
 * @retval	SCHD_ERROR	: on error
 *
 * @see	#sched_error_code in constant.h
 */
static int
check_queue_max_project_res(server_info *si, queue_info *qi, resource_resv *rr,
	limcounts *sc, limcounts *qc, schd_error *err)
{
	int		ret;
	resdef		*rdef = NULL;

	if ((qi == NULL) || (rr == NULL) || (qc == NULL))
		return (SCHD_ERROR);

	if (rr->project.empty())
		return 0;

	if (!qi->has_proj_limit)
	    return (0);

	auto &cts = qc->project;

	ret = check_max_project_res(rr, cts, &rdef, LI2RESCTX(qi->liminfo));
	if (ret != 0)
		log_eventf(PBSEVENT_DEBUG4, PBS_EVENTCLASS_JOB, LOG_DEBUG, rr->name,
			"check_max_project_res returned %d", ret);
	switch (ret) {
		default:
		case -1:
			return (SCHD_ERROR);
		case 0:
			return (0);
		case 1:	/* generic project limit exceeded */
			schderr_args_q_res(qi->name, NULL, NULL, err);
			err->rdef = rdef;
			return (QUEUE_PROJECT_RES_LIMIT_REACHED);
		case 2:	/* individual project limit exceeded */
			schderr_args_q_res(qi->name, rr->project, NULL, err);
			err->rdef = rdef;
			return (QUEUE_BYPROJECT_RES_LIMIT_REACHED);
	}
}

/**
 * @brief
 *		check_queue_max_project_run_soft	soft limit checking function for
 *					project queue run limits
 *
 * @param [in]	si	-	server_info structure to use for limit evaluation
 * @param [in]	qi	-	queue_info structure to use for limit evaluation
 * @param [in]	rr	-	resource_resv structure to use for limit evaluation
 *
 * @return	integer indicating failing limit test if limit is exceeded
 * @retval	0	: if limit is not exceeded
 * @retval	PREEMPT_TO_BIT(preempt enum)	: if limit is exceeded
 * @retval	PREEMPT_TO_BIT(PREEMPT_ERR)	: on error
 *
 * @see	#preempt enum in constant.h
 */
static int
check_queue_max_project_run_soft(server_info *si, queue_info *qi,
	resource_resv *rr)
{
	char		*key;
	std::string	project;
	int		used;
	int		max_project_run_soft, max_genproject_run_soft;
	counts		*cnt = NULL;

	if (qi == NULL)
		return (PREEMPT_TO_BIT(PREEMPT_ERR));

	if (rr->project.empty())
		return 0;

	if (!qi->has_proj_limit)
	    return (0);

	project = rr->project;
	if ((key = entlim_mk_runkey(LIM_PROJECT, project.c_str())) == NULL)
		return (PREEMPT_TO_BIT(PREEMPT_ERR));
	max_project_run_soft = (int) lim_get(key, LI2RUNCTXSOFT(qi->liminfo));
	free(key);

	if ((key = entlim_mk_runkey(LIM_PROJECT, genparam)) == NULL)
		return (PREEMPT_TO_BIT(PREEMPT_ERR));
	max_genproject_run_soft = (int) lim_get(key, LI2RUNCTXSOFT(qi->liminfo));
	free(key);

	if ((max_project_run_soft == SCHD_INFINITY) &&
		(max_genproject_run_soft == SCHD_INFINITY))
		return (0);

	used = find_counts_elm(qi->project_counts, project, NULL, &cnt, NULL);

	log_eventf(PBSEVENT_DEBUG4, PBS_EVENTCLASS_JOB, LOG_DEBUG, rr->name,
		"%s project %s max_*project_run_soft (%d, %d), used %d",
		project.c_str(), max_project_run_soft, max_genproject_run_soft, used);

	if (max_project_run_soft != SCHD_INFINITY) {
		if (max_project_run_soft < used) {
			if (cnt != NULL)
				cnt->soft_limit_preempt_bit = PREEMPT_TO_BIT(PREEMPT_OVER_QUEUE_LIMIT);
			return (PREEMPT_TO_BIT(PREEMPT_OVER_QUEUE_LIMIT));
		} else {
			if (cnt != NULL)
				cnt->soft_limit_preempt_bit = 0;
			return (0);	/* ignore a generic limit */
		}
	} else if (max_genproject_run_soft < used) {
		if (cnt != NULL)
			cnt->soft_limit_preempt_bit = PREEMPT_TO_BIT(PREEMPT_OVER_QUEUE_LIMIT);
		return (PREEMPT_TO_BIT(PREEMPT_OVER_QUEUE_LIMIT));
	}
	else {
		if (cnt != NULL)
			cnt->soft_limit_preempt_bit = 0;
		return (0);
	}
}


/**
 * @brief
 *		check_queue_max_project_res_soft	soft limit checking function for
 *					project queue resource limits
 *
 * @param[in]	si	-	server_info structure to use for limit evaluation
 * @param[in]	qi	-	queue_info structure to use for limit evaluation
 * @param[in]	rr	-	resource_resv structure to use for limit evaluation
 *
 * @return	integer indicating failing limit test if limit is exceeded
 * @retval	0	: if limit is not exceeded
 * @retval	PREEMPT_TO_BIT(preempt enum)	: if limit is exceeded
 * @retval	PREEMPT_TO_BIT(PREEMPT_ERR)	: on error
 *
 * @see	#preempt enum in constant.h
 */
static int
check_queue_max_project_res_soft(server_info *si, queue_info *qi,
	resource_resv *rr)
{
	if ((qi == NULL) || (rr == NULL))
		return (PREEMPT_TO_BIT(PREEMPT_ERR));

	if (!qi->has_proj_limit)
	    return (0);

	return (check_max_project_res_soft(rr, qi->project_counts,
		LI2RESCTXSOFT(qi->liminfo), PREEMPT_TO_BIT(PREEMPT_OVER_QUEUE_LIMIT)));
}


/**
 * @brief
 *		check_server_max_project_run	hard limit checking function for
 *					project server resource limits
 *
 * @param[in]	si	-	server_info structure to use for limit evaluation
 * @param[in]	qi	-	queue_info structure to use for limit evaluation
 * @param[in]	rr	-	resource_resv structure to use for limit evaluation
 * @param[in]	sc	-	limcounts struct for server count/total_count maxes over job run
 * @param[in]	qc	-	limcounts struct for queue count/total_count maxes over job run
 * @param[out]	err	-	schd_error structure to return error information
 *
 * @return	integer indicating failing limit test if limit is exceeded
 * @retval	0	: if limit is not exceeded
 * @retval	sched_error_code enum	: if limit is exceeded
 * @retval	SCHD_ERROR	: on error
 *
 * @see	#sched_error_code in constant.h
 */
static int
check_server_max_project_run(server_info *si, queue_info *qi, resource_resv *rr,
	limcounts *sc, limcounts *qc, schd_error *err)
{
	char		*key;
	std::string	project;
	int		used;
	int		max_project_run, max_genproject_run;

	if ((si == NULL) || (rr == NULL) || (sc == NULL))
		return (SCHD_ERROR);

	auto &cts = sc->project;

	if (rr->project.empty())
		return 0;

	if (!si->has_proj_limit)
	    return (0);

	project = rr->project;
	if ((key = entlim_mk_runkey(LIM_PROJECT, project.c_str())) == NULL)
		return (SCHD_ERROR);
	max_project_run = (int) lim_get(key, LI2RUNCTX(si->liminfo));
	free(key);

	if ((key = entlim_mk_runkey(LIM_PROJECT, genparam)) == NULL)
		return (SCHD_ERROR);
	max_genproject_run = (int) lim_get(key, LI2RUNCTX(si->liminfo));
	free(key);

	if ((max_project_run == SCHD_INFINITY) &&
		(max_genproject_run == SCHD_INFINITY))
		return (0);


	/* at this point, we know a generic or individual limit is set */
	used = find_counts_elm(cts, project, NULL, NULL, NULL);

	log_eventf(PBSEVENT_DEBUG4, PBS_EVENTCLASS_JOB, LOG_DEBUG, rr->name,
		"project %s max_*project_run (%d, %d), used %d",
		project.c_str(), max_project_run, max_genproject_run, used);

	if (max_project_run != SCHD_INFINITY) {
		if (max_project_run <= used) {
			schderr_args_server(project, err);
			return (SERVER_BYPROJECT_JOB_LIMIT_REACHED);
		} else
			return (0);	/* ignore a generic limit */
	} else if (max_genproject_run <= used) {
		schderr_args_server(NULL, err);
		return (SERVER_PROJECT_LIMIT_REACHED);
	} else
		return (0);

}

/**
 * @brief
 *		check_queue_max_project_run	hard limit checking function for
 *					project queue run limits
 *
 * @param[in]	si	-	server_info structure to use for limit evaluation
 * @param[in]	qi	-	queue_info structure to use for limit evaluation
 * @param[in]	rr	-	resource_resv structure to use for limit evaluation
 * @param[in]	sc	-	limcounts struct for server count/total_count maxes over job run
 * @param[in]	qc	-	limcounts struct for queue count/total_count maxes over job run
 * @param[out]	err	-	schd_error structure to return error information
 *
 * @return	integer indicating failing limit test if limit is exceeded
 * @retval	0	: if limit is not exceeded
 * @retval	sched_error_code enum	: if limit is exceeded
 * @retval	SCHD_ERROR	: on error
 *
 * @see	#sched_error_code in constant.h
 */
static int
check_queue_max_project_run(server_info *si, queue_info *qi, resource_resv *rr,
	limcounts *sc, limcounts *qc, schd_error *err)
{
	char		*key;
	std::string	project;
	int		used;
	int		max_project_run, max_genproject_run;

	if (qi == NULL || (rr == NULL) || (qc == NULL))
		return (SCHD_ERROR);

	auto &cts = qc->project;

	project = rr->project;
	if (project.empty())
		return 0;

	if (!qi->has_proj_limit)
	    return (0);

	if ((key = entlim_mk_runkey(LIM_PROJECT, project.c_str())) == NULL)
		return (SCHD_ERROR);
	max_project_run = (int) lim_get(key, LI2RUNCTX(qi->liminfo));
	free(key);

	if ((key = entlim_mk_runkey(LIM_PROJECT, genparam)) == NULL)
		return (SCHD_ERROR);
	max_genproject_run = (int) lim_get(key, LI2RUNCTX(qi->liminfo));
	free(key);

	if ((max_project_run == SCHD_INFINITY) &&
		(max_genproject_run == SCHD_INFINITY))
		return (0);


	/* at this point, we know a generic or individual limit is set */
	used = find_counts_elm(cts, project, NULL, NULL, NULL);

	log_eventf(PBSEVENT_DEBUG4, PBS_EVENTCLASS_JOB, LOG_DEBUG, rr->name,
		"project %s max_*project_run (%d, %d), used %d",
		project.c_str(), max_project_run, max_genproject_run, used);

	if (max_project_run != SCHD_INFINITY) {
		if (max_project_run <= used) {
			schderr_args_q(qi->name, project, err);
			return (QUEUE_BYPROJECT_JOB_LIMIT_REACHED);
		} else
			return (0);	/* ignore a generic limit */
	} else if (max_genproject_run <= used) {
		schderr_args_q(qi->name, NULL, err);
		return (QUEUE_PROJECT_LIMIT_REACHED);
	} else
		return (0);
}
