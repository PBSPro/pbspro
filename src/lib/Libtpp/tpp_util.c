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
 * @file	tpp_util.c
 *
 * @brief	Miscellaneous utility routines used by the TPP library
 *
 *
 */
#include <pbs_config.h>
#if RWLOCK_SUPPORT == 2
#if !defined (_XOPEN_SOURCE)
#define _XOPEN_SOURCE 500
#endif
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <stdarg.h>
#include <ctype.h>
#include "pbs_idx.h"
#include "pbs_error.h"
#include "tpp_internal.h"
#include "dis.h"
#ifdef PBS_COMPRESSION_ENABLED
#include <zlib.h>
#endif

#define BACKTRACE_SIZE 100
#include <execinfo.h>

/*
 *	Global Variables
 */

#ifndef WIN32
pthread_mutex_t tpp_nslookup_mutex = PTHREAD_MUTEX_INITIALIZER;
#endif

/* TLS data for each TPP thread */
static pthread_key_t tpp_key_tls;
static pthread_once_t tpp_once_ctrl = PTHREAD_ONCE_INIT; /* once ctrl to initialize tls key */

long tpp_log_event_mask = 0;

/* default keepalive values */
#define DEFAULT_TCP_KEEPALIVE_TIME 30
#define DEFAULT_TCP_KEEPALIVE_INTVL 10
#define DEFAULT_TCP_KEEPALIVE_PROBES 3
#define DEFAULT_TCP_USER_TIMEOUT 60000

#define PBS_TCP_KEEPALIVE "PBS_TCP_KEEPALIVE" /* environment string to search for */

/* extern functions called from this file into the tpp_transport.c */
static pbs_tcp_chan_t * tppdis_get_user_data(int sd);

void
tpp_auth_logger(int type, int objclass, int severity, const char *objname, const char *text)
{
	tpp_log(severity, objname, (char *)text);
}

/**
 * @brief
 *	Get the user buffer associated with the tpp channel. If no buffer has
 *	been set, then allocate a tppdis_chan structure and associate with
 *	the given tpp channel
 *
 * @param[in] - fd - Tpp channel to which to get/associate a user buffer
 *
 * @retval	NULL - Failure
 * @retval	!NULL - Buffer associated with the tpp channel
 *
 * @par Side Effects:
 *	None
 *
 * @par MT-safe: No
 *
 */
static pbs_tcp_chan_t *
tppdis_get_user_data(int fd)
{
	void *data = tpp_get_user_data(fd);
	if (data == NULL) {
		if (errno != ENOTCONN) {
			/* fd connected, but first time - so call setup */
			dis_setup_chan(fd, (pbs_tcp_chan_t * (*)(int))&tpp_get_user_data);
			/* get the buffer again*/
			data = tpp_get_user_data(fd);
		}
	}
	return (pbs_tcp_chan_t *)data;
}

/**
 * @brief
 *	Setup dis function pointers to point to tpp_dis routines
 *
 * @par Side Effects:
 *	None
 *
 * @par MT-safe: No
 *
 */
void
DIS_tpp_funcs()
{
	pfn_transport_get_chan = tppdis_get_user_data;
	pfn_transport_set_chan = (int (*)(int, pbs_tcp_chan_t *)) &tpp_set_user_data;
	pfn_transport_recv = tpp_recv;
	pfn_transport_send = tpp_send;
}


/**
 * @brief
 *		This is the log handler for tpp implemented in the daemon. The pointer to
 *		this function is used by the Libtpp layer when it needs to log something to
 *		the daemon logs
 *
 * @param[in]	level   - Logging level
 * @param[in]	objname - Name of the object about which logging is being done
 * @param[in]	mess    - The log message
 *
 */
void
tpp_log(int level, const char *routine, const char *fmt, ...)
{
	char id[2 * PBS_MAXHOSTNAME];
	char func[PBS_MAXHOSTNAME];
	int thrd_index;
	int etype;
	int len;
	char logbuf[LOG_BUF_SIZE];
	char *buf;
	va_list args;

#ifdef TPPDEBUG
	level = LOG_CRIT; /* for TPPDEBUG mode force all logs message */
#endif
	etype = log_level_2_etype(level);

	func[0] = '\0';
	if (routine)
		snprintf(func, sizeof(func), ";%s", routine);

	thrd_index = tpp_get_thrd_index();
	if (thrd_index == -1)
		snprintf(id, sizeof(id), "%s(Main Thread)%s", msg_daemonname ? msg_daemonname : "", func);
	else
		snprintf(id, sizeof(id), "%s(Thread %d)%s", msg_daemonname ? msg_daemonname : "", thrd_index, func);

	va_start(args, fmt);

	len = vsnprintf(logbuf, sizeof(logbuf), fmt, args);

	if (len >= sizeof(logbuf)) {
		buf = pbs_asprintf_format(len, fmt, args);
		if (buf == NULL) {
			va_end(args);
			return;
		}
	} else
		buf = logbuf;

	log_event(etype, PBS_EVENTCLASS_TPP, level, id, buf);

	if (len >= sizeof(logbuf))
		free(buf);

	va_end(args);
}

/**
 * @brief
 *	Helper function called by PBS daemons to set the tpp configuration to
 *	be later used during tpp_init() call.
 *
 * @param[in] pbs_conf - Pointer to the pbs_config structure
 * @param[out] tpp_conf - The tpp configuration structure duly filled based on
 *			  the input parameters
 * @param[in] nodenames - The comma separated list of name of this side of the communication.
 * @param[in] port     - The port at which this side is identified.
 * @param[in] routers  - Array of router addresses ended by a null entry
 *			 router addresses are of the form "host:port"
 *
 * @retval Error code
 * @return -1 - Failure
 * @return  0 - Success
 *
 * @par Side Effects:
 *	None
 *
 * @par MT-safe: No
 *
 */
