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
 * @file    resource.c
 *
 * @brief
 * 	resource.c - contains functions related to resources.
 *
 * Functions included are:
 * 	query_resources()
 * 	conv_rsc_type()
 * 	def_is_consumable()
 * 	def_is_bool()
 * 	copy_resdef_array()
 * 	find_resdef()
 * 	is_res_avail_set()
 * 	add_resource_sig()
 * 	create_resource_signature()
 * 	update_resource_defs()
 * 	resstr_to_resdef()
 * 	collect_resources_from_requests()
 * 	update_sorting_defs()
 *
 */

#include <pbs_config.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <pbs_ifl.h>
#include <log.h>
#include "resource.h"
#include "constant.h"
#include "config.h"
#include "data_types.h"
#include "misc.h"
#include "globals.h"
#include "resource_resv.h"
#include "pbs_internal.h"
#include "limits_if.h"
#include "sort.h"
#include "parse.h"
#include "fifo.h"



/**
 * @brief
 * 		query a pbs server for the resources it knows about and fill in the global unordered_map
 *
 * @param[in]	pbs_sd	-	communication descriptor to pbs server
 *
 * @return unordered_map of resource defs
 */
std::unordered_map<std::string, resdef *>
query_resources(int pbs_sd)
{
	struct batch_status *bs;		/* queried resources from server */
	struct batch_status *cur_bs;		/* used to iterate over resources */
	struct attrl *attrp;			/* iterate over resource fields */
	std::unordered_map<std::string, resdef *> tmpres;

	if ((bs = send_statrsc(pbs_sd, NULL, NULL, const_cast<char *>("p"))) == NULL) {
		const char *errmsg = pbs_geterrmsg(pbs_sd);
		if (errmsg == NULL)
			errmsg = "";

		log_eventf(PBSEVENT_SCHED, PBS_EVENTCLASS_REQUEST, LOG_INFO, "pbs_statrsc",
			"pbs_statrsc failed: %s (%d)", errmsg, pbs_errno);
		return {};
	}

	for (cur_bs = bs; cur_bs != NULL; cur_bs = cur_bs->next) {
		int flags = NO_FLAGS;
		resource_type rtype;

		for (attrp = cur_bs->attribs; attrp != NULL; attrp = attrp->next) {
			char *endp;
			
			if (!strcmp(attrp->name, ATTR_RESC_TYPE)) {
				int num = strtol(attrp->value, &endp, 10);
				rtype = conv_rsc_type(num);
			} else if (!strcmp(attrp->name, ATTR_RESC_FLAG)) {
				flags = strtol(attrp->value, &endp, 10);
			}
		}
		tmpres[cur_bs->name] = new resdef(cur_bs->name, flags, rtype);
	}
	pbs_statfree(bs);

	/**
	 * @par Make sure all the well known resources are sent to us.
	 *      This is to allow us to directly index into the allres umap.
	 *      Do not directly index into the allres umap for non-well known resources.  Use find_resdef()
	 */
	for (const auto& r : well_known_res) {
		if (tmpres.find(r) == tmpres.end()) {
			for (auto& d : tmpres)
				delete d.second;
			return {};
		}
	}

	return tmpres;
}

/**
 * @brief
 * 		convert server type number into resource_type struct
 *
 * @param[in]	type	-	server type number
 * @param[out]	rtype	-	resource type structure
 *
 * @return	converted resource_type
 *
 */
resource_type
conv_rsc_type(int type)
{
	resource_type rtype;
	switch (type) {
		case ATR_TYPE_STR:
		case ATR_TYPE_ARST:
			rtype.is_string = true;
			rtype.is_non_consumable = true;
			break;
		case ATR_TYPE_BOOL:
			rtype.is_boolean = true;
			rtype.is_non_consumable = true;
			break;
		case ATR_TYPE_SIZE:
			rtype.is_size = true;
			rtype.is_num = true;
			rtype.is_consumable = true;
			break;
		case ATR_TYPE_SHORT:
		case ATR_TYPE_LONG:
		case ATR_TYPE_LL:
			rtype.is_long = true;
			rtype.is_num = true;
			rtype.is_consumable = true;
			break;
		case ATR_TYPE_FLOAT:
			rtype.is_float = true;
			rtype.is_num = true;
			rtype.is_consumable = true;
			break;
	}
	return rtype;
}

/**
 * @brief find a resdef in the global allres
 * 	  This function should be used if not finding a well known resource
 *
 * @param name - the name of the resdef to find
 *
 * @return resdef *
 * @retval found resdef
 * @retval NULL if not found
 */
resdef *
find_resdef(const std::string& name)
{
	auto f = allres.find(name);
	if (f == allres.end())
		return NULL;

	return f->second;
}

/**
 * @brief
 * 		checks if a resource avail is set.
 *
 * @param[in]	res	-	resource to see if it is set
 *
 * @return	int
 * @retval	1	: is set
 * @retval	0	: not set
 */
