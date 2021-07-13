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

#include <pbs_config.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "data_types.h"
#include "pbs_bitmap.h"
#include "node_info.h"
#include "server_info.h"
#include "buckets.h"
#include "globals.h"
#include "resource.h"
#include "resource_resv.h"
#include "simulate.h"
#include "misc.h"
#include "sort.h"
#include "node_partition.h"
#include "check.h"
#include <log.h>
#include "pbs_internal.h"

/* bucket_bitpool constructor */
bucket_bitpool *
new_bucket_bitpool()
{
	bucket_bitpool *bp;

	bp = static_cast<bucket_bitpool *>(calloc(1, sizeof(bucket_bitpool)));
	if (bp == NULL) {
		log_err(errno, __func__, MEM_ERR_MSG);
		return NULL;
	}

	bp->truth = pbs_bitmap_alloc(NULL, 1);
	if (bp->truth == NULL) {
		free_bucket_bitpool(bp);
		return NULL;
	}
	bp->truth_ct = 0;

	bp->working = pbs_bitmap_alloc(NULL, 1);
	if (bp->working == NULL) {
		free_bucket_bitpool(bp);
		return NULL;
	}
	bp->working_ct = 0;

	return bp;
}

/* bucket_bitpool destructor */
void
free_bucket_bitpool(bucket_bitpool *bp) {
	if(bp == NULL)
		return;

	pbs_bitmap_free(bp->truth);
	pbs_bitmap_free(bp->working);

	free(bp);
}

/* bucket_bitpool copy constructor */
bucket_bitpool *
dup_bucket_bitpool(bucket_bitpool *obp) {
	bucket_bitpool *nbp;

	nbp = new_bucket_bitpool();

	if (pbs_bitmap_assign(nbp->truth, obp->truth) == 0) {
		free_bucket_bitpool(nbp);
		return NULL;
	}
	nbp->truth_ct = obp->truth_ct;

	if (pbs_bitmap_assign(nbp->working, obp->working) == 0) {
		free_bucket_bitpool(nbp);
		return NULL;
	}
	nbp->working_ct = obp->working_ct;

	return nbp;
}

/* node_bucket constructor */
node_bucket *
new_node_bucket(int new_pools) {
	node_bucket *nb;

	nb = static_cast<node_bucket *>(calloc(1, sizeof(node_bucket)));
	if (nb == NULL) {
		log_err(errno, __func__, MEM_ERR_MSG);
		return NULL;
	}

	if (new_pools) {
		nb->busy_pool = new_bucket_bitpool();
		if (nb->busy_pool == NULL) {
			free_node_bucket(nb);
			return NULL;
		}

		nb->busy_later_pool = new_bucket_bitpool();
		if (nb->busy_later_pool == NULL) {
			free_node_bucket(nb);
			return NULL;
		}

		nb->free_pool = new_bucket_bitpool();
		if (nb->free_pool == NULL) {
			free_node_bucket(nb);
			return NULL;
		}
	} else {
		nb->busy_pool = NULL;
		nb->busy_later_pool = NULL;
		nb->free_pool = NULL;
	}
	nb->bkt_nodes = pbs_bitmap_alloc(NULL, 1);
	if (nb->bkt_nodes == NULL) {
		free_node_bucket(nb);
		return NULL;
	}

	nb->res_spec = NULL;
	nb->queue = NULL;
	nb->priority = 0;
	nb->total = 0;

	return nb;
}

/* node_bucket copy constructor */
node_bucket *
dup_node_bucket(node_bucket *onb, server_info *nsinfo) {
	node_bucket *nnb;

	nnb = new_node_bucket(0);
	if (nnb == NULL)
		return NULL;

	nnb->busy_pool = dup_bucket_bitpool(onb->busy_pool);
	if (nnb->busy_pool == NULL) {
		free_node_bucket(nnb);
		return NULL;
	}

	nnb->busy_later_pool = dup_bucket_bitpool(onb->busy_later_pool);
	if (nnb->busy_later_pool == NULL) {
		free_node_bucket(nnb);
		return NULL;
	}

	nnb->free_pool = dup_bucket_bitpool(onb->free_pool);
	if (nnb->free_pool == NULL) {
		free_node_bucket(nnb);
		return NULL;
	}

	pbs_bitmap_assign(nnb->bkt_nodes, onb->bkt_nodes);
	nnb->res_spec = dup_resource_list(onb->res_spec);
	if (nnb->res_spec == NULL) {
		free_node_bucket(nnb);
		return NULL;
	}

	if (onb->queue != NULL)
		nnb->queue = find_queue_info(nsinfo->queues, onb->queue->name);

	if (onb->name != NULL)
		nnb->name = string_dup(onb->name);
	nnb->total = onb->total;
	nnb->priority = onb->priority;

	return nnb;
}

