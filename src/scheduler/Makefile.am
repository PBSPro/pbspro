
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

lib_LIBRARIES = libpbs_sched.a
noinst_LIBRARIES = libschedharness.a

common_cflags = \
	-I$(top_srcdir)/src/include \
	@libz_inc@ \
	-pthread \
	@PYTHON_INCLUDES@ \
	@KRB5_CFLAGS@

common_libs = \
	$(builddir)/libpbs_sched.a \
	$(builddir)/libschedharness.a \
	$(top_builddir)/src/lib/Libutil/libutil.a \
	$(top_builddir)/src/lib/Liblog/liblog.a \
	$(top_builddir)/src/lib/Libnet/libnet.a \
	$(top_builddir)/src/lib/Libsec/libsec.a \
	$(top_builddir)/src/lib/Libpbs/.libs/libpbs.a \
	@PYTHON_LDFLAGS@ \
	@PYTHON_LIBS@ \
	@libz_lib@ \
	@libical_lib@ \
	@KRB5_LIBS@ \
	@libundolr_lib@

libpbs_sched_a_CPPFLAGS = ${common_cflags}

libschedharness_a_CPPFLAGS = ${common_cflags}
libschedharness_a_SOURCES = \
	pbs_sched_utils.cpp

libpbs_sched_a_SOURCES = \
	$(top_builddir)/src/lib/Libpython/shared_python_utils.c \
	buckets.cpp \
	buckets.h \
	check.cpp \
	check.h \
	config.h \
	constant.h \
	data_types.h \
	dedtime.cpp \
	dedtime.h \
	fairshare.cpp \
	fairshare.h \
	fifo.cpp \
	fifo.h \
	get_4byte.cpp \
	globals.cpp \
	globals.h \
	job_info.cpp \
	job_info.h \
	limits.cpp \
	limits_if.h \
	misc.cpp \
	misc.h \
	multi_threading.cpp \
	multi_threading.h \
	node_info.cpp \
	node_info.h \
	node_partition.cpp \
	node_partition.h \
	parse.cpp \
	parse.h \
	pbs_bitmap.cpp \
	pbs_bitmap.h \
	prev_job_info.cpp \
	prev_job_info.h \
	prime.cpp \
	prime.h \
	queue.cpp \
	queue.h \
	queue_info.cpp \
	queue_info.h \
	resource.cpp \
	resource.h \
	resource_resv.cpp \
	resource_resv.h \
	resv_info.cpp \
	resv_info.h \
	sched_exception.cpp \
	sched_ifl_wrappers.cpp \
	server_info.cpp \
	server_info.h \
	simulate.cpp \
	simulate.h \
	sort.cpp \
	sort.h \
	state_count.cpp \
	state_count.h \
	site_code.cpp \
	site_code.h \
	site_data.h

sbin_PROGRAMS = pbs_sched pbsfs
noinst_PROGRAMS = pbs_sched_bare

pbs_sched_CPPFLAGS = ${common_cflags}
pbs_sched_LDADD = ${common_libs}
pbs_sched_SOURCES = pbs_sched.cpp

pbs_sched_bare_CPPFLAGS = ${common_cflags}
pbs_sched_bare_LDADD = ${common_libs}
pbs_sched_bare_SOURCES = pbs_sched_bare.cpp

pbsfs_CPPFLAGS = ${common_cflags}
pbsfs_LDADD = ${common_libs}
pbsfs_SOURCES = pbsfs.cpp

dist_sysconf_DATA = \
	pbs_dedicated \
	pbs_holidays \
	pbs_holidays.2017 \
	pbs_resource_group \
	pbs_sched_config