int
set_tpp_config(struct pbs_config *pbs_conf, struct tpp_config *tpp_conf, char *nodenames, int port, char *r)
{
	int i, end;
	int num_routers = 0;
	char *routers = NULL;
	char *s, *t, *ctx;
	char *nm;
	int len, hlen;
	char *token, *saveptr, *tmp;
	char *formatted_names = NULL;

	/* before doing anything else, initialize the key to the tls
	 * its okay to call this function multiple times since it
	 * uses a pthread_once functionality to initialize key only once
	 */
	if (tpp_init_tls_key() != 0) {
		/* can only use prints since tpp key init failed */
		fprintf(stderr, "Failed to initialize tls key\n");
		return -1;
	}

	if (r) {
		routers = strdup(r);
		if (!routers) {
			tpp_log(LOG_CRIT, __func__, "Out of memory allocating routers");
			return -1;
		}
	}

	if (!nodenames) {
		tpp_log(LOG_CRIT, NULL, "TPP node name not set");
		return -1;
	}

	if (port == -1) {
		struct sockaddr_in in;
		int sd;
		int rc;
		tpp_addr_t *addr;

		if ((sd = tpp_sock_socket(AF_INET, SOCK_STREAM, 0)) == -1) {
			tpp_log(LOG_ERR, __func__, "tpp_sock_socket() error, errno=%d", errno);
			return -1;
		}

		/* bind this socket to a reserved port */
		in.sin_family = AF_INET;
		in.sin_addr.s_addr = INADDR_ANY;
		in.sin_port = 0;
		memset(&(in.sin_zero), '\0', sizeof(in.sin_zero));
		if ((rc = tpp_sock_bind(sd, (struct sockaddr *) &in, sizeof(in))) == -1) {
			tpp_log(LOG_ERR, __func__, "tpp_sock_bind() error, errno=%d", errno);
			tpp_sock_close(sd);
			return -1;
		}

		addr = tpp_get_local_host(sd);
		if (addr) {
			port = addr->port;
			free(addr);
		}

		if (port == -1) {
			tpp_log(LOG_ERR, __func__, "TPP client could not detect port to use");
			tpp_sock_close(sd);
			return -1;
		}
		/* don't close this socket */
		tpp_set_close_on_exec(sd);
	}

	/* add port information to the node names and format into a single string as desired by TPP */
	len = 0;
	token = strtok_r(nodenames, ",", &saveptr);
	while (token) {
		nm = mk_hostname(token, port);
		if (!nm) {
			tpp_log(LOG_CRIT, NULL, "Failed to make node name");
			return -1;
		}

		hlen = strlen(nm);
		if ((tmp = realloc(formatted_names, len + hlen + 2)) == NULL) { /* 2 for command and null char */
			tpp_log(LOG_CRIT, NULL, "Failed to make formatted node name");
			return -1;
		}

		formatted_names = tmp;

		if (len == 0) {
			strcpy(formatted_names, nm);
		} else {
			strcat(formatted_names, ",");
			strcat(formatted_names, nm);
		}
		free(nm);

		len += hlen + 2;

		token = strtok_r(NULL, ",", &saveptr);
	}

	tpp_conf->node_name = formatted_names;
	tpp_conf->node_type = TPP_LEAF_NODE;
	tpp_conf->numthreads = 1;

	tpp_conf->auth_config = make_auth_config(pbs_conf->auth_method,
							pbs_conf->encrypt_method,
							pbs_conf->pbs_exec_path,
							pbs_conf->pbs_home_path,
							(void *)tpp_auth_logger);
	if (tpp_conf->auth_config == NULL) {
		tpp_log(LOG_CRIT, __func__, "Out of memory allocating auth config");
		return -1;
	}

	tpp_log(LOG_INFO, NULL, "TPP authentication method = %s", tpp_conf->auth_config->auth_method);
	if (tpp_conf->auth_config->encrypt_method[0] != '\0')
		tpp_log(LOG_INFO, NULL, "TPP encryption method = %s", tpp_conf->auth_config->encrypt_method);


	if ((tpp_conf->supported_auth_methods = dup_string_arr(pbs_conf->supported_auth_methods)) == NULL) {
		tpp_log(LOG_CRIT, __func__, "Out of memory while making copy of supported auth methods");
		return -1;
	}

#ifdef PBS_COMPRESSION_ENABLED
	tpp_conf->compress = pbs_conf->pbs_use_compression;
#else
	tpp_conf->compress = 0;
#endif

	/* set default parameters for keepalive */
	tpp_conf->tcp_keepalive = 1;
	tpp_conf->tcp_keep_idle = DEFAULT_TCP_KEEPALIVE_TIME;
	tpp_conf->tcp_keep_intvl = DEFAULT_TCP_KEEPALIVE_INTVL;
	tpp_conf->tcp_keep_probes = DEFAULT_TCP_KEEPALIVE_PROBES;
	tpp_conf->tcp_user_timeout = DEFAULT_TCP_USER_TIMEOUT;

	/* if set, read them from environment variable PBS_TCP_KEEPALIVE */
	if ((s = getenv(PBS_TCP_KEEPALIVE))) {
		/*
		 * The format is a comma separated list of values in order, for the following variables,
		 * tcp_keepalive_enable,tcp_keepalive_time,tcp_keepalive_intvl,tcp_keepalive_probes,tcp_user_timeout
		 */
		tpp_conf->tcp_keepalive = 0;
		t = strtok_r(s, ",", &ctx);
		if (t) {
			/* this has to be the tcp_keepalive_enable value */
			if (atol(t) == 1) {
				tpp_conf->tcp_keepalive = 1;

				/* parse other values only if this is enabled */
				if ((t = strtok_r(NULL, ",", &ctx))) {
					/* tcp_keepalive_time */
					tpp_conf->tcp_keep_idle = (int) atol(t);
				}

				if (t && (t = strtok_r(NULL, ",", &ctx))) {
					/* tcp_keepalive_intvl */
					tpp_conf->tcp_keep_intvl = (int) atol(t);
				}

				if (t && (t = strtok_r(NULL, ",", &ctx))) {
					/* tcp_keepalive_probes */
					tpp_conf->tcp_keep_probes = (int) atol(t);
				}

				if (t && (t = strtok_r(NULL, ",", &ctx))) {
					/*tcp_user_timeout */
					tpp_conf->tcp_user_timeout = (int) atol(t);
				}

				/* emit a log depicting what we are going to use as keepalive */
				tpp_log(LOG_CRIT, NULL,
						"Using tcp_keepalive_time=%d, tcp_keepalive_intvl=%d, tcp_keepalive_probes=%d, tcp_user_timeout=%d",
						tpp_conf->tcp_keep_idle, tpp_conf->tcp_keep_intvl, tpp_conf->tcp_keep_probes, tpp_conf->tcp_user_timeout);
			} else {
				tpp_log(LOG_CRIT, NULL, "tcp keepalive disabled");
			}
		}
	}

	tpp_conf->buf_limit_per_conn = 5000; /* size in KB, TODO: load from pbs.conf */

	if (routers && routers[0] != '\0') {
		char *p = routers;
		char *q;

		num_routers = 1;

		while (*p) {
			if (*p == ',')
				num_routers++;
			p++;
		}

		tpp_conf->routers = calloc(num_routers + 1, sizeof(char *));
		if (!tpp_conf->routers) {
			tpp_log(LOG_CRIT, __func__, "Out of memory allocating routers array");
			return -1;
		}

		q = p = routers;
		i = end = 0;
		while (!end) {
			if (!*p)
				end = 1;
			if ((*p && *p == ',') || end) {
				*p = 0;
				while (isspace(*q))
					q++;
				nm = mk_hostname(q, TPP_DEF_ROUTER_PORT);
				if (!nm) {
					tpp_log(LOG_CRIT, NULL, "Failed to make router name");
					return -1;
				}
				tpp_conf->routers[i++] = nm;
				q = p + 1;
			}
			if (!end)
				p++;
		}

	} else {
		tpp_conf->routers = NULL;
	}

	for (i = 0; i < num_routers; i++) {
		if (tpp_conf->routers[i] == NULL || strcmp(tpp_conf->routers[i], tpp_conf->node_name) == 0) {
			tpp_log(LOG_CRIT, NULL, "Router name NULL or points to same node endpoint %s", (tpp_conf->routers[i]) ?(tpp_conf->routers[i]) : "");
			return -1;
		}
	}

	if (routers)
		free(routers);

	return 0;
}

/* 
 * free tpp conf member variables
 * to be called before exit
 * 
 * @param[in] tpp_conf - pointer to the tpp conf structure
 * 
 */
void
free_tpp_config(struct tpp_config *tpp_conf)
{
	free(tpp_conf->routers);
	free_string_array(tpp_conf->supported_auth_methods);
	free(tpp_conf->node_name);
	free_auth_config(tpp_conf->auth_config);
}

/**
 * @brief tpp_make_authdata - allocate conn_auth_t structure based given values
 *
 * @param[in] tpp_conf - pointer to tpp config structure
 * @param[in] conn_type - one of AUTH_CLIENT or AUTH_SERVER
 * @param[in] auth_method - auth method name
 * @param[in] encrypt_method - encrypt method name
 *
 * @return conn_auth_t *
 * @return !NULL - success
 * @return NULL  - failure
 */
conn_auth_t *
tpp_make_authdata(struct tpp_config *tpp_conf, int conn_type, char *auth_method, char *encrypt_method)
{
	conn_auth_t *authdata = NULL;

	if ((authdata = (conn_auth_t *)calloc(1, sizeof(conn_auth_t))) == NULL) {
		tpp_log(LOG_CRIT, __func__, "Out of memory");
		return NULL;
	}
	authdata->conn_type = conn_type;
	authdata->config = make_auth_config(auth_method,
						encrypt_method,
						tpp_conf->auth_config->pbs_exec_path,
						tpp_conf->auth_config->pbs_home_path,
						tpp_conf->auth_config->logfunc);
	if (authdata->config == NULL) {
		tpp_log(LOG_CRIT, __func__, "Out of memory");
		return NULL;
	}

	return authdata;
}

/**
 * @brief tpp_handle_auth_handshake - initiate handshake or process incoming handshake data
 *
 * @param[in] tfd - file descriptor
 * @param[in] conn_fd - connection fd for sending data
 * @param[in] authdata - pointer to conn auth data struct associated with tfd
 * @param[in] for_encrypt - whether to handle incoming data for encrypt/decrypt or for authentication
 * @param[in] data_in - incoming handshake data (if any)
 * @param[in] len_in - length of data_in else 0
 *
 * @return int
 * @return -1 - failure
 * @return 0  - need handshake continuation
 * @return 1  - handshake completed
 */
int
tpp_handle_auth_handshake(int tfd, int conn_fd, conn_auth_t *authdata, int for_encrypt, void *data_in, size_t len_in)
{
	void *data_out = NULL;
	size_t len_out = 0;
	int is_handshake_done = 0;
	void *authctx = NULL;
	auth_def_t *authdef = NULL;

	if (authdata == NULL) {
		tpp_log(LOG_CRIT, __func__, "tfd=%d, No auth data found", tfd);
		return -1;
	}

	if (for_encrypt == FOR_AUTH) {
		if (authdata->authdef == NULL) {
			authdef = get_auth(authdata->config->auth_method);
			if (authdef == NULL) {
				tpp_log(LOG_CRIT, __func__, "Failed to find authdef");
				return -1;
			}
			authdata->authdef = authdef;
			authdef->set_config((const pbs_auth_config_t *)(authdata->config));
			if (authdef->create_ctx(&(authdata->authctx), authdata->conn_type, AUTH_SERVICE_CONN, tpp_transport_get_conn_hostname(tfd))) {
				tpp_log(LOG_CRIT, __func__, "Failed to create auth context");
				return -1;
			}

		}
		authdef = authdata->authdef;
		authctx = authdata->authctx;
	} else {
		if (authdata->encryptdef == NULL) {
			authdef = get_auth(authdata->config->encrypt_method);
			if (authdef == NULL) {
				tpp_log(LOG_CRIT, __func__, "Failed to find authdef");
				return -1;
			}
			authdata->encryptdef = authdef;
			authdef->set_config((const pbs_auth_config_t *)(authdata->config));
			if (authdef->create_ctx(&(authdata->encryptctx), authdata->conn_type, AUTH_SERVICE_CONN, tpp_transport_get_conn_hostname(tfd))) {
				tpp_log(LOG_CRIT, __func__, "Failed to create encrypt context");
				return -1;
			}

		}
		authdef = authdata->encryptdef;
		authctx = authdata->encryptctx;
	}
	tpp_transport_set_conn_extra(tfd, authdata);

	if (authdef->process_handshake_data(authctx, data_in, len_in, &data_out, &len_out, &is_handshake_done) != 0) {
		if (len_out > 0) {
			tpp_log(LOG_CRIT, __func__, (char *)data_out);
			free(data_out);
		}
		return -1;
	}

	if (len_out > 0) {
		tpp_auth_pkt_hdr_t *ahdr = NULL;
		tpp_packet_t *pkt = NULL;

		pkt = tpp_bld_pkt(NULL, NULL, sizeof(tpp_auth_pkt_hdr_t), 1, (void **) &ahdr);
		if (!pkt) {
			tpp_log(LOG_CRIT, __func__, "Failed to build packet");
			free(data_out);
			return -1;
		}
		ahdr->type = TPP_AUTH_CTX;
		ahdr->for_encrypt = for_encrypt;
		strcpy(ahdr->auth_method, authdata->config->auth_method);
		strcpy(ahdr->encrypt_method, authdata->config->encrypt_method);

		if (!tpp_bld_pkt(pkt, data_out, len_out, 0, NULL)) {
			tpp_log(LOG_CRIT, __func__, "Failed to build packet");
			free(data_out);
			return -1;
		}

		if (tpp_transport_vsend(conn_fd, pkt) != 0) {
			tpp_log(LOG_CRIT, __func__, "tpp_transport_vsend failed, err=%d", errno);
			return -1;
		}
	}

	/*
	 * We didn't send any handshake data and handshake is not completed
	 * so error out as we should send some handshake data
	 * or handshake should be completed
	 */
	if (is_handshake_done == 0 && len_out == 0) {
		tpp_log(LOG_CRIT, __func__, "Auth handshake failed");
		return -1;
	}

	if (is_handshake_done != 1)
		return 0;
	return 1;
}