int
is_res_avail_set(schd_resource *res)
{
	if (res == NULL)
		return 0;

	if (res->type.is_string) {
		if (res->str_avail != NULL && res->str_avail[0] != NULL)
			return 1;
	}
	else if (res->avail != SCHD_INFINITY_RES)
		return 1;

	return 0;
}

/**
 * @brief
 * 		add the resource signature of 'res' to the string 'sig'
 *
 * @param[in,out]	sig	-	resource signature we're adding to
 * @param[in,out]	sig_size	-	size of string sig
 * @param[in]	res	-	the resource to add to sig
 *
 * @return	success/failure
 * @retval	1	: success
 * @retval	0	: failure
 */
int
add_resource_sig(char **sig, int *sig_size, schd_resource *res)
{
	if (sig == NULL || res == NULL)
		return 0;

	if (pbs_strcat(sig, sig_size, res->name) == 0)
		return 0;
	if (pbs_strcat(sig, sig_size, "=") == 0)
		return 0;
	if (pbs_strcat(sig, sig_size, res_to_str(res, RF_AVAIL)) == 0)
		return 0;

	return 1;
}
/**
 * @brief
 * 		create node string resource node signature based on the resources
 *      in order from the 'resources' parameter.  This signature can be used
 *      to compare two nodes to see if they are equivalent resource wise
 * @par
 *      FORM: res0=val:res1=val:...:resN=val
 *      Where 0, 1, .., N are indices into the resources array
 *
 * @param[in]	node	-	node to create signature from
 * @param[in]	resources	-	string array of resources
 * @param[in]	flags	-	CHECK_ALL_BOOLS - include all booleans even if not in resources
 *
 * @return	char *
 * @retval	signature of node
 * @retval	NULL	: on error
 *
 * @par	it is the responsibility of the caller to free string returned
 */
char *
create_resource_signature(schd_resource *reslist, std::unordered_set<resdef *>& resources, unsigned int flags)
{
	char *sig = NULL;
	int sig_size = 0;
	schd_resource *res;

	if (reslist == NULL)
		return NULL;

	if ((sig = static_cast<char *>(malloc(1024))) == NULL) {
		log_err(errno, __func__, MEM_ERR_MSG);
		return NULL;
	}
	sig_size = 1024;
	sig[0] = '\0';

	for (const auto& r : resources) {
		res = find_resource(reslist, r);
		if (res != NULL) {
			if (res->indirect_res != NULL) {
				res = res->indirect_res;
			}
			if (is_res_avail_set(res)) {
				add_resource_sig(&sig, &sig_size, res);
				if (pbs_strcat(&sig, &sig_size, ":") == NULL) {
					free(sig);
					return NULL;
				}
			}
		}
	}

	if ((flags & ADD_ALL_BOOL)) {
		for (const auto& br : boolres) {
			if (resources.find(br) == resources.end()) {
				res = find_resource(reslist, br);
				if (res != NULL) {
					add_resource_sig(&sig, &sig_size, res);
					if (pbs_strcat(&sig, &sig_size, ":") == NULL) {
						free(sig);
						return NULL;
					}
				}
			}
		}
	}
	/* we will have a trailing ':' if we did anything, so strip it*/
	if (sig[0] != '\0')
		sig[strlen(sig) - 1] = '\0';

	return sig;
}



/**
 * @brief update allres and sub-containers of resource definitions.  This is called
 *		in schedule().  If it fails in schedule() we'll pick it up in the next call to quuery_server()
 *
 * @param[in]	pbs_sd	-	connection descriptor to the pbs server
 *
 * @return	bool
 * @retval	true - successfully updated resdefs
 * @retval	false - failed to update resdefs
 */
bool
update_resource_defs(int pbs_sd)
{
	auto tmpres = query_resources(pbs_sd);

	if (tmpres.empty())
		return false;

	for (auto &lr : last_running) {
		resource_req *prev_res = NULL;
		for (auto ru = lr.resused; ru != NULL;) {
			auto f = tmpres.find(ru->name);
			if (f == tmpres.end()) {
				resource_req *tru;
				tru = ru->next;
				free_resource_req(ru);
				if (prev_res != NULL)
					prev_res->next = tru;
				else
					lr.resused = tru;
				ru = tru;
			} else {
				ru->def = f->second;
				ru->name = ru->def->name.c_str();
				prev_res = ru;
				ru = ru->next;
			}
		}
	}

	for (auto& d : allres)
		delete d.second;

	allres = tmpres;

	consres.clear();
	for (const auto& def : allres) {
		if (def.second->type.is_consumable)
			consres.insert(def.second);
	}

	boolres.clear();
	for (const auto &def : allres) {
		if (def.second->type.is_boolean)
			boolres.insert(def.second);
	}

	conf.resdef_to_check.clear();
	if (!conf.res_to_check.empty()) {
		conf.resdef_to_check = resstr_to_resdef(conf.res_to_check);
	}
	update_sorting_defs();

	clear_limres();

	return true;
}