/* node_bucket array copy constructor */
node_bucket **
dup_node_bucket_array(node_bucket **old, server_info *nsinfo) {
	node_bucket **nnb;
	int i;
	if (old == NULL)
		return NULL;

	nnb = static_cast<node_bucket **>(malloc((count_array(old) + 1) * sizeof(node_bucket *)));
	if (nnb == NULL) {
		log_err(errno, __func__, MEM_ERR_MSG);
		return NULL;
	}

	for (i = 0; old[i] != NULL; i++) {
		nnb[i] = dup_node_bucket(old[i], nsinfo);
		if (nnb[i] == NULL) {
			free_node_bucket_array(nnb);
			return NULL;
		}
	}

	nnb[i] = NULL;

	return nnb;
}

/* node_bucket destructor */
void
free_node_bucket(node_bucket *nb) {
	if(nb == NULL)
		return;

	free_bucket_bitpool(nb->busy_pool);
	free_bucket_bitpool(nb->busy_later_pool);
	free_bucket_bitpool(nb->free_pool);

	free_resource_list(nb->res_spec);

	pbs_bitmap_free(nb->bkt_nodes);

	free(nb->name);
	free(nb);
}

/* node bucket array destructor */
void
free_node_bucket_array(node_bucket **buckets) {
	int i;

	if (buckets == NULL)
		return;

	for (i = 0; buckets[i] != NULL; i++)
		free_node_bucket(buckets[i]);

	free(buckets);
}

/* node_bucket_count constructor */
node_bucket_count *
new_node_bucket_count() {
	node_bucket_count *nbc;

	nbc = static_cast<node_bucket_count *>(malloc(sizeof(node_bucket_count)));
	if(nbc == NULL) {
		log_err(errno, __func__, MEM_ERR_MSG);
		return NULL;
	}
	nbc->bkt = NULL;
	nbc->chunk_count = 0;

	return nbc;
}

void
free_node_bucket_count(node_bucket_count *nbc) {
	if(nbc == NULL)
		return;

	free(nbc);
}

void free_node_bucket_count_array(node_bucket_count **nbc_array) {
	int i;
	if(nbc_array == NULL)
		return;

	for(i = 0; nbc_array[i] != NULL; i++)
		free_node_bucket_count(nbc_array[i]);

	free(nbc_array);
}

/**
 * @brief find the index into an array of node_buckets based on resources, queue, and priority
 * @param[in] buckets - the node_bucket array to search
 * @param[in] rl - the resource list of the node bucket
 * @param[in] qinfo - the queue of the node bucket
 * @param[in] priority - the priority of the node bucket
 * @return int
 * @retval index of array if found
 * @retval -1 if not found or on error
 */
int
find_node_bucket_ind(node_bucket **buckets, schd_resource *rl, queue_info *qinfo, int priority) {
	int i;
	if (buckets == NULL || rl == NULL)
		return -1;

	for (i = 0; buckets[i] != NULL; i++) {
		if (buckets[i]->queue == qinfo && buckets[i]->priority == priority &&
				compare_resource_avail_list(buckets[i]->res_spec, rl))
			return i;
	}
	return -1;
}

/**
 * @brief create a name for a node bucket based on resource names, priority, and queue
 *
 * @return char *
 * @retval name of bucket
 * @retval NULL - error
 */
char *create_node_bucket_name(status *policy, node_bucket *nb) {
	char *name;
	int len;

	if(policy == NULL || nb == NULL)
		return NULL;

	name = create_resource_signature(nb->res_spec, policy->resdef_to_check_no_hostvnode, ADD_ALL_BOOL);
	if (name == NULL)
		return NULL;

	len = strlen(name);

	if (nb->priority != 0) {
		char buf[20];
		if (pbs_strcat(&name, &len, ":priority=") == NULL) {
			free(name);
			return NULL;
		}
		snprintf(buf, sizeof(buf), "%d", nb->priority);
		if (pbs_strcat(&name, &len, buf) == NULL) {
			free(name);
			return NULL;
		}
	}

	if (nb->queue != NULL) {
		if (pbs_strcat(&name, &len, ":queue=") == NULL) {
			free(name);
			return NULL;
		}
		if (pbs_strcat(&name, &len, nb->queue->name.c_str()) == NULL) {
			free(name);
			return NULL;
		}
	}

	return name;

}

/**
 * @brief create node buckets from an array of nodes
 * @param[in] policy - policy info
 * @param[in] nodes - the nodes to create buckets from
 * @param[in] queues - the queues the nodes may be associated with.  May be NULL
 * @param[in] flags - flags to control creation of buckets
 * 						UPDATE_BUCKET_IND - update the bucket_ind member on the node_info structure
 * 						NO_PRINT_BUCKETS - do not print that a bucket has been created
 * @return node_bucket **
 * @retval array of node buckets
 * @retval NULL on error
 */
