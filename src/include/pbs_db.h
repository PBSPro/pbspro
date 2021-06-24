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
 * @file    pbs_db.h
 *
 * @brief
 * PBS database interface. (Functions declarations and structures)
 *
 * This header file contains functions to access the PBS data store
 * Only these functions should be used by PBS code. Actual implementation
 * of these functions are database specific and are implemented in Libdb.
 *
 * In most cases, the size of the fields in the structures correspond
 * one to one with the column size of the respective tables in database.
 * The functions/interfaces in this header are PBS Private.
 */

#ifndef _PBS_DB_H
#define	_PBS_DB_H

#include <pbs_ifl.h>
#include <sys/types.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "list_link.h"

#ifdef	__cplusplus
extern "C" {
#endif

#ifndef	MIN
#define	MIN(x, y)	(((x) < (y)) ? (x) : (y))
#endif
#ifndef	MAX
#define	MAX(x, y)	(((x) > (y)) ? (x) : (y))
#endif

#define PBS_MAX_DB_CONN_INIT_ERR  (MAXPATHLEN*2)

/* type of saves bit wise flags - see savetype */
#define OBJ_SAVE_NEW    1   /* object is new, so whole object should be saved */
#define OBJ_SAVE_QS     2   /* quick save area modified, it should be saved */

/**
 * @brief
 * Following are a set of mapping of DATABASE vs C data types. These are
 * typedefed here to allow mapping the database data types easily.
 */
typedef short     SMALLINT;
typedef int       INTEGER;
typedef long long BIGINT;
typedef char      *TEXT;

struct pbs_db_attr_list {
	int attr_count;
	pbs_list_head attrs;
};

typedef struct pbs_db_attr_list pbs_db_attr_list_t;

/**
 * @brief
 *  Structure used to map database server structure to C
 *
 */
struct pbs_db_svr_info {
	BIGINT  sv_jobidnumber;
	pbs_db_attr_list_t db_attr_list; /* list of attributes */
};
typedef struct pbs_db_svr_info pbs_db_svr_info_t;

/**
 * @brief
 *  Structure used to map database scheduler structure to C
 *
 */
struct pbs_db_sched_info {
	char    sched_name[PBS_MAXSCHEDNAME+1]; /* sched name */
	pbs_db_attr_list_t db_attr_list; 	/* list of attributes */
};
typedef struct pbs_db_sched_info pbs_db_sched_info_t;

/**
 * @brief
 *  Structure used to map database queue structure to C
 *
 */
struct pbs_db_que_info {
	char    qu_name[PBS_MAXQUEUENAME +1]; 	/* queue name */
	INTEGER qu_type;		 	/* queue type: exec, route */
	pbs_db_attr_list_t db_attr_list; 	/* list of attributes */
};
typedef struct pbs_db_que_info pbs_db_que_info_t;

/**
 * @brief
 *  Structure used to map database node structure to C
 *
 */
struct pbs_db_node_info {
	char	nd_name[PBS_MAXSERVERNAME+1]; /* vnode's name */
	INTEGER nd_index;	/* global node index */
	BIGINT	mom_modtime; 	/* node config update time */
	char	nd_hostname[PBS_MAXSERVERNAME+1]; /* node hostname */
	INTEGER nd_state;	/* state of node */
	INTEGER nd_ntype;	/* node type */
	char	nd_pque[PBS_MAXSERVERNAME+1]; /* queue to which it belongs */
	pbs_db_attr_list_t db_attr_list; /* list of attributes */
};
typedef struct pbs_db_node_info pbs_db_node_info_t;

/**
 * @brief
 *  Structure used to map database mominfo_time structure to C
 *
 */
struct pbs_db_mominfo_time {
	BIGINT	mit_time; /* time of the host to vnode map */
	INTEGER mit_gen;  /* generation of the host to vnode map */
};
typedef struct pbs_db_mominfo_time pbs_db_mominfo_time_t;

/**
 * @brief
 *  Structure used to map database job structure to C
 *
 */
struct pbs_db_job_info {
	char     ji_jobid[PBS_MAXSVRJOBID + 1]; /* job identifier */
	INTEGER  ji_state;	/* Internal copy of state */
	INTEGER  ji_substate;	/* job sub-state */
	INTEGER  ji_svrflags;	/* server flags */
	BIGINT   ji_stime;	/* time job started execution */	
	char     ji_queue[PBS_MAXQUEUENAME + 1];  /* name of current queue */
	char     ji_destin[PBS_MAXROUTEDEST + 1]; /* dest from qmove/route */
	INTEGER  ji_un_type;	/* job's queue type */
	INTEGER  ji_exitstat;	/* job exit status from MOM */
	BIGINT   ji_quetime;	/* time entered queue */
	BIGINT   ji_rteretry;	/* route retry time */
	INTEGER  ji_fromsock;	/* socket job coming over */
	BIGINT   ji_fromaddr;	/* host job coming from   */
	char     ji_jid[8];	/* extended job save data */
	INTEGER  ji_credtype;	/* credential type */
	BIGINT   ji_qrank;	/* sort key for db query */
	pbs_db_attr_list_t db_attr_list; /* list of attributes for database */
};
typedef struct pbs_db_job_info pbs_db_job_info_t;

/**
 * @brief
 *  Structure used to map database job script to C
 *
 */
struct pbs_db_jobscr_info {
	char     ji_jobid[PBS_MAXSVRJOBID + 1]; /* job identifier */
	TEXT     script;			/* job script */
};
typedef struct pbs_db_jobscr_info pbs_db_jobscr_info_t;

/**
 * @brief
 *  Structure used to map database resv structure to C
 *
 */
struct pbs_db_resv_info {
	char    ri_resvid[PBS_MAXSVRJOBID + 1]; /* reservation identifier */
	char    ri_queue[PBS_MAXQUEUENAME + 1]; /* queue used by reservation */
	INTEGER ri_state;	/* internal copy of state */
	INTEGER ri_substate;	/* substate of resv state */
	BIGINT  ri_stime;	/* left window boundry  */
	BIGINT  ri_etime;	/* right window boundry */
	BIGINT  ri_duration;	/* reservation duration */
	INTEGER ri_tactive;	/* time reservation became active */
	INTEGER ri_svrflags;	/* server flags */
	pbs_db_attr_list_t db_attr_list; /* list of attributes */
};
typedef struct pbs_db_resv_info pbs_db_resv_info_t;

/**
 * @brief
 *  Structure used to pass database query options to database functions
 *
 *  Flags field can be used to pass any flags to a query function.
 *  Timestamp field can be used to pass a timestamp, to return rows that have
 *  a modification timestamp newer (more recent) than the timestamp passed.
 *  (Basically to return rows that have been modified since a point of time)
 *
 */
struct pbs_db_query_options {
	int	flags;
	time_t	timestamp;
};
typedef struct pbs_db_query_options pbs_db_query_options_t;


#define PBS_DB_SVR		0
#define PBS_DB_SCHED		1
#define PBS_DB_QUEUE		2
#define PBS_DB_NODE		3
#define PBS_DB_MOMINFO_TIME	4
#define PBS_DB_JOB		5
#define PBS_DB_JOBSCR		6
#define PBS_DB_RESV		7
#define PBS_DB_NUM_TYPES	8

/* connection error code */
#define PBS_DB_SUCCESS		0
#define PBS_DB_CONNREFUSED	1
#define PBS_DB_AUTH_FAILED	2
#define PBS_DB_CONNFAILED	3
#define PBS_DB_NOMEM		4
#define PBS_DB_STILL_STARTING	5
#define PBS_DB_ERR		6
#define PBS_DB_OOM_ERR		7

/* Database connection states */
#define PBS_DB_CONNECT_STATE_NOT_CONNECTED	1
#define PBS_DB_CONNECT_STATE_CONNECTING		2
#define PBS_DB_CONNECT_STATE_CONNECTED		3
#define PBS_DB_CONNECT_STATE_FAILED		4

/* Database states */
#define PBS_DB_DOWN		1
#define PBS_DB_STARTING		2
#define PBS_DB_STARTED		3

/**
 * @brief
 *  Wrapper object structure. Contains a pointer to one of the several database
 *  structures.
 *
 *  Most of the database manipulation/query functions take this structure as a
 *  parmater. Depending on the contained structure type, an appropriate internal
 *  database manipulation/query function is eventually called. This allows to
 *  keep the interace simpler and generic.
 *
 */
struct pbs_db_obj_info {
	int 	pbs_db_obj_type; /* identifies the contained object type */
	union {
		pbs_db_svr_info_t     *pbs_db_svr;		/* map database server structure to C */
		pbs_db_sched_info_t   *pbs_db_sched;		/* map database scheduler structure to C */
		pbs_db_que_info_t     *pbs_db_que;		/* map database queue structure to C */
		pbs_db_node_info_t    *pbs_db_node;		/* map database node structure to C */
		pbs_db_mominfo_time_t *pbs_db_mominfo_tm;	/* map database mominfo_time structure to C */
		pbs_db_job_info_t     *pbs_db_job;		/* map database job structure to C */
		pbs_db_jobscr_info_t  *pbs_db_jobscr;		/* map database job script to C */
		pbs_db_resv_info_t    *pbs_db_resv;		/* map database resv structure to C */
	} pbs_db_un;
};
typedef struct pbs_db_obj_info pbs_db_obj_info_t;
typedef void (*query_cb_t)(pbs_db_obj_info_t *, int *);

#define PBS_DB_CNT_TIMEOUT_NORMAL	30
#define PBS_DB_CNT_TIMEOUT_INFINITE	0


/* Database start stop control commands */
#define PBS_DB_CONTROL_STATUS	"status"
#define PBS_DB_CONTROL_START	"start"
#define PBS_DB_CONTROL_STOP	"stop"

/**
 * @brief
 *	Initialize a database connection handle
 *      - creates a database connection handle
 *      - Initializes various fields of the connection structure
 *      - Retrieves connection password and sets the database
 *        connect string
 *
 * @param[out]  conn		- Initialized connecetion handler
 * @param[in]   host		- The name of the host on which database resides
 * @param[in]	port		- The port number where database is running
 * @param[in]   timeout		- The timeout value in seconds to attempt the connection
 *
 * @return      int
 * @retval      !0  - Failure
 * @retval      0 - Success
 *
 */
int pbs_db_connect(void **conn, char *host, int port, int timeout);

/**
 * @brief
 *	Disconnect from the database and frees all allocated memory.
 *
 * @param[in]   conn - Connected database handle
 *  
 * @return      Failure error code
 * @retval      Non-zero  - Failure
 * @retval      0 - Success
 *
 */
int pbs_db_disconnect(void *conn);

/**
 * @brief
 *	Insert a new object into the database
 *
 * @param[in]	conn - Connected database handle
 * @param[in]	pbs_db_obj_info_t - Wrapper object that describes the object
 *              (and data) to insert
 * @param[in]   savetype - Update or Insert
 *
 * @return      int
 * @retval      -1  - Failure
 * @retval       0  - success
 *
 */
int pbs_db_save_obj(void *conn, pbs_db_obj_info_t *obj, int savetype);

/**
 * @brief
 *	Delete an existing object from the database
 *
 * @param[in]	conn - Connected database handle
 * @param[in]	pbs_db_obj_info_t - Wrapper object that describes the object
 *              (and data) to delete
 *
 * @return      int
 * @retval      -1  - Failure
 * @retval       0  - success
 * @retval       1 -  Success but no rows deleted
 *
 */
int pbs_db_delete_obj(void *conn, pbs_db_obj_info_t *obj);

/**
 * @brief
 *	Delete attributes of an existing object from the database
 *
 * @param[in]	conn - Connected database handle
 * @param[in]	pbs_db_obj_info_t - The pointer to the wrapper object which
 *				describes the PBS object (job/resv/node etc) that is wrapped
 *				inside it.
 * @param[in]	obj_id - The object id of the parent (jobid, node-name etc)
 * @param[in]	db_attr_list - List of attributes to remove from DB
 *
 * @return      int
 * @retval      -1  - Failure
 * @retval       0  - success
 * @retval       1 -  Success but no rows deleted
 *
 */
int pbs_db_delete_attr_obj(void *conn, pbs_db_obj_info_t *obj, void *obj_id, pbs_db_attr_list_t *db_attr_list);

/**
 * @brief
 *	Search the database for existing objects and load the server structures.
 *
 * @param[in]	conn - Connected database handle
 * @param[in]	pbs_db_obj_info_t - The pointer to the wrapper object which
 *				describes the PBS object (job/resv/node etc) that is wrapped
 *				inside it.
 * @param[in]	pbs_db_query_options_t - Pointer to the options object that can
 *				contain the flags or timestamp which will effect the query.
 * @param[in]	callback function which will process the result from the database
 * 				and update the server strctures.
 *
 * @return	int
 * @retval	0	- Success but no rows found
 * @retval	-1	- Failure
 * @retval	>0	- Success and number of rows found
 *
 */
int pbs_db_search(void *conn, pbs_db_obj_info_t *obj, pbs_db_query_options_t *opts, query_cb_t query_cb);

/**
 * @brief
 *	Load a single existing object from the database
 *
 * @param[in]     conn - Connected database handle
 * @param[in/out] pbs_db_obj_info_t - Wrapper object that describes the object
 *                (and data) to load. This parameter used to return the data about
 *                the object loaded
 *
 * @return      int
 * @retval      -1  - Failure
 * @retval       0  - success
 * @retval       1 -  Success but no rows loaded
 *
 */
int pbs_db_load_obj(void *conn, pbs_db_obj_info_t *obj);

/**
 * @brief
 *	Function to check whether data-service is running
 *
 * @return      Error code
 * @retval	-1 - Error in routine
 * @retval	0  - Data service running
 * @retval	1  - Data service not running
 *
 */
int pbs_status_db(char *pbs_ds_host, int pbs_ds_port);

/**
 * @brief
 *	Start the database daemons/service.
 *
 * @param[out]	errmsg - returns the startup error message if any
 *
 * @return	    int
 * @retval       0     - success
 * @retval       !=0   - Failure
 *
 */
int pbs_start_db(char *pbs_ds_host, int pbs_ds_port);

/**
 * @brief
 *	Stop the database daemons/service
 *
 * @param[out]	errmsg - returns the db  error message if any
 *
 * @return      Error code
 * @retval      !=0 - Failure
 * @retval       0  - Success
 *
 */
int pbs_stop_db(char *pbs_ds_host, int pbs_ds_port);

/**
 * @brief
 *	Translates the error code to an error message
 *
 * @param[in]   err_code - Error code to translate
 * @param[out]  err_msg  - The translated error message (newly allocated memory)
 *
 */
void pbs_db_get_errmsg(int err_code, char **err_msg);

/**
 * @brief
 *	Function to create new databse user or change password of current user.
 *
 * @param[in] conn[in]: The database connection handle which was created by pbs_db_connection.
 * @param[in] user_name[in]: Databse user name.
 * @param[in] password[in]:  New password for the database.
 * @param[in] olduser[in]: old database user name.
 *
 * @retval       -1 - Failure
 * @retval        0  - Success
 *
 */
int pbs_db_password(void *conn, char *userid, char *password, char *olduser);


#ifdef	__cplusplus
}
#endif

#endif	/* _PBS_DB_H */