/**
 * @brief
 *	Create a packet structure from the inputs provided
 *
 * @param[in] - pkt  - Pointer to packet to add chunk, or create new packet if NULL
 * @param[in] - data - pointer to data buffer (if NULL provided, no copy happens)
 * @param[in] - len  - Lentgh of data buffer
 * @param[in] - dup  - Make a copy of the data provided?
 * @param[in] - dup_data  - Ptr to copy of data created, if dup is true
 *
 * @return Newly allocated packet structure
 * @retval NULL - Failure (Out of memory)
 * @retval !NULL - Address of allocated packet structure
 *
 * @par Side Effects:
 *	None
 *
 * @par MT-safe: Yes
 *
 */
tpp_packet_t *
tpp_bld_pkt(tpp_packet_t *pkt, void *data, int len, int dup, void **dup_data)
{
	tpp_chunk_t *chunk;
	void *d = data;

	/* first create the requested chunk for the packet */
	if ((chunk = malloc(sizeof(tpp_chunk_t))) == NULL) {
		tpp_log(LOG_CRIT, __func__, "Failed to build chunk");
		tpp_free_pkt(pkt);
		return NULL;
	}
	/* dup flag was provided, so allocate space */
	if (dup) {
		d = malloc(len);
		if (!d) {
			tpp_log(LOG_CRIT, __func__, "Out of memory allocating packet duplicate data for chunk");
			free(chunk);
			tpp_free_pkt(pkt);
			return NULL;
		}
		if (data)
			memcpy(d, data, len);
		if (dup_data)
			*dup_data = d; /* return allocated data ptr */
	}
	chunk->data = d;
	chunk->pos = chunk->data;
	chunk->len = len;
	CLEAR_LINK(chunk->chunk_link);

	/* add chunk to packet */
	/* if packet NULL, create packet now and add chunk */
	if (pkt == NULL) {
		if ((pkt = malloc(sizeof(tpp_packet_t))) == NULL) {
			if (d != data)
				free(d);
			tpp_free_pkt(pkt);
			tpp_log(LOG_CRIT, __func__, "Out of memory allocating packet");
			return NULL;
		}
		CLEAR_HEAD(pkt->chunks);
		pkt->ref_count = 1;
		pkt->totlen = 0;
		pkt->curr_chunk = chunk;
	}

	pkt->totlen += len;
	append_link(&pkt->chunks, &chunk->chunk_link, chunk);

	return pkt;
}

/**
 * @brief
 *	Free a chunk
 *
 * @param[in] - chunk - Ptr to the chunk to be freed.
 *
 * @par Side Effects:
 *	None
 *
 * @par MT-safe: Yes
 *
 */
void
tpp_free_chunk(tpp_chunk_t *chunk)
{
	if (chunk) {
		delete_link(&chunk->chunk_link);
		free(chunk->data);
		free(chunk);
	}
}

/**
 * @brief
 *	Free a packet structure
 *
 * @param[in] - pkt - Ptr to the packet to be freed.
 *
 * @par Side Effects:
 *	None
 *
 * @par MT-safe: Yes
 *
 */
void
tpp_free_pkt(tpp_packet_t *pkt)
{
	if (pkt) {
		pkt->ref_count--;

		if (pkt->ref_count <= 0) {
			tpp_chunk_t *chunk;
			while((chunk = GET_NEXT(pkt->chunks)))
				tpp_free_chunk(chunk);
			free(pkt);
		}
	}
}

/**
 * @brief
 *	Mark a file descriptor as non-blocking
 *
 * @param[in] - fd - The file descriptor
 *
 * @return	Error code
 * @retval -1	Failure
 * @retval  0	Success
 *
 * @par Side Effects:
 *	None
 *
 * @par MT-safe: Yes
 *
 */
int
tpp_set_non_blocking(int fd)
{
	int flags;

	/* If they have O_NONBLOCK, use the Posix way to do it */
#if defined(O_NONBLOCK)
	if (-1 == (flags = fcntl(fd, F_GETFL, 0)))
		flags = 0;
	return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
#elif defined(WIN32)
	flags=1;
	if (ioctlsocket(fd, FIONBIO, &flags) == SOCKET_ERROR)
		return -1;
	return 0;
#else
	/* Otherwise, use the old way of doing it */
	flags = 1;
	return ioctl(fd, FIOBIO, &flags);
#endif
}

/**
 * @brief
 *	Mark a file descriptor with close on exec flag
 *
 * @param[in] - fd - The file descriptor
 *
 * @return	Error code
 * @retval -1	Failure
 * @retval  0	Success
 *
 * @par Side Effects:
 *	None
 *
 * @par MT-safe: Yes
 *
 */
int
tpp_set_close_on_exec(int fd)
{
#ifndef WIN32
	int flags;
	if ((flags = fcntl(fd, F_GETFD)) != -1)
		fcntl(fd, F_SETFD, flags | FD_CLOEXEC);
#endif
	return 0;
}

/**
 * @brief
 *	Mark a socket descriptor as keepalive
 *
 * @param[in] - fd - The socket descriptor
 *
 * @return	Error code
 * @retval -1	Failure
 * @retval  0	Success
 *
 * @par Side Effects:
 *	None
 *
 * @par MT-safe: Yes
 *
 */
int
tpp_set_keep_alive(int fd, struct tpp_config *cnf)
{
	int optval = 1;
	pbs_socklen_t optlen;

	if (cnf->tcp_keepalive == 0)
		return 0; /* not using keepalive, return success */

	optlen = sizeof(optval);

#ifdef SO_KEEPALIVE
	optval = cnf->tcp_keepalive;
	if (tpp_sock_setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &optval, optlen) < 0) {
		tpp_log(LOG_CRIT, __func__, "setsockopt(SO_KEEPALIVE) errno=%d", errno);
		return -1;
	}
#endif

#ifndef WIN32
#ifdef TCP_KEEPIDLE
	optval = cnf->tcp_keep_idle;
	if (tpp_sock_setsockopt(fd, IPPROTO_TCP, TCP_KEEPIDLE, &optval, optlen) < 0) {
		tpp_log(LOG_CRIT, __func__, "setsockopt(TCP_KEEPIDLE) errno=%d", errno);
		return -1;
	}
#endif

#ifdef TCP_KEEPINTVL
	optval = cnf->tcp_keep_intvl;
	if (tpp_sock_setsockopt(fd, IPPROTO_TCP, TCP_KEEPINTVL, &optval, optlen) < 0) {
		tpp_log(LOG_CRIT, __func__, "setsockopt(TCP_KEEPINTVL) errno=%d", errno);
		return -1;
	}
#endif

#ifdef TCP_KEEPCNT
	optval = cnf->tcp_keep_probes;
	if (tpp_sock_setsockopt(fd, IPPROTO_TCP, TCP_KEEPCNT, &optval, optlen) < 0) {
		tpp_log(LOG_CRIT, __func__, "setsockopt(TCP_KEEPCNT) errno=%d", errno);
		return -1;
	}
#endif

#endif /*for win32*/

	return 0;
}

/**
 * @brief
 *	Create a posix thread
 *
 * @param[in] - start_routine - The threads routine
 * @param[out] - id - The thread id is returned in this field
 * @param[in] - data - The ptr to be passed to the thread routine
 *
 * @return	Error code
 * @retval -1	Failure
 * @retval  0	Success
 *
 * @par Side Effects:
 *	None
 *
 * @par MT-safe: Yes
 *
 */