node_bucket **
create_node_buckets(status *policy, node_info **nodes, std::vector<queue_info *> &queues, unsigned int flags) {
	int i;
	int j = 0;
	node_bucket **buckets = NULL;
	node_bucket **tmp;
	int node_ct;

	if (policy == NULL || nodes == NULL)
		return NULL;

	node_ct = count_array(nodes);

	buckets = static_cast<node_bucket **>(calloc((node_ct + 1), sizeof(node_bucket *)));
	if (buckets == NULL) {
		log_err(errno, __func__, MEM_ERR_MSG);
		return NULL;
	}


	for (i = 0; i < node_ct; i++) {
		node_bucket *nb = NULL;
		int bkt_ind;
		queue_info *qinfo = NULL;
		int node_ind = nodes[i]->node_ind;

		if (nodes[i]->is_down || nodes[i]->is_offline || node_ind == -1 || nodes[i]->lic_lock == 0)
			continue;

		if (!queues.empty() && !nodes[i]->queue_name.empty())
			qinfo = find_queue_info(queues, nodes[i]->queue_name);

		bkt_ind = find_node_bucket_ind(buckets, nodes[i]->res, qinfo, nodes[i]->priority);
		if (flags & UPDATE_BUCKET_IND) {
			if (bkt_ind == -1)
				nodes[i]->bucket_ind = j;
			else
				nodes[i]->bucket_ind = bkt_ind;
		}
		if (bkt_ind != -1)
			nb = buckets[bkt_ind];


		if (nb == NULL) { /* no bucket found, need to add one*/
			schd_resource *cur_res;
			buckets[j] = new_node_bucket(1);

			if (buckets[j] == NULL) {
				free_node_bucket_array(buckets);
				return NULL;
			}

			buckets[j]->res_spec = dup_selective_resource_list(nodes[i]->res, policy->resdef_to_check_no_hostvnode,
									   (ADD_UNSET_BOOLS_FALSE | ADD_ALL_BOOL));

			if (buckets[j]->res_spec == NULL) {
				free_node_bucket_array(buckets);
				return NULL;
			}

			if (qinfo != NULL)
				buckets[j]->queue = qinfo;

			buckets[j]->priority = nodes[i]->priority;

			for (cur_res = buckets[j]->res_spec; cur_res != NULL; cur_res = cur_res->next)
				if (cur_res->type.is_consumable)
					cur_res->assigned = 0;


			buckets[j]->busy_later_pool->truth_ct = 0;
			buckets[j]->free_pool->truth_ct = 0;
			buckets[j]->busy_pool->truth_ct = 0;

			buckets[j]->total = 0;

			buckets[j]->name = create_node_bucket_name(policy, buckets[j]);
			if (buckets[j]->name == NULL) {
				free_node_bucket_array(buckets);
				return NULL;
			}
			if (!(flags & NO_PRINT_BUCKETS))
				log_eventf(PBSEVENT_DEBUG3, PBS_EVENTCLASS_NODE, LOG_DEBUG, __func__, "Created node bucket %s", buckets[j]->name);

			nb = buckets[j];
			j++;
		}
		pbs_bitmap_bit_on(nb->bkt_nodes, node_ind);
		nb->total++;
		if (nodes[i]->is_free && nodes[i]->num_jobs == 0 && nodes[i]->num_run_resv == 0) {
			if (nodes[i]->node_events != NULL) {
				pbs_bitmap_bit_on(nb->busy_later_pool->truth, node_ind);
				nb->busy_later_pool->truth_ct++;
			}
			else {
				pbs_bitmap_bit_on(nb->free_pool->truth, node_ind);
				nb->free_pool->truth_ct++;
			}
		} else {
			pbs_bitmap_bit_on(nb->busy_pool->truth, node_ind);
			nb->busy_pool->truth_ct++;
		}
	}

	if (j == 0) {
		free(buckets);
		return NULL;
	}

	tmp = static_cast<node_bucket **>(realloc(buckets, (j + 1) * sizeof(node_bucket *)));
	if (tmp != NULL)
		buckets = tmp;
	else {
		log_err(errno, __func__, MEM_ERR_MSG);
		free_node_bucket_array(buckets);
		return NULL;

	}
	return buckets;
}

