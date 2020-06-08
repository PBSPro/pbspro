# coding: utf-8

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


from tests.functional import *


class TestResourceMultiChunk(TestFunctional):

    """
    Test suite to test value of custom resource
    in a multi chunk job request
    """
    def setUp(self):
        TestFunctional.setUp(self)
        attr = {}
        attr['type'] = 'float'
        attr['flag'] = 'nh'
        r = 'foo_float'
        self.server.manager(MGR_CMD_CREATE, RSC, attr, id=r)
        self.scheduler.add_resource('foo_float')
        a = {'resources_available.foo_float': 4.2,
             'resources_available.ncpus': 2}
        self.server.manager(MGR_CMD_SET, NODE, a, self.mom.shortname)

    @skipOnCpuSet()
    def test_resource_float_type(self):
        """
        Test to check the value of custom resource
        in Resource_List.<custom resc> matches the value
        requested by the multi-chunk job
        """
        a = {'Resource_List.select': '2:ncpus=1:foo_float=0.8',
             'Resource_List.place': 'shared'}
        j = Job(TEST_USER, attrs=a)
        jid = self.server.submit(j)
        self.server.expect(JOB, {'job_state': 'R'}, id=jid)
        self.server.expect(JOB, {'Resource_List.foo_float': 1.6}, id=jid)
