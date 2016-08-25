# coding: utf-8

# Copyright (C) 1994-2016 Altair Engineering, Inc.
# For more information, contact Altair at www.altair.com.
#
# This file is part of the PBS Professional ("PBS Pro") software.
#
# Open Source License Information:
#
# PBS Pro is free software. You can redistribute it and/or modify it under the
# terms of the GNU Affero General Public License as published by the Free
# Software Foundation, either version 3 of the License, or (at your option) any
# later version.
#
# PBS Pro is distributed in the hope that it will be useful, but WITHOUT ANY
# WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
# PARTICULAR PURPOSE.  See the GNU Affero General Public License for more details.
#
# You should have received a copy of the GNU Affero General Public License along
# with this program.  If not, see <http://www.gnu.org/licenses/>.
#
# Commercial License Information:
#
# The PBS Pro software is licensed under the terms of the GNU Affero General
# Public License agreement ("AGPL"), except where a separate commercial license
# agreement for PBS Pro version 14 or later has been executed in writing with Altair.
#
# Altair’s dual-license business model allows companies, individuals, and
# organizations to create proprietary derivative works of PBS Pro and distribute
# them - whether embedded or bundled with other software - under a commercial
# license agreement.
#
# Use of Altair’s trademarks, including but not limited to "PBS™",
# "PBS Professional®", and "PBS Pro™" and Altair’s logos is subject to Altair's
# trademark licensing policies.

from ptl.utils.pbs_testsuite import *

class Test_qstat_tag(PBSTestSuite):
    """
    Test <jsdl-hpcpa:Executable> tag is dispalyed with "Executable" while doing qstat -f
    """
    def test_qstat_tag(self):
		"""
		Test <jsdl-hpcpa:Executable> tag is dispalyed with "Executable" while doing qstat -f
		"""
		ret = True
		j = Job(TEST_USER)
		j.set_sleep_time(10)
		jid = self.server.submit(j)
		qstat_cmd = os.path.join(self.server.pbs_conf['PBS_EXEC'], 'bin',
                             'qstat') + ' -f ' + str(jid)
		ret = self.du.run_cmd(self.server.hostname, cmd=qstat_cmd, sudo=True)
		if -1 != str(ret).find('Executable'):
			if -1 == str(ret).find('<jsdl-hpcpa:Executable>'):
				ret = False		
		self.assertTrue(ret)
    