/* chunk_map constructor */
chunk_map *
new_chunk_map() {
	chunk_map *cmap;
	cmap = static_cast<chunk_map *>(malloc(sizeof(chunk_map)));
	if (cmap == NULL) {
		log_err(errno, __func__, MEM_ERR_MSG);
		return NULL;
	}

	cmap->chk = NULL;
	cmap->bkt_cnts = NULL;
	cmap->node_bits = pbs_bitmap_alloc(NULL, 1);
	if (cmap->node_bits == NULL) {
		free_chunk_map(cmap);
		return NULL;
	}

	return cmap;
}

/* chunk_map destructor */
void
free_chunk_map(chunk_map *cmap) {
	if (cmap == NULL)
		return;

	free_node_bucket_count_array(cmap->bkt_cnts);
	pbs_bitmap_free(cmap->node_bits);
	free(cmap);
}

/* chunk_map array destructor */
void
free_chunk_map_array(chunk_map **cmap_arr) {
	int i;
	if (cmap_arr == NULL)
		return;

	for (i = 0; cmap_arr[i] != NULL; i++)
		free_chunk_map(cmap_arr[i]);

	free(cmap_arr);
}

/**
 * @brief log a summary of a chunk_map array
 * @param[in] resresv - the job we are logging about
 * @param[in] cmap - the chunk_map to log
 *
 * @return nothing
 */
void
log_chunk_map_array(resource_resv *resresv, chunk_map **cmap) {
	int i;
	int j;

	if (resresv == NULL || cmap == NULL)
		return;

	for (i = 0; cmap[i] != NULL; i++) {
		int total_chunks = 0;

		log_eventf(PBSEVENT_DEBUG3, PBS_EVENTCLASS_JOB, LOG_DEBUG, resresv->name, "Chunk: %s", cmap[i]->chk->str_chunk);

		for (j = 0; cmap[i]->bkt_cnts[j] != NULL; j++) {
			int chunk_count;
			node_bucket_count *nbc = cmap[i]->bkt_cnts[j];
			chunk_count = (nbc->bkt->free_pool->truth_ct + nbc->bkt->busy_later_pool->truth_ct) * nbc->chunk_count;
			log_eventf(PBSEVENT_DEBUG3, PBS_EVENTCLASS_JOB, LOG_DEBUG, resresv->name, "Bucket %s can fit %d chunks", nbc->bkt->name, chunk_count);
			total_chunks += chunk_count;
		}
		if (total_chunks < cmap[i]->chk->num_chunks)
			log_eventf(PBSEVENT_DEBUG3, PBS_EVENTCLASS_JOB, LOG_DEBUG, resresv->name,
				"Found %d out of %d chunks needed", total_chunks, cmap[i]->chk->num_chunks);
	}
}


/**
 * @brief set working buckets = truth buckets
 * @param[in,out] nb - node bucket to set
 */
void
set_working_bucket_to_truth(node_bucket *nb) {
	if(nb == NULL)
		return;
	if (nb->busy_pool == NULL || nb->busy_later_pool == NULL || nb->free_pool == NULL)
		return;

	pbs_bitmap_assign(nb->busy_pool->working, nb->busy_pool->truth);
	nb->busy_pool->working_ct = nb->busy_pool->truth_ct;

	pbs_bitmap_assign(nb->busy_later_pool->working, nb->busy_later_pool->truth);
	nb->busy_later_pool->working_ct = nb->busy_later_pool->truth_ct;

	pbs_bitmap_assign(nb->free_pool->working, nb->free_pool->truth);
	nb->free_pool->working_ct = nb->free_pool->truth_ct;
}

/**
 * @brief map job to nodes in buckets and allocate nodes to job
 * @param[in, out] cmap - mapping between chunks and buckets for the job
 * @param[in] resresv - the job
 * @param[out] err - error structure
 * @return int
 * @retval 1 - success
 * @retval 0 - failure
 */