int
tpp_cr_thrd(void *(*start_routine)(void*), pthread_t *id, void *data)
{
	pthread_attr_t *attr = NULL;
	int rc = -1;
#ifndef WIN32
	pthread_attr_t setattr;
	size_t stack_size;

	attr = &setattr;
	if (pthread_attr_init(attr) != 0) {
		tpp_log(LOG_CRIT, __func__, "Failed to initialize attribute");
		return -1;
	}
	if (pthread_attr_getstacksize(attr, &stack_size) != 0) {
		tpp_log(LOG_CRIT, __func__, "Failed to get stack size of thread");
		return -1;
	} else	{
		if (stack_size < MIN_STACK_LIMIT) {
			if (pthread_attr_setstacksize(attr, MIN_STACK_LIMIT) != 0) {
				tpp_log(LOG_CRIT, __func__, "Failed to set stack size for thread");
				return -1;
			}
		} else {
			if (pthread_attr_setstacksize(attr, stack_size) != 0) {
				tpp_log(LOG_CRIT, __func__, "Failed to set stack size for thread");
				return -1;
			}
		}
	}
#endif
	if (pthread_create(id, attr, start_routine, data) == 0)
		rc = 0;

#ifndef WIN32
	if (pthread_attr_destroy(attr) != 0) {
		tpp_log(LOG_CRIT, __func__, "Failed to destroy attribute");
		return -1;
	}
#endif
	return rc;
}

/**
 * @brief
 *	Initialize a pthread mutex
 *
 * @param[in] - lock - A pthread mutex variable to initialize
 *
 * @par Side Effects:
 *	None
 *
 * @par MT-safe: Yes
 *
 * @return error code
 * @retval	1 failure
 * @retval	0	success
 */
int
tpp_init_lock(pthread_mutex_t *lock)
{
	pthread_mutexattr_t attr;
	int type;

	if (pthread_mutexattr_init(&attr) != 0) {
		tpp_log(LOG_CRIT, __func__, "Failed to initialize mutex attr");
		return 1;
	}
#if defined (linux)
	type = PTHREAD_MUTEX_RECURSIVE_NP;
#else
	type = PTHREAD_MUTEX_RECURSIVE;
#endif
	if (pthread_mutexattr_settype(&attr, type)) {
		tpp_log(LOG_CRIT, __func__, "Failed to set mutex type");
		return 1;
	}

	if (pthread_mutex_init(lock, &attr) != 0) {
		tpp_log(LOG_CRIT, __func__, "Failed to initialize mutex");
		return 1;
	}

	return 0;
}

/**
 * @brief
 *	Destroy a pthread mutex
 *
 * @param[in] - lock - The pthread mutex to destroy
 *
 * @par Side Effects:
 *	None
 *
 * @par MT-safe: Yes
 *
 * @return 	error code
 * @retval	1	failure
 * @retval	0	success
 */
int
tpp_destroy_lock(pthread_mutex_t *lock)
{
	if (pthread_mutex_destroy(lock) != 0) {
		tpp_log(LOG_CRIT, __func__, "Failed to destroy mutex");
		return 1;
	}
	return 0;
}

/**
 * @brief
 *	Acquire lock on a mutex
 *
 * @param[in] - lock - ptr to a mutex variable
 *
 * @par Side Effects:
 *	None
 *
 * @par MT-safe: Yes
 *
 * @return	error code
 * @retval	1	failure
 * @retval	0	success
 */
int
tpp_lock(pthread_mutex_t *lock)
{
	if (pthread_mutex_lock(lock) != 0) {
		tpp_log(LOG_CRIT, __func__, "Failed to lock mutex");
		return 1;
	}
	return 0;
}

/**
 * @brief
 *	Release lock on a mutex
 *
 * @param[in] - lock - ptr to a mutex variable
 *
 * @par Side Effects:
 *	None
 *
 * @par MT-safe: Yes
 *
 * @return	error code
 * @retval	1	failure
 * @retval	0	success
 */
int
tpp_unlock(pthread_mutex_t *lock)
{
	if (pthread_mutex_unlock(lock) != 0) {
		tpp_log(LOG_CRIT, __func__, "Failed to unlock mutex");
		return 1;
	}
	return 0;
}

/**
 * @brief
 *	Initialize a rw lock
 *
 * @param[in] - lock - A pthread rw variable to initialize
 *
 * @par Side Effects:
 *	None
 *
 * @par MT-safe: Yes
 *
 * @return	error code
 * @retval	1	failure
 * @retval	0	success
 */
int
tpp_init_rwlock(void *lock)
{
	if (pthread_rwlock_init(lock, NULL) != 0) {
		tpp_log(LOG_CRIT, __func__, "Failed to initialize rw lock");
		return 1;
	}
	return 0;
}

/**
 * @brief
 *	Acquire read lock on a rw lock
 *
 * @param[in] - lock - ptr to a rw lock variable
 *
 * @par Side Effects:
 *	None
 *
 * @par MT-safe: Yes
 *
 * @return	error code
 * @retval	1	failure
 * @retval	0	success
 */
int
tpp_read_lock(void *lock)
{
	if (pthread_rwlock_rdlock(lock) != 0) {
		tpp_log(LOG_CRIT, __func__, "Failed in rdlock");
		return 1;
	}
	return 0;
}

/**
 * @brief
 *	Acquire write lock on a rw lock
 *
 * @param[in] - lock - ptr to a rw lock variable
 *
 * @par Side Effects:
 *	None
 *
 * @par MT-safe: Yes
 *
 */
int
tpp_write_lock(void *lock)
{
	if (pthread_rwlock_wrlock(lock) != 0) {
		tpp_log(LOG_CRIT, __func__, "Failed to wrlock");
		return 1;
	}
	return 0;
}

/**
 * @brief
 *	Unlock an rw lock
 *
 * @param[in] - lock - ptr to a rw lock variable
 *
 * @par Side Effects:
 *	None
 *
 * @par MT-safe: Yes
 *
 * @return	error code
 * @retval	1	failure
 * @retval	0	success
 */
int
tpp_unlock_rwlock(void *lock)
{
	if (pthread_rwlock_unlock(lock) != 0) {
		tpp_log(LOG_CRIT, __func__, "Failed to unlock rw lock");
		return 1;
	}
	return 0;
}

/**
 * @brief
 *	Destroy a rw lock
 *
 * @param[in] - lock - The rw to destroy
 *
 * @par Side Effects:
 *	None
 *
 * @par MT-safe: Yes
 *
 * * @return	error code
 * @retval	1	failure
 * @retval	0	success
 */
int
tpp_destroy_rwlock(void *lock)
{
	if (pthread_rwlock_destroy(lock) != 0) {
		tpp_log(LOG_CRIT, __func__, "Failed to destroy rw lock");
		return 1;
	}
	return 0;
}

/**
 * @brief
 *	Parse a hostname:port format and break into host and port portions.
 *	If port is not available, set the default port to DEF_TPP_ROUTER_PORT.
 *
 * @param[in] - full - The full hostname (host:port)
 * @param[out] - port - The port extracted from the full hostname
 *
 * @return	hostname part
 * @retval NULL Failure
 * @retval !NULL hostname
 *
 * @par Side Effects:
 *	None
 *
 * @par MT-safe: Yes
 *
 */
char *
tpp_parse_hostname(char *full, int *port)
{
	char *p;
	char *host = NULL;

	*port = TPP_DEF_ROUTER_PORT;
	if ((host = strdup(full)) == NULL)
		return NULL;

	if ((p = strstr(host, ":"))) {
		*p = '\0';
		*port = atol(p + 1);
	}
	return host;
}

/**
 * @brief
 *	Enqueue a node to a queue
 *
 * Linked List is from right to left
 * Insertion (tail) is at the right
 * and Deletion (head) is at left
 *
 * head ---> node ---> node ---> tail
 *
 * ---> Next points to right
 *
 * @param[in] - l - The address of the queue
 * @param[in] - data   - Data to be added as a node to the queue
 *
 * @return	The ptr to the newly created queue node
 * @retval	NULL - Failed to enqueue data (out of memory)
 * @retval	!NULL - Ptr to the newly created node
 *
 * @par Side Effects:
 *	None
 *
 * @par MT-safe: Yes
 *
 */
tpp_que_elem_t *
tpp_enque(tpp_que_t *l, void *data)
{
	tpp_que_elem_t *nd;

	if ((nd = malloc(sizeof(tpp_que_elem_t))) == NULL) {
		return NULL;
	}
	nd->queue_data = data;

	if (l->tail) {
		nd->prev = l->tail;
		nd->next = NULL;
		l->tail->next = nd;
		l->tail = nd;
	} else {
		l->tail = nd;
		l->head = nd;
		nd->next = NULL;
		nd->prev = NULL;
	}
	return nd;
}

/**
 * @brief
 *	De-queue (remove) a node from a queue
 *
 * Linked List is from right to left
 * Insertion (tail) is at the right
 * and Deletion (head) is at left
 *
 * head ---> node ---> node ---> tail
 *
 * ---> Next points to right
 *
 * @param[in] - l - The address of the queue
 *
 * @return	The ptr to the data from the node just removed from queue
 * @retval	NULL - Queue is empty
 * @retval	!NULL - Ptr to the data
 *
 * @par Side Effects:
 *	None
 *
 * @par MT-safe: Yes
 *
 */
