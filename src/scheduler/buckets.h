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


#ifndef _BUCKETS_H
#define _BUCKETS_H

#include "data_types.h"

/* bucket_bitpool constructor, copy constructor, destructor */
bucket_bitpool *new_bucket_bitpool();
void free_bucket_bitpool(bucket_bitpool *bp);
bucket_bitpool *dup_bucket_bitpool(bucket_bitpool *obp);

/* node_bucket constructor, copy constructor, destructor */
node_bucket *new_node_bucket(int new_pools);
node_bucket *dup_node_bucket(node_bucket *onb, server_info *nsinfo);
node_bucket **dup_node_bucket_array(node_bucket **old, server_info *nsinfo);
void free_node_bucket(node_bucket *nb);
void free_node_bucket_array(node_bucket **buckets);

/* find index of node_bucket in an array */
int find_node_bucket_ind(node_bucket **buckets, schd_resource *rl, queue_info *queue, int priority);

/* create node_buckets an array of nodes */
node_bucket **create_node_buckets(status *policy, node_info **nodes, std::vector<queue_info *> &queues, unsigned int flags);

/* Create a name for the node bucket based on resources, queue, and priority */
char *create_node_bucket_name(status *policy, node_bucket *nb);

/* match job's request to buckets and allocate */
int bucket_match(chunk_map **cmap, resource_resv *resresv, schd_error *err);
/* convert chunk_map->node_bits into nspec array */
nspec **bucket_to_nspecs(status *policy, chunk_map **cmap, resource_resv *resresv);

/* can a job completely fit on a node before it is busy */
int node_can_fit_job_time(int node_ind, resource_resv *resresv);

/* bucket version of a = b */
void set_working_bucket_to_truth(node_bucket *nb);
void set_chkpt_bucket_to_working(node_bucket *nb);
void set_working_bucket_to_chkpt(node_bucket *nb);
void set_chkpt_bucket_to_truth(node_bucket *nb);
void set_truth_bucket_to_chkpt(node_bucket *nb);

/* chunk_map constructor, copy constructor, destructor */
chunk_map *new_chunk_map();
chunk_map *dup_chunk_map(chunk_map *ocmap);
void free_chunk_map(chunk_map *cmap);
void free_chunk_map_array(chunk_map **cmap_arr);
chunk_map **dup_chunk_map_array(chunk_map **ocmap_arr);

/* decide of a job should use the node_bucket path */
int job_should_use_buckets(resource_resv *resresv);

/* Log a summary of a chunk_map array */
void log_chunk_map_array(resource_resv *resresv, chunk_map **cmap);


/* Check to see if a job can run on nodes via the node_bucket codepath */
nspec **check_node_buckets(status *policy, server_info *sinfo, queue_info *qinfo, resource_resv *resresv, schd_error *err);
nspec **map_buckets(status *policy, node_bucket **bkts, resource_resv *resresv, schd_error *err);

/* map job to buckets that can satisfy */
chunk_map **find_correct_buckets(status *policy, node_bucket **buckets, resource_resv *resresv, schd_error *err);

#endif	/* _BUCKETS_H */