int
bucket_match(chunk_map **cmap, resource_resv *resresv, schd_error *err)
{
	int i;
	int j;
	int k;
	static pbs_bitmap *zeromap = NULL;
	server_info *sinfo;

	if (cmap == NULL || resresv == NULL || resresv->select == NULL)
		return 0;

	if (zeromap == NULL) {
		zeromap = pbs_bitmap_alloc(NULL, 1);
		if (zeromap == NULL)
			return 0;
	}

	sinfo = resresv->server;

	for (i = 0; cmap[i] != NULL; i++) {
		if (cmap[i]->bkt_cnts != NULL) {
			for (j = 0; cmap[i]->bkt_cnts[j] != NULL; j++) {
				set_working_bucket_to_truth(cmap[i]->bkt_cnts[j]->bkt);
				pbs_bitmap_assign(cmap[i]->node_bits, zeromap);
			}
		}
	}

	for (i = 0; cmap[i] != NULL; i++) {
		int num_chunks_needed = cmap[i]->chk->num_chunks;

		if (cmap[i]->bkt_cnts == NULL)
			break;

		for (j = 0; cmap[i]->bkt_cnts[j] != NULL && num_chunks_needed > 0; j++) {
			node_bucket *bkt = cmap[i]->bkt_cnts[j]->bkt;
			int chunks_added = 0;


			for (k = pbs_bitmap_first_on_bit(bkt->busy_later_pool->working);
			     num_chunks_needed > chunks_added && k >= 0;
			     k = pbs_bitmap_next_on_bit(bkt->busy_later_pool->working, k)) {
				clear_schd_error(err);
				if (resresv->aoename != NULL) {
					if (sinfo->unordered_nodes[k]->current_aoe == NULL ||
					   strcmp(sinfo->unordered_nodes[k]->current_aoe, resresv->aoename) != 0)
						if (is_provisionable(sinfo->unordered_nodes[k], resresv, err) == NOT_PROVISIONABLE) {
							continue;
						}
				}
				if (node_can_fit_job_time(k, resresv)) {
					pbs_bitmap_bit_off(bkt->busy_later_pool->working, k);
					bkt->busy_later_pool->working_ct--;
					pbs_bitmap_bit_on(bkt->busy_pool->working, k);
					bkt->busy_pool->working_ct++;
					pbs_bitmap_bit_on(cmap[i]->node_bits, k);
					chunks_added += cmap[i]->bkt_cnts[j]->chunk_count;
				}

			}

			for (k = pbs_bitmap_first_on_bit(bkt->free_pool->working);
			     num_chunks_needed > chunks_added && k >= 0;
			     k = pbs_bitmap_next_on_bit(bkt->free_pool->working, k)) {
				clear_schd_error(err);
				if (resresv->aoename != NULL) {
					if (sinfo->unordered_nodes[k]->current_aoe == NULL ||
					   strcmp(sinfo->unordered_nodes[k]->current_aoe, resresv->aoename) != 0)
						if (is_provisionable(sinfo->unordered_nodes[k], resresv, err) == NOT_PROVISIONABLE) {
							continue;
						}
				}
				pbs_bitmap_bit_off(bkt->free_pool->working, k);
				bkt->free_pool->working_ct--;
				pbs_bitmap_bit_on(bkt->busy_pool->working, k);
				bkt->busy_pool->working_ct++;
				pbs_bitmap_bit_on(cmap[i]->node_bits, k);
				chunks_added += cmap[i]->bkt_cnts[j]->chunk_count;
			}

			if (chunks_added > 0)
				num_chunks_needed -= chunks_added;
		}
		/* Couldn't find buckets to satisfy all the chunks */
		if (num_chunks_needed > 0)
			return 0;
	}

	return 1;
}

/**
 * @brief Determine if a job can fit in time before a node becomes busy
 * @param[in] node_ind - index into sinfo->snodes of the node
 * @param[in] resresv - the job
 * @return yes/no
 * @retval 1 - yes
 * @retvan 0 - no
 */
int
node_can_fit_job_time(int node_ind, resource_resv *resresv)
{
	te_list *tel;
	time_t end;
	server_info *sinfo;

	if (resresv == NULL)
		return 0;

	sinfo = resresv->server;
	end = sinfo->server_time + calc_time_left(resresv, 0);
	tel = sinfo->unordered_nodes[node_ind]->node_events;
	if (tel != NULL && tel->event != NULL)
		if (tel->event->event_time < end)
			return 0;

	return 1;
}

/**
 * @brief convert a chunk into an nspec for a job on a node
 * @param policy - policy info
 * @param chk - the chunk
 * @param node - the node
 * @param aoename - the aoe requested by the job
 * @return nspec*
 * @retval the nspec
 * @retval NULL on error
 */
nspec *
chunk_to_nspec(status *policy, chunk *chk, node_info *node, char *aoename)
{
	nspec *ns;
	resource_req *prev_req;
	resource_req *req;
	resource_req *cur_req;

	if (policy == NULL || chk == NULL || node == NULL)
		return NULL;

	ns = new_nspec();
	if (ns == NULL)
		return NULL;

	ns->ninfo = node;
	ns->seq_num = get_sched_rank();
	ns->end_of_chunk = 1;
	prev_req = NULL;
	if (aoename != NULL) {
		if (node->current_aoe == NULL || strcmp(aoename, node->current_aoe) != 0) {
			ns->go_provision = 1;
			req = create_resource_req("aoe", aoename);
			if (req == NULL) {
				free_nspec(ns);
				return NULL;
			}
			ns->resreq = req;
			prev_req = req;
		}
	}
	for (cur_req = chk->req; cur_req != NULL; cur_req = cur_req->next) {
		if (cur_req->def->type.is_consumable && policy->resdef_to_check.find(cur_req->def) != policy->resdef_to_check.end()) {
			req = dup_resource_req(cur_req);
			if (req == NULL) {
				free_nspec(ns);
				return NULL;
			}
			if (prev_req == NULL)
				ns->resreq = req;
			else
				prev_req->next = req;
			prev_req = req;
		}
	}

	return ns;
}

