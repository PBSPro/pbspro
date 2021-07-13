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

#ifndef	_NODE_PARTITION_H
#define _NODE_PARTITION_H

#include "data_types.h"
#include <pbs_ifl.h>


/*
 *
 *      new_node_partition - allocate and initialize a node_partition
 *
 *      returns new node partition or NULL on error
 *
 */
#ifdef NAS /* localmod 005 */
node_partition *new_node_partition(void);
#else
node_partition *new_node_partition();
#endif /* localmod 005 */

/*
 *
 *      free_node_partition_array - free an array of node_partitions
 *
 *        np_arr - node partition array to free
 *
 *      returns nothing
 *
 */
void free_node_partition_array(node_partition **np_arr);

/*
 *
 *      free_node_partition - free a node_partition structure
 *
 *        np - the node_partition to free
 *
 */
void free_node_partition(node_partition *np);

/*
 *
 *      dup_node_partition_array - duplicate a node_partition array
 *
 *        onp_arr - the node_partition array to duplicate
 *        nsinfo - server for the new node partition
 *
 *      returns duplicated array or NULL on error
 *
 */
node_partition **dup_node_partition_array(node_partition **onp_arr, server_info *nsinfo);

/*
 *
 *      dup_node_partition - duplicate a node_partition structure
 *
 *        onp - the node_partition structure to duplicate
 *        nsinfo - server for the new node partiton (the nodes are needed)
 *
 *      returns duplicated node_partition or NULL on error
 *
 */
node_partition *dup_node_partition(node_partition *onp, server_info *nsinfo);

/* copy a node partition array from pointers out of another.*/
node_partition **copy_node_partition_ptr_array(node_partition **onp_arr, node_partition **new_nps);

/*
 *
 *      create_node_partitions - break apart nodes into partitions
 *
 *         IN: policy - policy info
 *         IN: nodes  -  the nodes to create partitions from
 *	   IN: resname - node grouping resource name
 *         IN: flags - flags which change operations of node partition creation
 *	  OUT: num_parts - the number of node partitions created
 *
 *      returns node_partition array or NULL on error
 *              num_parts set to the number of partitions created if not NULL
 *
 *
 */
node_partition **create_node_partitions(status *policy, node_info **nodes, const std::vector<std::string> &resnames,
					unsigned int flags, int *num_parts);

/*
 *
 *      find_node_partition - find a node partition by name in an array
 *
 *        np_arr - array of node partitions to search
 *        name
 *
 *      returns found node partition or NULL if not found
 *
 */
node_partition *find_node_partition(node_partition **np_arr, const std::string &name);

/* find node partition by unique rank */

node_partition *find_node_partition_by_rank(node_partition **np_arr, int rank);

/*
 *	node_partition_update_array - update an entire array of node partitions
 *	  nodepart - the array of node partition to update
 *
 *	returns 1 on all success, 0 on any failure
 *	Note: This is not an atomic operation
 */
int node_partition_update_array(status *policy, node_partition **nodepart);

/*
 *	node_partition_update - update the meta data about a node partition
 *			like free_nodes and res
 *
 *	  np - the node partition to update
 *
 *	returns 1 on success, 0 on failure
 */
int node_partition_update(status *policy, node_partition *np);

/*
 *	new_np_cache - constructor
 */
np_cache *new_np_cache(void);

/*
 *	free_np_cache_array - destructor for array
 */
void free_np_cache_array(std::vector<np_cache *> &npc_arr);

/*
 *	free_np_cache - destructor
 */
void free_np_cache(np_cache *npc);

/*
 *	find_np_cache - find a np_cache by the array of resource names and
 *			nodes which created it.
 *
 *	  npc_arr - the array to search
 *	  resnames - the list of names
 *	  ninfo_arr - array of nodes
 *
 *	NOTE: function does node node_info pointer comparison to save time
 *
 *	returns the node found node partition or NULL if not (or on error)
 *
 */
np_cache *
find_np_cache(np_cache **npc_arr,
	const std::vector<std::string> &resnames, node_info **ninfo_arr);
/*
 *	find_alloc_np_cache - find a np_cache by the array of resource names
 *			      and nodes which created it.  If the np_cache
 *			      does not exist, create it and add it to the list
 */
np_cache *
find_alloc_np_cache(status *policy, std::vector<np_cache *> &pnpc_arr,
	const std::vector<std::string> &resnames, node_info **ninfo_arr,
	int (*sort_func)(const void *, const void *));
/*
 *	add_np_cache - add an np_cache to an array
 *	returns 1 on success - 0 on failure
 */
int add_np_cache(np_cache ***npc_arr, np_cache *npc);

/*
 * do an inital check to see if a resresv can fit into a node partition
 * based on the meta data we keep
 */
int resresv_can_fit_nodepart(status *policy, node_partition *np, resource_resv *resresv, int total, schd_error *err);

/*
 *	create_specific_nodepart - create a node partition with specific
 *				   nodes, rather than from a placement
 *				   set resource=value
 */
node_partition *create_specific_nodepart(status *policy, const char *name, node_info **nodes, int flags );
/* create the placement sets for the server and queues */
bool create_placement_sets(status *policy, server_info *sinfo);

/* Update placement sets and allparts */
void update_all_nodepart(status *policy, server_info *sinfo, unsigned int flags);

/* Sort all placement sets (server's psets, queue's psets, and hostsets) */
void sort_all_nodepart(status *policy, server_info *sinfo);

/*
 * update the node buckets associated with a node
 */
void update_buckets_for_node(node_bucket **bkts, node_info *ninfo);

/*
 * update the node buckets associated with a node partition on
 * job/resv run/end
 */
void update_buckets_for_node_array(node_bucket **bkts, node_info **ninfo_arr);

#endif	/* _NODE_PARTITION_H */
