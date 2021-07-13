
#
# Copyright (C) 1994-2021 Altair Engineering, Inc.
# For more information, contact Altair at www.altair.com.
#
# This file is part of both the OpenPBS software ("OpenPBS")
# and the PBS Professional ("PBS Pro") software.
#
# Open Source License Information:
#
# OpenPBS is free software. You can redistribute it and/or modify it under
# the terms of the GNU Affero General Public License as published by the
# Free Software Foundation, either version 3 of the License, or (at your
# option) any later version.
#
# OpenPBS is distributed in the hope that it will be useful, but WITHOUT
# ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
# FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Affero General Public
# License for more details.
#
# You should have received a copy of the GNU Affero General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.
#
# Commercial License Information:
#
# PBS Pro is commercially licensed software that shares a common core with
# the OpenPBS software.  For a copy of the commercial license terms and
# conditions, go to: (http://www.pbspro.com/agreement.html) or contact the
# Altair Legal Department.
#
# Altair's dual-license business model allows companies, individuals, and
# organizations to create proprietary derivative works of OpenPBS and
# distribute them - whether embedded or bundled with other software -
# under a commercial license agreement.
#
# Use of Altair's trademarks, including but not limited to "PBS™",
# "OpenPBS®", "PBS Professional®", and "PBS Pro™" and Altair's logos is
# subject to Altair's trademark licensing policies.

#

include_HEADERS = \
	pbs_error.h \
	pbs_ifl.h \
	rm.h \
	tm_.h \
	tm.h

noinst_HEADERS = \
	acct.h \
	libauth.h \
	auth.h \
	attribute.h \
	avltree.h \
	basil.h \
	batch_request.h \
	bitfield.h \
	cmds.h \
	credential.h \
	dis.h \
	grunt.h \
	hook_func.h \
	hook.h \
	ifl_internal.h \
	job.h \
	libpbs.h \
	libsec.h \
	libutil.h \
	list_link.h \
	log.h \
	Long_.h \
	Long.h \
	Makefile.in \
	mom_func.h \
	mom_hook_func.h \
	mom_server.h \
	mom_vnode.h \
	net_connect.h \
	pbs_array_list.h \
	pbs_assert.h \
	pbs_client_thread.h \
	pbs_db.h \
	pbs_ecl.h \
	pbs_entlim.h \
	pbs_idx.h \
	pbs_internal.h \
	pbs_reliable.h \
	pbs_json.h \
	pbs_license.h \
	pbs_mpp.h \
	pbs_nodes.h \
	pbs_python.h \
	pbs_python_private.h \
	pbs_share.h \
	pbs_undolr.h \
	pbs_v1_module_common.i \
	pbs_version.h \
	placementsets.h \
	portability.h \
	port_forwarding.h \
	provision.h \
	qmgr.h \
	queue.h \
	range.h \
	reservation.h \
	resmon.h \
	resource.h \
	resv_node.h \
	tpp.h \
	sched_cmds.h \
	pbs_sched.h \
	server.h \
	server_limits.h \
	site_queue.h \
	site_job_attr_def.h \
	site_job_attr_enum.h \
	site_qmgr_node_print.h \
	site_qmgr_que_print.h \
	site_qmgr_sched_print.h \
	site_qmgr_svr_print.h \
	site_que_attr_def.h \
	site_que_attr_enum.h \
	site_resc_attr_def.h \
	site_resv_attr_def.h \
	site_resv_attr_enum.h \
	site_sched_attr_def.h \
	site_sched_attr_enum.h \
	site_svr_attr_def.h \
	site_svr_attr_enum.h \
	job_attr_enum.h \
	node_attr_enum.h \
	queue_attr_enum.h \
	resc_def_enum.h \
	resv_attr_enum.h \
	sched_attr_enum.h \
	svr_attr_enum.h \
	svrfunc.h \
	ticket.h \
	tracking.h \
	user.h \
	work_task.h


CLEANFILES = \
	job_attr_enum.h \
	node_attr_enum.h \
	queue_attr_enum.h \
	resc_def_enum.h \
	resv_attr_enum.h \
	sched_attr_enum.h \
	svr_attr_enum.h

job_attr_enum.h: $(top_srcdir)/src/lib/Libattr/master_job_attr_def.xml $(top_srcdir)/buildutils/attr_parser.py
	@echo Generating $@ from $< ; \
	@PYTHON@ $(top_srcdir)/buildutils/attr_parser.py -m $(top_srcdir)/src/lib/Libattr/master_job_attr_def.xml -d $@

node_attr_enum.h: $(top_srcdir)/src/lib/Libattr/master_node_attr_def.xml $(top_srcdir)/buildutils/attr_parser.py
	@echo Generating $@ from $< ; \
	@PYTHON@ $(top_srcdir)/buildutils/attr_parser.py -m $(top_srcdir)/src/lib/Libattr/master_node_attr_def.xml -d $@

queue_attr_enum.h: $(top_srcdir)/src/lib/Libattr/master_queue_attr_def.xml $(top_srcdir)/buildutils/attr_parser.py
	@echo Generating $@ from $< ; \
	@PYTHON@ $(top_srcdir)/buildutils/attr_parser.py -m $(top_srcdir)/src/lib/Libattr/master_queue_attr_def.xml -d $@

resc_def_enum.h: $(top_srcdir)/src/lib/Libattr/master_resc_def_all.xml $(top_srcdir)/buildutils/attr_parser.py
	@echo Generating $@ from $< ; \
	@PYTHON@ $(top_srcdir)/buildutils/attr_parser.py -m $(top_srcdir)/src/lib/Libattr/master_resc_def_all.xml -d $@

resv_attr_enum.h: $(top_srcdir)/src/lib/Libattr/master_resv_attr_def.xml $(top_srcdir)/buildutils/attr_parser.py
	@echo Generating $@ from $< ; \
	@PYTHON@ $(top_srcdir)/buildutils/attr_parser.py -m $(top_srcdir)/src/lib/Libattr/master_resv_attr_def.xml -d $@

sched_attr_enum.h: $(top_srcdir)/src/lib/Libattr/master_sched_attr_def.xml $(top_srcdir)/buildutils/attr_parser.py
	@echo Generating $@ from $< ; \
	@PYTHON@ $(top_srcdir)/buildutils/attr_parser.py -m $(top_srcdir)/src/lib/Libattr/master_sched_attr_def.xml -d $@

svr_attr_enum.h: $(top_srcdir)/src/lib/Libattr/master_svr_attr_def.xml $(top_srcdir)/buildutils/attr_parser.py
	@echo Generating $@ from $< ; \
	@PYTHON@ $(top_srcdir)/buildutils/attr_parser.py -m $(top_srcdir)/src/lib/Libattr/master_svr_attr_def.xml -d $@