/**
 * @brief convert a chunk_map->node_bits into an nspec array
 * @param[in] policy - policy info
 * @param[in] cb_map - chunk_map->node_bits are the nodes to allocate
 * @param resresv - the job
 * @return nspec **
 * @retval nspec array to run the job on
 * @retval NULL on error
 */
nspec **
bucket_to_nspecs(status *policy, chunk_map **cb_map, resource_resv *resresv)
{
	int i;
	int j;
	int k;
	int cnt = 1;
	int n = 0;
	nspec **ns_arr;
	server_info *sinfo;

	if (policy == NULL || cb_map == NULL || resresv == NULL)
		return NULL;

	sinfo = resresv->server;
	ns_arr = static_cast<nspec **>(calloc(resresv->select->total_chunks + 1, sizeof(nspec*)));
	if (ns_arr == NULL) {
		log_err(errno, __func__, MEM_ERR_MSG);
		return NULL;
	}

	for (i = 0; cb_map[i] != NULL; i++) {
		int chunks_needed = cb_map[i]->chk->num_chunks;
		for (j = pbs_bitmap_first_on_bit(cb_map[i]->node_bits); j >= 0;
		     j = pbs_bitmap_next_on_bit(cb_map[i]->node_bits, j)) {
			/* Find the bucket the node is in */
			if (cb_map[i]->bkt_cnts != NULL) {
				for (k = 0; cb_map[i]->bkt_cnts[k] != NULL; k++)
					if (pbs_bitmap_get_bit(cb_map[i]->bkt_cnts[k]->bkt->bkt_nodes, j)) {
						cnt = cb_map[i]->bkt_cnts[k]->chunk_count;
						break;
					}
			} else {
				/* Error case(shouldn't happen): the bkt_cnts is NULL.  Only assign one chunk.
				 * This could cause us not to allocate enough chunks in free placement
				 */
				cnt = 1;
			}
			/* Allocate the chunks.  For all but the final chunk, we need to allocate cnt chunks,
			 * For the final chunk, we might allocate less.
			 */
			for( ; cnt > 0 && chunks_needed > 0; cnt--, chunks_needed--, n++) {
				ns_arr[n] = chunk_to_nspec(policy, cb_map[i]->chk, sinfo->unordered_nodes[j], resresv->aoename);
				if (ns_arr[n] == NULL) {
					free_nspecs(ns_arr);
					return NULL;
				}
			}
		}

	}
	ns_arr[n] = NULL;

	return ns_arr;
}

/**
 * @brief decide if a job should use the node bucket algorithm
 * @param resresv - the job
 * @return int
 * @retval 1 if the job should use the bucket algorithm
 * @retval 0 if not
 */
int job_should_use_buckets(resource_resv *resresv) {

	if (resresv == NULL)
		return 0;

	/* nodes are bucketed, they can't be sorted by unused */
	if (conf.node_sort_unused)
		return 0;

	/* Bucket algorithm doesn't support avoid_provisioning */
	if (conf.provision_policy == AVOID_PROVISION)
		return 0;

	/* qrun uses the standard path */
	if (resresv == resresv->server->qrun_job)
		return 0;

	/* Jobs in reservations use the standard path */
	if (resresv->job != NULL) {
		if(resresv->job->resv != NULL)
			return 0;
	}

	/* Only excl jobs use buckets */
	if (resresv->place_spec->share)
		return 0;
	if (!resresv->place_spec->excl)
		return 0;

	/* place=pack jobs do not use buckets */
	if (resresv->place_spec->pack)
		return 0;

	/*  multivnoded systems are incompatible */
	if (resresv->server->has_multi_vnode)
		return 0;

	/* Job's requesting specific hosts or vnodes use the standard path */
	const auto& defs = resresv->select->defs;
	if (defs.find(allres["host"]) != defs.end())
		return 0;
	if (defs.find(allres["vnode"]) != defs.end())
		return 0;
	/* If a job has an execselect, it means it's requesting vnode */
	if (resresv->execselect != NULL)
		return 0;

	return 1;

}

/*
 * @brief - create a mapping of chunks to the buckets they can run in.
 * 	    The mapping will be of the chunks to all the buckets that can satisfy them.
 * 	    This may be way more nodes than are required to run the job.
 * 	    If we can't find enough nodes in the buckets, we know we can never run.
 *
 * @param[in] policy - policy info
 * @param[in] buckets - buckets to check
 * @param[in] resresv - resresv to check
 * @param[out] err - error structure to return failure
 *
 * @return chunk map
 * @retval NULL - for the following reasons:
		- if no buckets are found for one chunk
 *		- if there aren't enough nodes in all buckets found for one chunk
 *		- on malloc() failure
 */