void *
tpp_deque(tpp_que_t *l)
{
	void *data = NULL;
	tpp_que_elem_t *p;
	if (l->head) {
		data = l->head->queue_data;
		p = l->head;
		l->head = l->head->next;
		if (l->head)
			l->head->prev = NULL;
		else
			l->tail = NULL;
		free(p);
	}
	return data;
}

/**
 * @brief
 *	Delete a specific node from a queue
 *
 * Linked List is from right to left
 * Insertion (tail) is at the right
 * and Deletion (head) is at left
 *
 * head ---> node ---> node ---> tail
 *
 * ---> Next points to right
 *
 * @param[in] - l - The address of the queue
 * @param[in] - n - Ptr of the node to remove
 *
 * @return	The ptr to the previous node in the queue (or NULL)
 * @retval	NULL - Failed to enqueue data (out of memory)
 * @retval	!NULL - Ptr to the previous node
 *
 * @par Side Effects:
 *	None
 *
 * @par MT-safe: Yes
 *
 */
tpp_que_elem_t*
tpp_que_del_elem(tpp_que_t *l, tpp_que_elem_t *n)
{
	tpp_que_elem_t *p = NULL;
	if (n) {
		if (n->next) {
			n->next->prev = n->prev;
		}
		if (n->prev) {
			n->prev->next = n->next;
		}

		if (n == l->head) {
			l->head = n->next;
		}
		if (n == l->tail) {
			l->tail = n->prev;
		}
		if (l->head == NULL || l->tail == NULL) {
			l->tail = NULL;
			l->head = NULL;
		}
		if (n->prev)
			p = n->prev;
		/* else return p as NULL, so list QUE_NEXT starts from head again */
		free(n);
	}
	return p;
}

/**
 * @brief
 *	Insert a node a specific position in the queue
 *
 * Linked List is from right to left
 * Insertion (tail) is at the right
 * and Deletion (head) is at left
 *
 * head ---> node ---> node ---> tail
 *
 * ---> Next points to right
 *
 * @param[in] - l - The address of the queue
 * @param[in] - n - Ptr to the location at which to insert node
 * @param[in] - data - Data to be put in the new node
 * @param[in] - before - Insert before or after the node location of n
 *
 * @return	The ptr to the just inserted node
 * @retval	NULL - Failed to insert data (out of memory)
 * @retval	!NULL - Ptr to the newly created node
 *
 * @par Side Effects:
 *	None
 *
 * @par MT-safe: Yes
 *
 */
tpp_que_elem_t*
tpp_que_ins_elem(tpp_que_t *l, tpp_que_elem_t *n, void *data, int before)
{
	tpp_que_elem_t *nd = NULL;

	if (n) {
		if ((nd = malloc(sizeof(tpp_que_elem_t))) == NULL) {
			return NULL;
		}
		nd->queue_data = data;
		if (before == 0) {
			/* after */
			nd->next = n->next;
			nd->prev = n;
			if (n->next)
				n->next->prev = nd;
			n->next = nd;
			if (n == l->tail)
				l->tail = nd;

		} else {
			/* before */
			nd->prev = n->prev;
			nd->next = n;
			if (n->prev)
				n->prev->next = nd;
			n->prev = nd;
			if (n == l->head)
				l->head = nd;
		}
	}
	return nd;
}

/**
 * @brief
 *	Convenience function to set the control header and and send the control
 *	packet (TPP_CTL_NOROUTE) to the given destination by calling
 *	tpp_transport_vsend.
 *
 * @param[in] - fd - The physical connection via which to send control packet
 * @param[in] - src - The host:port of the source (sender)
 * @param[in] - dest - The host:port of the destination
 *
 * @return	Error code
 * @retval	-1    - Failure
 * @retval	 0    - Success
 *
 * @par Side Effects:
 *	None
 *
 * @par MT-safe: Yes
 *
 */
int
tpp_send_ctl_msg(int fd, int code, tpp_addr_t *src, tpp_addr_t *dest, unsigned int src_sd, char err_num, char *msg)
{
	tpp_ctl_pkt_hdr_t *lhdr = NULL;
	tpp_packet_t *pkt = NULL;

	/* send a packet back to where the original packet came from
	 * basically reverse src and dest
	 */
	pkt = tpp_bld_pkt(NULL,  NULL, sizeof(tpp_ctl_pkt_hdr_t), 1, (void **) &lhdr);
	if (!pkt) {
		tpp_log(LOG_CRIT, __func__, "Failed to build packet");
		return -1;
	}
	lhdr->type = TPP_CTL_MSG;
	lhdr->code = code;
	lhdr->src_sd = htonl(src_sd);
	lhdr->error_num = err_num;
	if (src)
		memcpy(&lhdr->dest_addr, src, sizeof(tpp_addr_t));
	if (dest)
		memcpy(&lhdr->src_addr, dest, sizeof(tpp_addr_t));
	if (msg == NULL)
		msg = "";

	if (!tpp_bld_pkt(pkt, msg, strlen(msg) + 1, 1, NULL)) {
		tpp_log(LOG_CRIT, __func__, "Failed to build packet");
		return -1;
	}

	TPP_DBPRT("Sending CTL PKT: sd=%d, msg=%s", src_sd, msg);
	if (tpp_transport_vsend(fd, pkt) != 0) {
		tpp_log(LOG_CRIT, __func__, "tpp_transport_vsend failed");
		return -1;
	}
	return 0;
}

/**
 * @brief
 *	Combine the host and port parameters to a single string.
 *
 * @param[in] - host - hostname
 * @param[in] - port - add port if not already present
 *
 * @return	The combined string with the host:port
 * @retval	NULL - Failure (out of memory)
 * @retval	!NULl - Combined string
 *
 * @par Side Effects:
 *	None
 *
 * @par MT-safe: Yes
 *
 */
char *
mk_hostname(char *host, int port)
{
	char *node_name = malloc(strlen(host) + 10);
	if (node_name) {
		if (strchr(host, ':') || port == -1)
			strcpy(node_name, host);
		else
			sprintf(node_name, "%s:%d", host, port);
	}
	return node_name;
}

/**
 * @brief
 *	Once function for initializing TLS key
 *
 *  @return    - Error code
 *	@retval -1 - Failure
 *	@retval  0 - Success
 *
 * @par Side Effects:
 *	Initializes the global tpp_key_tls, exits if fails
 *
 * @par MT-safe: No
 *
 */
static void
tpp_init_tls_key_once(void)
{
	if (pthread_key_create(&tpp_key_tls, NULL) != 0) {
		fprintf(stderr, "Failed to initialize TLS key\n");
	}
}

/**
 * @brief
 *	Initialize the TLS key
 *
 *  @return    - Error code
 *	@retval -1 - Failure
 *	@retval  0 - Success
 *
 * @par Side Effects:
 *	Initializes the global tpp_key_tls
 *
 * @par MT-safe: No
 *
 */
int
tpp_init_tls_key()
{
	if (pthread_once(&tpp_once_ctrl, tpp_init_tls_key_once) != 0)
		return -1;
	return 0;
}

/**
 * @brief
 *	Get the data from the thread TLS
 *
 * @return	Pointer of the tpp_thread_data structure from threads TLS
 * @retval	NULL - Pthread functions failed
 * @retval	!NULl - Data from TLS
 *
 * @par Side Effects:
 *	None
 *
 * @par MT-safe: Yes
 *
 */
tpp_tls_t *
tpp_get_tls()
{
	tpp_tls_t *ptr;
	if ((ptr = pthread_getspecific(tpp_key_tls)) == NULL) {
		ptr = calloc(1, sizeof(tpp_tls_t));
		if (!ptr)
			return NULL;

		if (pthread_setspecific(tpp_key_tls, ptr) != 0) {
			free(ptr);
			return NULL;
		}
	}
	return (tpp_tls_t *) ptr; /* thread data already initialized */
}

#ifdef PBS_COMPRESSION_ENABLED

#define COMPR_LEVEL Z_DEFAULT_COMPRESSION

struct def_ctx {
	z_stream cmpr_strm;
	void *cmpr_buf;
	int len;
};

/**
 * @brief
 *	Initialize a multi step deflation
 *	Allocate an initial result buffer of given length
 *
 * @param[in] initial_len -  initial length of result buffer
 *
 * @return - The deflate context
 * @retval - NULL  - Failure
 * @retval - !NULL - Success
 *
 * @par Side Effects:
 *	None
 *
 * @par MT-safe: No
 *
 */
void *
tpp_multi_deflate_init(int initial_len)
{
	int ret;
	struct def_ctx *ctx = malloc(sizeof(struct def_ctx));
	if (!ctx) {
		tpp_log(LOG_CRIT, __func__, "Out of memory allocating context buffer %lu bytes", sizeof(struct def_ctx));
		return NULL;
	}

	if ((ctx->cmpr_buf = malloc(initial_len)) == NULL) {
		free(ctx);
		tpp_log(LOG_CRIT, __func__, "Out of memory allocating deflate buffer %d bytes", initial_len);
		return NULL;
	}

	/* allocate deflate state */
	ctx->cmpr_strm.zalloc = Z_NULL;
	ctx->cmpr_strm.zfree = Z_NULL;
	ctx->cmpr_strm.opaque = Z_NULL;
	ret = deflateInit(&ctx->cmpr_strm, COMPR_LEVEL);
	if (ret != Z_OK) {
		free(ctx->cmpr_buf);
		free(ctx);
		tpp_log(LOG_CRIT, __func__, "Multi compression init failed");
		return NULL;
	}

	ctx->len = initial_len;
	ctx->cmpr_strm.avail_out = initial_len;
	ctx->cmpr_strm.next_out = ctx->cmpr_buf;
	return (void *) ctx;
}