/**
 * @brief
 * 		convert an array of string resource names into resdefs
 *
 * @param[in]	resstr	-	array of resource strings
 *
 * @return	resdef array
 * @retval	NULL	: on error
 */
std::unordered_set<resdef *>
resstr_to_resdef(const std::unordered_set<std::string>& resstr)
{
	std::unordered_set<resdef *> defs;

	for (const auto& str : resstr) {
		auto def = find_resdef(str);
		if (def != NULL)
			defs.insert(def);
		else {
			log_event(PBSEVENT_SCHED, PBS_EVENTCLASS_FILE, LOG_NOTICE, str, "Unknown Resource");
		}
	}

	return defs;
}

std::unordered_set<resdef *>
resstr_to_resdef(const char * const* resstr)
{
	std::unordered_set<resdef *> defs;

	for (int i = 0; resstr[i] != NULL; i++) {
		auto def = find_resdef(resstr[i]);
		if (def != NULL)
			defs.insert(def);
		else {
			log_event(PBSEVENT_SCHED, PBS_EVENTCLASS_FILE, LOG_NOTICE, resstr[i], "Unknown Resource");
		}
	}

	return defs;
}

/**
 * @brief
 * 		collect a unique list of resource definitions from an array of requests
 *
 * @param[in]	resresv_arr	-	array of requests
 *
 * @return	array of resource definitions
 */
std::unordered_set<resdef *>
collect_resources_from_requests(resource_resv **resresv_arr)
{
	int i;
	resource_req *req;
	std::unordered_set<resdef *> defset;

	for (i = 0; resresv_arr[i] != NULL; i++) {
		resource_resv *r = resresv_arr[i];

		/* schedselect: node resources - resources to be satisfied on the nodes */
		if (r->select != NULL) {
			for (const auto &sdef : r->select->defs)
				defset.insert(sdef);
		}
		/*
		 * execselect: select created from the exec_vnode.  This is likely to
		 * be a subset of resources from schedselect + 'vnode'.  It is possible
		 * that a job was run with qrun -H (res=val) where res is not part of
		 * the schedselect.  The exec_vnode is taken directly from the -H argument
		 * The qrun -H case is why we need to do this check.
		 */
		if (r->execselect != NULL) {
			if ((r->job != NULL && in_runnable_state(r)) ||
			    (r->resv != NULL && (r->resv->resv_state == RESV_BEING_ALTERED || r->resv->resv_substate == RESV_DEGRADED))) {
				for (const auto &esd : r->execselect->defs)
					defset.insert(esd);
			}
		}
		/* Resource_List: job wide resources: resources submitted with
		 * qsub -l and resources with the ATTR_DFLAG_RASSN which the server
		 * sums all the requested amounts in the select and sets job wide
		 */
		for (req = r->resreq; req != NULL; req = req->next)
			if (conf.res_to_check.find(req->name) != conf.res_to_check.end())
				defset.insert(req->def);
	}
	return defset;
}

/**
 * @brief update the resource def for a single sort_info vector
 * @param[in] op - operation to do (e.h. update or clear)
 * @param[in, out] siv - sort_info vector to update
 * @param[in] obj - object type (node or job)
 * @param[in] prefix - string prefix for logging
 * 
 * @return nothing
 */
void update_single_sort_def(std::vector<sort_info>& siv, int obj, const char *prefix)
{
	for (auto &si : siv) {
		auto f = allres.find(si.res_name);
		if (is_speccase_sort(si.res_name, obj))
			si.def = NULL;
		else if (f == allres.end()) {
			log_eventf(PBSEVENT_SCHED, PBS_EVENTCLASS_FILE, LOG_NOTICE, CONFIG_FILE,
				"%s sorting resource %s is not a valid resource", prefix, si.res_name.c_str());
			si.def = NULL;
		} else
			si.def = f->second;
	}
}

/**
 * @brief update the resource definition pointers in the sort_info structures
 *
 * @par	We parse our config file when we start.  We do not have the resource
 *      definitions at that time.  They also can change over time if the server
 *      sends us a SCH_CONFIGURE command.
 *
 * @param[in]	op	-	update(non-zero) or clear definitions(0)
 */
void update_sorting_defs(void)
{
	update_single_sort_def(conf.prime_node_sort, SOBJ_NODE, "prime node");
	update_single_sort_def(conf.non_prime_node_sort, SOBJ_NODE, "Non-prime node");
	update_single_sort_def(conf.prime_sort, SOBJ_JOB, "prime job");
	update_single_sort_def(conf.non_prime_sort, SOBJ_JOB, "Non-prime job");
}

/**
 * 	@brief resource_type constructor
 */
resource_type::resource_type()
{
	is_non_consumable = false;
	is_string = false;
	is_boolean = false;

	is_consumable = false;
	is_num = false;
	is_long = false;
	is_float = false;
	is_size = false;
	is_time = false;
}