chunk_map **
find_correct_buckets(status *policy, node_bucket **buckets, resource_resv *resresv, schd_error *err)
{
	int bucket_ct;
	int chunk_ct;
	int i, j;
	int can_run = 1;
	chunk_map **cb_map;
	static struct schd_error *failerr = NULL;

	if (policy == NULL || buckets == NULL || resresv == NULL || resresv->select == NULL || resresv->select->chunks == NULL || err == NULL)
		return NULL;

	if (failerr == NULL) {
		failerr = new_schd_error();
		if (failerr == NULL) {
			set_schd_error_codes(err, NOT_RUN, SCHD_ERROR);
			return 0;
		}
	} else
		clear_schd_error(failerr);

	bucket_ct = count_array(buckets);
	chunk_ct = count_array(resresv->select->chunks);

	cb_map = static_cast<chunk_map **>(calloc((chunk_ct + 1), sizeof(chunk_map *)));
	if (cb_map == NULL) {
		log_err(errno, __func__, MEM_ERR_MSG);
		return NULL;
	}

	for (i = 0; resresv->select->chunks[i] != NULL; i++) {
		int total = 0;
		int b = 0;
		cb_map[i] = new_chunk_map();
		if (cb_map[i] == NULL) {
			free_chunk_map_array(cb_map);
			return NULL;
		}
		cb_map[i]->chk = resresv->select->chunks[i];
		cb_map[i]->bkt_cnts = static_cast<node_bucket_count **>(calloc(bucket_ct + 1, sizeof(node_bucket_count *)));
		if (cb_map[i]->bkt_cnts == NULL) {
			log_err(errno, __func__, MEM_ERR_MSG);
			free_chunk_map_array(cb_map);
			set_schd_error_codes(err, NOT_RUN, SCHD_ERROR);
			return NULL;
		}
		for (j = 0; buckets[j] != NULL && can_run; j++) {
			queue_info *qinfo = NULL;

			if (resresv->job != NULL && resresv->job->queue->nodes != NULL)
				qinfo = resresv->job->queue;

			if (buckets[j]->queue == qinfo) {
				int c;
				c = check_avail_resources(buckets[j]->res_spec, resresv->select->chunks[i]->req,
							  (CHECK_ALL_BOOLS | COMPARE_TOTAL | UNSET_RES_ZERO),
							  policy->resdef_to_check_no_hostvnode, INSUFFICIENT_RESOURCE, err);
				if (c > 0) {
					if (resresv->place_spec->scatter || resresv->place_spec->vscatter)
						c = 1;


					cb_map[i]->bkt_cnts[b] = new_node_bucket_count();
					if(cb_map[i]->bkt_cnts[b] == NULL) {
						free_chunk_map_array(cb_map);
						set_schd_error_codes(err, NOT_RUN, SCHD_ERROR);
						return NULL;
					}
					cb_map[i]->bkt_cnts[b]->bkt = buckets[j];
					cb_map[i]->bkt_cnts[b++]->chunk_count = c;
					total += buckets[j]->total * c;
				} else {
					if (failerr->status_code == SCHD_UNKWN)
						move_schd_error(failerr, err);
				}
				clear_schd_error(err);
			}
		}

		/* No buckets match or not enough nodes in the buckets: the job can't run */
		if(b == 0 || total < cb_map[i]->chk->num_chunks)
			can_run = 0;
	}
	cb_map[i] = NULL;

	log_chunk_map_array(resresv, cb_map);

	if (can_run == 0) {
		if (err->status_code == SCHD_UNKWN && failerr->status_code != SCHD_UNKWN)
			move_schd_error(err, failerr);
		err->status_code = NEVER_RUN;
		free_chunk_map_array(cb_map);
		return NULL;
	}


	return cb_map;
}

/**
 * @brief entry point into the node bucket algorithm.  If placement sets are
 * 	in use, choose the right pool and call map_buckets() on each.  If placement
 * 	sets are not in use, just call map_buckets()
 * @param[in] policy - policy info
 * @param[in] sinfo - the server info universe
 * @param[in] qinfo - the queue the job is in
 * @param[in] resresv - the job
 * @param[out] err - schd_error structure to return reason why the job can't run
 * @return nspec **
 * @retval place job can run
 * @retval NULL if job can't run
 */