/**
 * @brief
 *	Add data to a multi step deflation
 *
 * @param[in] c - The deflate context
 * @param[in] fini - Whether this call is the final data addition
 * @param[in] inbuf - Pointer to data buffer to add
 * @param[in] inlen - Length of input buffer to add
 *
 * @return - Error code
 * @retval - -1  - Failure
 * @retval -  0  - Success
 *
 * @par Side Effects:
 *	None
 *
 * @par MT-safe: No
 *
 */
int
tpp_multi_deflate_do(void *c, int fini, void *inbuf, unsigned int inlen)
{
	struct def_ctx *ctx = c;
	int flush;
	int ret;
	int filled;
	void *p;

	ctx->cmpr_strm.avail_in = inlen;
	ctx->cmpr_strm.next_in = inbuf;

	flush = (fini == 1) ? Z_FINISH : Z_NO_FLUSH;
	while (1) {
		ret = deflate(&ctx->cmpr_strm, flush);
		if (ret == Z_OK && ctx->cmpr_strm.avail_out == 0) {
			/* more output pending, but no output buffer space */
			filled = (char *) ctx->cmpr_strm.next_out - (char *) ctx->cmpr_buf;
			ctx->len = ctx->len * 2;
			p = realloc(ctx->cmpr_buf, ctx->len);
			if (!p) {
				tpp_log(LOG_CRIT, __func__, "Out of memory allocating deflate buffer %d bytes", ctx->len);
				deflateEnd(&ctx->cmpr_strm);
				free(ctx->cmpr_buf);
				free(ctx);
				return -1;
			}
			ctx->cmpr_buf = p;
			ctx->cmpr_strm.next_out = (Bytef *)((char *) ctx->cmpr_buf + filled);
			ctx->cmpr_strm.avail_out = ctx->len - filled;
		} else
			break;
	}
	if (fini == 1 && ret != Z_STREAM_END) {
		deflateEnd(&ctx->cmpr_strm);
		free(ctx->cmpr_buf);
		free(ctx);
		tpp_log(LOG_CRIT, __func__, "Multi compression step failed");
		return -1;
	}
	return 0;
}

/**
 * @brief
 *	Complete the deflate and
 *
 * @param[in] c - The deflate context
 * @param[out] cmpr_len - The total length after compression
 *
 * @return - compressed buffer
 * @retval - NULL  - Failure
 * @retval - !NULL - Success
 *
 * @par Side Effects:
 *	None
 *
 * @par MT-safe: No
 *
 */
void *
tpp_multi_deflate_done(void *c, unsigned int *cmpr_len)
{
	struct def_ctx *ctx = c;
	void *data = ctx->cmpr_buf;
	int ret;

	*cmpr_len = ctx->cmpr_strm.total_out;

	ret = deflateEnd(&ctx->cmpr_strm);
	free(ctx);
	if (ret != Z_OK) {
		free(data);
		tpp_log(LOG_CRIT, __func__, "Compression cleanup failed");
		return NULL;
	}
	return data;
}

/**
 * @brief Deflate (compress) data
 *
 * @param[in] inbuf   - Ptr to buffer to compress
 * @param[in] inlen   - The size of input buffer
 * @param[out] outlen - The size of the compressed data
 *
 * @return      - Ptr to the compressed data buffer
 * @retval  !NULL - Success
 * @retval   NULL - Failure
 *
 * @par MT-safe: No
 **/
void *
tpp_deflate(void *inbuf, unsigned int inlen, unsigned int *outlen)
{
	z_stream strm;
	int ret;
	void *data;
	unsigned int filled;
	void *p;
	int len;

	*outlen = 0;

	/* allocate deflate state */
	strm.zalloc = Z_NULL;
	strm.zfree = Z_NULL;
	strm.opaque = Z_NULL;
	ret = deflateInit(&strm, Z_DEFAULT_COMPRESSION);
	if (ret != Z_OK) {
		tpp_log(LOG_CRIT, __func__, "Compression failed");
		return NULL;
	}

	/* set input data to be compressed */
	len = inlen;
	strm.avail_in = len;
	strm.next_in = inbuf;

	/* allocate buffer to temporarily collect compressed data */
	data = malloc(len);
	if (!data) {
		deflateEnd(&strm);
		tpp_log(LOG_CRIT, __func__, "Out of memory allocating deflate buffer %d bytes", len);
		return NULL;
	}

	/* run deflate() on input until output buffer not full, finish
	 * compression if all of source has been read in
	 */

	strm.avail_out = len;
	strm.next_out = data;
	while (1) {
		ret = deflate(&strm, Z_FINISH);
		if (ret == Z_OK && strm.avail_out == 0) {
			/* more output pending, but no output buffer space */
			filled = (char *) strm.next_out - (char *) data;
			len = len * 2;
			p = realloc(data, len);
			if (!p) {
				deflateEnd(&strm);
				free(data);
				tpp_log(LOG_CRIT, __func__, "Out of memory allocating deflate buffer %d bytes", len);
				return NULL;
			}
			data = p;
			strm.next_out = (Bytef *)((char *) data + filled);
			strm.avail_out = len - filled;
		} else
			break;
	}
	deflateEnd(&strm); /* clean up */
	if (ret != Z_STREAM_END) {
		free(data);
		tpp_log(LOG_CRIT, __func__, "Compression failed");
		return NULL;
	}
	filled = (char *) strm.next_out - (char *) data;

	/* reduce the memory area occupied */
	if (filled != inlen) {
		p = realloc(data, filled);
		if (!p) {
			free(data);
			tpp_log(LOG_CRIT, __func__, "Out of memory allocating deflate buffer %d bytes", filled);
			return NULL;
		}
		data = p;
	}

	*outlen = filled;
	return data;
}

/**
 * @brief Inflate (de-compress) data
 *
 * @param[in] inbuf  - Ptr to compress data buffer
 * @param[in] inlen  - The size of input buffer
 * @param[in] totlen - The total size of the uncompress data
 *
 * @return      - Ptr to the uncompressed data buffer
 * @retval  !NULL - Success
 * @retval   NULL - Failure
 *
 * @par MT-safe: No
 **/
void *
tpp_inflate(void *inbuf, unsigned int inlen, unsigned int totlen)
{
	int ret;
	z_stream strm;
	void *outbuf = NULL;

	/*
	 * in some rare cases totlen < compressed_len (inlen)
	 * so safer to malloc the larger of the two values
	 */
	outbuf = malloc(totlen > inlen ? totlen:inlen);
	if (!outbuf) {
		tpp_log(LOG_CRIT, __func__, "Out of memory allocating inflate buffer %d bytes", totlen);
		return NULL;
	}

	/* allocate inflate state */
	strm.zalloc = Z_NULL;
	strm.zfree = Z_NULL;
	strm.opaque = Z_NULL;
	strm.avail_in = 0;
	strm.next_in = Z_NULL;
	ret = inflateInit(&strm);
	if (ret != Z_OK) {
		free(outbuf);
		tpp_log(LOG_CRIT, __func__, "Decompression Init (inflateInit) failed, ret = %d", ret);
		return NULL;
	}

	/* decompress until deflate stream ends or end of file */
	strm.avail_in = inlen;
	strm.next_in = inbuf;

	/* run inflate() on input until output buffer not full */
	strm.avail_out = totlen;
	strm.next_out = outbuf;
	ret = inflate(&strm, Z_FINISH);
	inflateEnd(&strm);
	if (ret != Z_STREAM_END) {
		free(outbuf);
		tpp_log(LOG_CRIT, __func__, "Decompression (inflate) failed, ret = %d", ret);
		return NULL;
	}
	return outbuf;
}
#else
void *
tpp_multi_deflate_init(int initial_len)
{
	tpp_log(LOG_CRIT, __func__, "TPP compression disabled");
	return NULL;
}

int
tpp_multi_deflate_do(void *c, int fini, void *inbuf, unsigned int inlen)
{
	tpp_log(LOG_CRIT, __func__, "TPP compression disabled");
	return -1;
}

void *
tpp_multi_deflate_done(void *c, unsigned int *cmpr_len)
{
	tpp_log(LOG_CRIT, __func__, "TPP compression disabled");
	return NULL;
}

void *
tpp_deflate(void *inbuf, unsigned int inlen, unsigned int *outlen)
{
	tpp_log(LOG_CRIT, __func__, "TPP compression disabled");
	return NULL;
}

void *
tpp_inflate(void *inbuf, unsigned int inlen, unsigned int totlen)
{
	tpp_log(LOG_CRIT, __func__, "TPP compression disabled");
	return NULL;
}
#endif

