#!/bin/bash -xe

# Copyright (C) 1994-2020 Altair Engineering, Inc.
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

if [ "x${BUILD_MODE}" == "xsanitize" ]; then
   exit 0
fi
. /etc/os-release
if [ "x${ID}" == "xcentos" ]; then
    yum -y install python3
elif [ "x${ID}" == "xopensuse" -o "x${ID}" == "xopensuse-leap" ]; then
    zypper -n install python3
elif [ "x${ID}" == "xdebian" -o "x${ID}" == "xubuntu" ]; then
    apt-get -y update
    apt-get -y upgrade
    apt-get install -y python3
fi

PBS_DIR=$(dirname $(readlink -f $(basename $0)))
${PBS_DIR}/ci/ci --local
if [ $? -ne 0 ];then
    exit 1
fi