nspec **
check_node_buckets(status *policy, server_info *sinfo, queue_info *qinfo, resource_resv *resresv, schd_error *err)
{
	node_partition **nodepart = NULL;

	if (policy == NULL || sinfo == NULL || resresv == NULL || err == NULL)
		return NULL;

	if (resresv->is_job && qinfo == NULL)
		return NULL;

	if (resresv->is_job && qinfo->nodepart != NULL)
		nodepart = qinfo->nodepart;
	else if (sinfo->nodepart != NULL)
		nodepart = sinfo->nodepart;
	else
		nodepart = NULL;

	/* job's place=group=res replaces server or queue node grouping
	 * We'll search the node partition cache for the job's pool of node partitions
	 * If it doesn't exist, we'll create it and add it to the cache
	 */
	if (resresv->place_spec->group != NULL) {
		std::vector<std::string> groupvec{resresv->place_spec->group};
		np_cache *npc = NULL;
		node_info **ninfo_arr;

		if (qinfo->has_nodes)
			ninfo_arr = qinfo->nodes;
		else
			ninfo_arr = sinfo->unassoc_nodes;

		npc = find_alloc_np_cache(policy, sinfo->npc_arr, groupvec, ninfo_arr, cmp_placement_sets);
		if (npc != NULL)
			nodepart = npc->nodepart;
	}
	if (nodepart != NULL) {
		int i;
		int can_run = 0;
		static schd_error *failerr = NULL;
		if (failerr == NULL) {
			failerr = new_schd_error();
			if (failerr == NULL)
				return NULL;
		} else
			clear_schd_error(failerr);

		for (i = 0; nodepart[i] != NULL; i++) {
			nspec **nspecs;
			log_eventf(PBSEVENT_DEBUG3, PBS_EVENTCLASS_JOB, LOG_DEBUG, resresv->name,
				"Evaluating placement set: %s", nodepart[i]->name);

			clear_schd_error(err);
			nspecs = map_buckets(policy, nodepart[i]->bkts, resresv, err);
			if (nspecs != NULL)
				return nspecs;
			if (err->status_code == NOT_RUN) {
				if (failerr->status_code == SCHD_UNKWN)
					copy_schd_error(failerr, err);
				can_run = 1;
			}
		}
		/* If we can't fit in any placement set, span over all of them */
		if (can_run == 0) {
			if (sc_attrs.do_not_span_psets) {
				set_schd_error_codes(err, NEVER_RUN, CANT_SPAN_PSET);
				return NULL;
			}
			else {
				log_event(PBSEVENT_DEBUG3, PBS_EVENTCLASS_JOB, LOG_DEBUG, resresv->name, "Request won't fit into any placement sets, will use all nodes");
				return map_buckets(policy, sinfo->buckets, resresv, err);
			}
		} else
			/* There is a possibility that the job might fit in one of the placement set,
			 * use that error code
			 */
			move_schd_error(err, failerr);
	} else if (pbs_conf.pbs_num_servers > 1 && resresv->svr_inst_id != NULL) {
		/* Restrict placement to local server when using buckets */
		if (sinfo->svr_to_psets.find(resresv->svr_inst_id) != sinfo->svr_to_psets.end()) {
			nspec **nspecs;

			nspecs = map_buckets(policy, sinfo->svr_to_psets[resresv->svr_inst_id]->bkts, resresv, err);
			if (nspecs != NULL)
				return nspecs;
		} else {	/* No nodes associated with owner server, so reject the job/reservation */
			set_schd_error_codes(err, NOT_RUN, NO_NODE_RESOURCES);
			return NULL;
		}
	}

	return map_buckets(policy, sinfo->buckets, resresv, err);
}

/*
 * @brief check to see if a resresv can fit on the nodes using buckets
 *
 * @param[in] policy - policy info
 * @param[in] bkts - buckets to search
 * @param[in] resresv - resresv to see if it can fit
 * @param[out] err - error structure to return failure
 *
 * @return place resresv can run or NULL if it can't
 */
nspec **
map_buckets(status *policy, node_bucket **bkts, resource_resv *resresv, schd_error *err)
{
	chunk_map **cmap;
	nspec **ns_arr;

	if (policy == NULL || bkts == NULL || resresv == NULL || err == NULL)
		return NULL;

	cmap = find_correct_buckets(policy, bkts, resresv, err);
	if (cmap == NULL)
		return NULL;

	clear_schd_error(err);
	if (bucket_match(cmap, resresv, err) == 0) {
		if (err->status_code == SCHD_UNKWN)
			set_schd_error_codes(err, NOT_RUN, NO_NODE_RESOURCES);

		free_chunk_map_array(cmap);
		return NULL;
	}

	ns_arr = bucket_to_nspecs(policy, cmap, resresv);

	free_chunk_map_array(cmap);
	return ns_arr;
}