/**
 * @brief Convenience function to validate a tpp header
 *
 * @param[in] tfd       - The transport fd
 * @param[in] pkt_start - The start address of the pkt
 *
 * @return - Packet validity status
 * @retval  0 - Packet has valid header structure
 * @retval -1 - Packet has invalid header structure
 *
 * @par MT-safe: No
 *
 **/
int
tpp_validate_hdr(int tfd, char *pkt_start)
{
	enum TPP_MSG_TYPES type;
	char *data;
	int data_len;

	data_len = ntohl(*((int *) pkt_start));
	data = pkt_start + sizeof(int);
	type = *((unsigned char *) data);

	if ((data_len < 0 || type >= TPP_LAST_MSG) ||
		(data_len > TPP_SEND_SIZE &&
			type != TPP_DATA &&
			type != TPP_MCAST_DATA &&
			type != TPP_ENCRYPTED_DATA &&
			type != TPP_AUTH_CTX)) {
		tpp_log(LOG_CRIT, __func__, "tfd=%d, Received invalid packet type with type=%d? data_len=%d", tfd, type, data_len);
		return -1;
	}
	return 0;
}

/**
 * @brief Get a list of addresses for a given hostname
 *
 * @param[in] node_names - comma separated hostnames, each of format host:port
 * @param[out] count - return address count
 *
 * @return        - Array of addresses in tpp_addr structures
 * @retval  !NULL - Array of addresses returned
 * @retval  NULL  - call failed
 *
 * @par MT-safe: Yes
 *
 **/
tpp_addr_t *
tpp_get_addresses(char *names, int *count)
{
	tpp_addr_t *addrs = NULL;
	tpp_addr_t *addrs_tmp = NULL;
	int tot_count = 0;
	int tmp_count;
	int i, j;
	char *token;
	char *saveptr;
	int port;
	char *p;
	char *node_names;

	*count = 0;
	if ((node_names = strdup(names)) == NULL) {
		tpp_log(LOG_CRIT, __func__, "Out of memory allocating address block");
		return NULL;
	}

	token = strtok_r(node_names, ",", &saveptr);
	while (token) {
		/* parse port from host name */
		if ((p = strchr(token, ':')) == NULL) {
			free(addrs);
			free(node_names);
			return NULL;
		}

		*p = '\0';
		port = atol(p+1);

		addrs_tmp = tpp_sock_resolve_host(token, &tmp_count); /* get all ipv4 addresses */
		if (addrs_tmp) {
			tpp_addr_t *tmp;
			if ((tmp = realloc(addrs, (tot_count + tmp_count) * sizeof(tpp_addr_t))) == NULL) {
				free(addrs);
				free(node_names);
				tpp_log(LOG_CRIT, __func__, "Out of memory allocating address block");
				return NULL;
			}
			addrs = tmp;

			for (i = 0; i < tmp_count; i++) {
				for (j = 0; j < tot_count; j++) {
					if (memcmp(&addrs[j].ip, &addrs_tmp[i].ip, sizeof(addrs_tmp[i].ip)) == 0)
						break;
				}

				/* add if duplicate not found already */
				if (j == tot_count) {
					memmove(&addrs[tot_count], &addrs_tmp[i], sizeof(tpp_addr_t));
					addrs[tot_count].port = htons(port);
					tot_count++;
				}
			}
			free(addrs_tmp);
		}

		token = strtok_r(NULL, ",", &saveptr);
	}
	free(node_names);

	*count = tot_count;
	return addrs; /* free @ caller */
}

/**
 * @brief Get the address of the local end of the connection
 *
 * @param[in] sock - connection id
 *
 * @return - Address of the local end of the connection in tpp_addr format
 * @retval  !NULL - address returned
 * @retval  NULL  - call failed
 *
 * @par MT-safe: Yes
 **/
tpp_addr_t *
tpp_get_local_host(int sock)
{
	struct sockaddr_storage addrs;
	struct sockaddr *addr = (struct sockaddr *)&addrs;
	struct sockaddr_in *inp = NULL;
	struct sockaddr_in6 *inp6 = NULL;
	tpp_addr_t *taddr = NULL;
	socklen_t len = sizeof(struct sockaddr);

	if (getsockname(sock, addr, &len) == -1) {
		tpp_log(LOG_CRIT, __func__, "Could not get name of peer for sock %d, errno=%d", sock, errno);
		return NULL;
	}
	if (addr->sa_family != AF_INET && addr->sa_family != AF_INET6) {
		tpp_log(LOG_CRIT, __func__, "Bad address family for sock %d", sock);
		return NULL;
	}

	taddr = calloc(1, sizeof(tpp_addr_t));
	if (!taddr) {
		tpp_log(LOG_CRIT, __func__, "Out of memory allocating address");
		return NULL;
	}

	if (addr->sa_family == AF_INET) {
		inp = (struct sockaddr_in *) addr;
		memcpy(&taddr->ip, &inp->sin_addr, sizeof(inp->sin_addr));
		taddr->port = inp->sin_port; /* keep in network order */
		taddr->family = TPP_ADDR_FAMILY_IPV4;
	} else if (addr->sa_family == AF_INET6){
		inp6 = (struct sockaddr_in6 *) addr;
		memcpy(&taddr->ip, &inp6->sin6_addr, sizeof(inp6->sin6_addr));
		taddr->port = inp6->sin6_port; /* keep in network order */
		taddr->family = TPP_ADDR_FAMILY_IPV6;
	}

	return taddr;
}

/**
 * @brief Get the address of the remote (peer) end of the connection
 *
 * @param[in] sock - connection id
 *
 * @return  - Address of the remote end of the connection in tpp_addr format
 * @retval  !NULL - address returned
 * @retval  NULL  - call failed
 *
 * @par MT-safe: Yes
 **/
tpp_addr_t *
tpp_get_connected_host(int sock)
{
	struct sockaddr_storage addrs;
	struct sockaddr *addr = (struct sockaddr *)&addrs;
	struct sockaddr_in *inp = NULL;
	struct sockaddr_in6 *inp6 = NULL;
	tpp_addr_t *taddr = NULL;
	socklen_t len = sizeof(struct sockaddr);

	if (getpeername(sock, addr, &len) == -1) {
		if (errno == ENOTCONN)
			tpp_log(LOG_CRIT, __func__, "Peer disconnected sock %d", sock);
		else
			tpp_log(LOG_CRIT, __func__, "Could not get name of peer for sock %d, errno=%d", sock, errno);

		return NULL;
	}
	if (addr->sa_family != AF_INET && addr->sa_family != AF_INET6) {
		tpp_log(LOG_CRIT, __func__, "Bad address family for sock %d", sock);
		return NULL;
	}

	taddr = calloc(1, sizeof(tpp_addr_t));
	if (!taddr) {
		tpp_log(LOG_CRIT, __func__, "Out of memory allocating address");
		return NULL;
	}

	if (addr->sa_family == AF_INET) {
		inp = (struct sockaddr_in *) addr;
		memcpy(&taddr->ip, &inp->sin_addr, sizeof(inp->sin_addr));
		taddr->port = inp->sin_port; /* keep in network order */
		taddr->family = TPP_ADDR_FAMILY_IPV4;
	} else if (addr->sa_family == AF_INET6){
		inp6 = (struct sockaddr_in6 *) addr;
		memcpy(&taddr->ip, &inp6->sin6_addr, sizeof(inp6->sin6_addr));
		taddr->port = inp6->sin6_port; /* keep in network order */
		taddr->family = TPP_ADDR_FAMILY_IPV6;
	}

	return taddr;
}

/**
 * @brief return a human readable string representation of an address
 *        for either an ipv4 or ipv6 address
 *
 * @param[in] ap - address in tpp_addr format
 *
 * @return  - string representation of address
 *            (uses TLS area to make it easy and yet thread safe)
 *
 * @par MT-safe: Yes
 **/
char *
tpp_netaddr(tpp_addr_t *ap)
{
	tpp_tls_t *ptr;
#ifdef WIN32
	struct sockaddr_in in;
	struct sockaddr_in6 in6;
	int len;
#endif
	char port[7];

	if (ap == NULL)
		return "unknown";

	ptr = tpp_get_tls();
	if (!ptr) {
		fprintf(stderr, "Out of memory\n");
		return "unknown";
	}

	ptr->tppstaticbuf[0] = '\0';

	if (ap->family == TPP_ADDR_FAMILY_UNSPEC)
		return "unknown";

#ifdef WIN32
	if (ap->family == TPP_ADDR_FAMILY_IPV4) {
		memcpy(&in.sin_addr, ap->ip, sizeof(in.sin_addr));
		in.sin_family = AF_INET;
		in.sin_port = 0;
		len = LOG_BUF_SIZE;
		WSAAddressToString((LPSOCKADDR) &in, sizeof(in), NULL, (LPSTR) &ptr->tppstaticbuf, &len);
	} else if (ap->family == TPP_ADDR_FAMILY_IPV6) {
		memcpy(&in6.sin6_addr, ap->ip, sizeof(in6.sin6_addr));
		in6.sin6_family = AF_INET6;
		in6.sin6_port = 0;
		len = LOG_BUF_SIZE;
		WSAAddressToString((LPSOCKADDR) &in6, sizeof(in6), NULL, (LPSTR) &ptr->tppstaticbuf, &len);
	}
#else
	if (ap->family == TPP_ADDR_FAMILY_IPV4) {
		inet_ntop(AF_INET, &ap->ip, ptr->tppstaticbuf, INET_ADDRSTRLEN);
	}  else if (ap->family == TPP_ADDR_FAMILY_IPV6) {
		inet_ntop(AF_INET6, &ap->ip, ptr->tppstaticbuf, INET6_ADDRSTRLEN);
	}
#endif
	sprintf(port, ":%d", ntohs(ap->port));
	strcat(ptr->tppstaticbuf, port);

	return ptr->tppstaticbuf;
}

/**
 * @brief return a human readable string representation of an address
 *        for either an ipv4 or ipv6 address
 *
 * @param[in] sa - address in sockaddr format
 *
 * @return  - string representation of address
 *            (uses TLS area to make it easy and yet thread safe)
 *
 * @par MT-safe: Yes
 **/
char *
tpp_netaddr_sa(struct sockaddr *sa)
{
#ifdef WIN32
	int len;
#endif
	tpp_tls_t *ptr = tpp_get_tls();
	if (!ptr) {
		fprintf(stderr, "Out of memory\n");
		return NULL;
	}
	ptr->tppstaticbuf[0] = '\0';

#ifdef WIN32
	len = sizeof(ptr->tppstaticbuf);
	WSAAddressToString((LPSOCKADDR)&sa, sizeof(struct sockaddr), NULL, (LPSTR)&ptr->tppstaticbuf, &len);
#else
	if (sa->sa_family == AF_INET)
		inet_ntop(sa->sa_family, &(((struct sockaddr_in *) sa)->sin_addr), ptr->tppstaticbuf, sizeof(ptr->tppstaticbuf));
	else
		inet_ntop(sa->sa_family, &(((struct sockaddr_in6 *) sa)->sin6_addr), ptr->tppstaticbuf, sizeof(ptr->tppstaticbuf));
#endif

	return ptr->tppstaticbuf;
}

/*
 * Convenience function to delete information about a router
 */
void
free_router(tpp_router_t *r)
{
	if (r) {
		if (r->router_name)
			free(r->router_name);
		free(r);
	}
}

/*
 * Convenience function to delete information about a leaf
 */
void
free_leaf(tpp_leaf_t *l)
{
	if (l) {
		if (l->leaf_addrs)
			free(l->leaf_addrs);

		free(l);
	}
}

/**
 * @brief Set the loglevel for the tpp layer. This is used to print
 * additional information at times (like ip addresses are revers
 * looked-up and hostnames printed in logs).
 *
 * @param[in] logmask - The logmask value to set
 *
 * @par MT-safe: No
 **/
void
tpp_set_logmask(long logmask)
{
	tpp_log_event_mask = logmask;
}


#ifndef WIN32

/**
 * @brief
 *	wrapper function for tpp_nslookup_mutex_lock().
 *
 */
void
tpp_nslookup_atfork_prepare()
{
	tpp_lock(&tpp_nslookup_mutex);
}

/**
 * @brief
 *	wrapper function for tpp_nslookup_mutex_unlock().
 *
 */
void
tpp_nslookup_atfork_parent()
{
	tpp_unlock(&tpp_nslookup_mutex);
}

/**
 * @brief
 *	wrapper function for tpp_nslookup_mutex_unlock().
 *
 */
void
tpp_nslookup_atfork_child()
{
	tpp_unlock(&tpp_nslookup_mutex);
}
#endif

/**
 * @brief encrypt the pkt  with the authdata provided
 *
 * @param[in] authdata - encryption information
 * @param[in] pkt - packet of data
 *
 * @par MT-safe: No
 **/
int
tpp_encrypt_pkt(conn_auth_t *authdata, tpp_packet_t *pkt)
{
	void *data_out = NULL;
	size_t len_out = 0;
	tpp_encrypt_hdr_t *ehdr;
	int totlen = pkt->totlen;
	tpp_chunk_t *chunk, *next;
	tpp_auth_pkt_hdr_t *data = (tpp_auth_pkt_hdr_t *)(((tpp_chunk_t *)(GET_NEXT(pkt->chunks)))->data);
	unsigned char type = data->type;
	void *buf = NULL;
	char *p;

	if (type == TPP_AUTH_CTX && data->for_encrypt == FOR_ENCRYPT)
		return 0;

	buf = malloc(totlen);
	if (buf == NULL) {
		tpp_log(LOG_CRIT, __func__, "Failed to allocated buffer for encrypting pkt data");
		return -1;
	}
	p = (char *)buf;
	chunk = GET_NEXT(pkt->chunks);
	while (chunk) {
		memcpy(p, chunk->data, chunk->len);
		p += chunk->len;
		next = GET_NEXT(chunk->chunk_link);
		tpp_free_chunk(chunk);
		chunk = next;
	}
	pkt->totlen = 0;
	CLEAR_HEAD(pkt->chunks);
	pkt->curr_chunk = NULL;

	if (authdata->encryptdef->encrypt_data(authdata->encryptctx, buf, totlen, &data_out, &len_out) != 0) {
		tpp_log(LOG_CRIT, __func__, "Failed to encrypt pkt data");
		free(buf);
		return -1;
	}

	if (totlen > 0 && len_out <= 0) {
		tpp_log(LOG_CRIT, __func__, "invalid encrypted data len: %d, pktlen: %d", (int) len_out, totlen);
		free(buf);
		return -1;
	}
	free(buf);
	if (!tpp_bld_pkt(pkt, NULL, sizeof(tpp_encrypt_hdr_t), 1, (void **)&ehdr)) {
		tpp_log(LOG_CRIT, __func__, "Failed to add encrypt pkt header into pkt");
		free(data_out);
		return -1;
	}
	if (!tpp_bld_pkt(pkt, data_out, len_out, 0, NULL)) {
		tpp_log(LOG_CRIT, __func__, "Failed to add encrypted data into pkt");
		free(data_out);
		return -1;
	}
	ehdr->ntotlen = htonl(pkt->totlen);
	ehdr->type = TPP_ENCRYPTED_DATA;
	pkt->curr_chunk = GET_NEXT(pkt->chunks);

	return 0;
}

/*
 * use TPPDEBUG instead of DEBUG, since DEBUG makes daemons not fork
 * and that does not work well with init scripts. Sometimes we need to
 * debug TPP in a PTL run where forked daemons are required
 * Hence use a separate macro
 */
#ifdef TPPDEBUG
/*
 * Convenience function to print the packet header
 *
 * @param[in] fnc - name of calling function
 * @param[in] data - start of data packet
 * @param[in] len - length of data packet
 *
 * @par MT-safe: yes
 */
void
print_packet_hdr(const char *fnc, void *data, int len)
{
	tpp_ctl_pkt_hdr_t *hdr = (tpp_ctl_pkt_hdr_t *) data;

	char str_types[][20] = { "TPP_CTL_JOIN", "TPP_CTL_LEAVE", "TPP_DATA", "TPP_CTL_MSG", "TPP_CLOSE_STRM", "TPP_MCAST_DATA" };
	unsigned char type = hdr->type;

	if (type == TPP_CTL_JOIN) {
		tpp_addr_t *addrs = (tpp_addr_t *) (((char *) data) + sizeof(tpp_join_pkt_hdr_t));
		tpp_log(LOG_CRIT, __func__, "%s message arrived from src_host = %s", str_types[type - 1], tpp_netaddr(addrs));
	} else if (type == TPP_CTL_LEAVE) {
		tpp_addr_t *addrs = (tpp_addr_t *) (((char *) data) + sizeof(tpp_leave_pkt_hdr_t));
		tpp_log(LOG_CRIT, __func__, "%s message arrived from src_host = %s", str_types[type - 1], tpp_netaddr(addrs));
	} else if (type == TPP_MCAST_DATA) {
		tpp_mcast_pkt_hdr_t *mhdr = (tpp_mcast_pkt_hdr_t *) data;
		tpp_log(LOG_CRIT, __func__,  "%s message arrived from src_host = %s", str_types[type - 1], tpp_netaddr(&mhdr->src_addr));
	} else if ((type == TPP_DATA) || (type == TPP_CLOSE_STRM)) {
		char buff[TPP_GEN_BUF_SZ+1];
		tpp_data_pkt_hdr_t *dhdr = (tpp_data_pkt_hdr_t *) data;

		strncpy(buff, tpp_netaddr(&dhdr->src_addr), sizeof(buff));
		tpp_log(LOG_CRIT, __func__, "%s: src_host=%s, dest_host=%s, len=%d, data_len=%d, src_sd=%d, dest_sd=%d, src_magic=%d", 
			str_types[type - 1], buff, tpp_netaddr(&dhdr->dest_addr), len + sizeof(tpp_data_pkt_hdr_t), len,
			ntohl(dhdr->src_sd), (ntohl(dhdr->dest_sd) == UNINITIALIZED_INT) ? -1 : ntohl(dhdr->dest_sd), ntohl(dhdr->src_magic));

	} else {
		tpp_log(LOG_CRIT, __func__, "%s message arrived from src_host = %s", str_types[type - 1], tpp_netaddr(&hdr->src_addr));
	}
}
#endif
