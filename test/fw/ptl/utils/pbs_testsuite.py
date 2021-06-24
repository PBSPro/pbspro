# coding: utf-8

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


import calendar
import grp
import inspect
import logging
import os
import platform
import pwd
import socket
import subprocess
import sys
import time
import textwrap
import unittest
from distutils.util import strtobool

import ptl
from ptl.lib.pbs_testlib import *
from ptl.utils.pbs_cliutils import CliUtils
from ptl.utils.pbs_dshutils import DshUtils
from ptl.utils.pbs_logutils import PBSLogAnalyzer
from ptl.utils.pbs_procutils import ProcMonitor
from ptl.utils.pbs_testusers import *
try:
    from ptl.utils.plugins.ptl_test_tags import tags
except ImportError:
    def tags(*args, **kwargs):
        pass
try:
    from nose.plugins.skip import SkipTest
except ImportError:
    class SkipTest(Exception):
        pass

SETUPLOG = 'setuplog'
TEARDOWNLOG = 'teardownlog'

SMOKE = 'smoke'
REGRESSION = 'regression'
NUMNODES = 'numnodes'
TIMEOUT_KEY = '__testcase_timeout__'
MINIMUM_TESTCASE_TIMEOUT = 600
REQUIREMENTS_KEY = '__PTL_REQS_LIST__'

# unit of min_ram and min_disk is GB
default_requirements = {
    'num_servers': 1,
    'num_moms': 1,
    'num_comms': 1,
    'num_clients': 1,
    'min_mom_ram': .128,
    'min_mom_disk': 1,
    'min_server_ram': .128,
    'min_server_disk': 1,
    'mom_on_server': False,
    'no_mom_on_server': False,
    'no_comm_on_server': False,
    'no_comm_on_mom': True
}


def skip(reason="Skipped test execution"):
    """
    Unconditionally skip a test.

    :param reason: Reason for the skip
    :type reason: str or None
    """
    skip_flag = True

    def wrapper(test_item):
        test_item.__unittest_skip__ = skip_flag
        test_item.__unittest_skip_why__ = reason
        return test_item
    return wrapper


def timeout(val):
    """
    Decorator to set timeout value of test case
    """
    def wrapper(obj):
        setattr(obj, TIMEOUT_KEY, int(val))
        return obj
    return wrapper


def checkModule(modname):
    """
    Decorator to check if named module is available on the system
    and if not skip the test
    """
    def decorated(function):
        import imp
        try:
            imp.find_module(modname)
        except ImportError:
            function.__unittest_skip__ = True
            function.__unittest_skip_why__ = 'Module unavailable ' + modname
        return function
    return decorated


def skipOnCray(function):
    """
    Decorator to skip a test on a ``Cray`` system
    """
    function.__skip_on_cray__ = True
    return function


def skipOnShasta(function):
    """
    Decorator to skip a test on a ``Cray Shasta`` system
    """
    function.__skip_on_shasta__ = True
    return function


def skipOnCpuSet(function):
    """
    Decorator to skip a test on a cgroup cpuset system
    """
    function.__skip_on_cpuset__ = True
    return function


def runOnlyOnLinux(function):
    """
    """
    function.__run_only_on_linux__ = True
    return function


def checkMomBashVersion(function):
    """
    Decorator to skip a test if bash version is less than 4.2.46
    """
    function.__check_mom_bash_version__ = True
    return function


def requirements(*args, **kwargs):
    """
    Decorator to provide the cluster information required for a particular
    testcase.
    """
    def wrap_obj(obj):
        getreq = getattr(obj, REQUIREMENTS_KEY, {})
        for name, value in kwargs.items():
            getreq[name] = value
        setattr(obj, REQUIREMENTS_KEY, getreq)
        return obj
    return wrap_obj


def testparams(**kwargs):
    """
    Decorator to set or modify test specific parameters
    """
    def decorated(function):
        function.__doc__ += "Test Params:" + "\n\t"
        for key, value in kwargs.items():
            function.__doc__ += str(key) + ' : ' + str(value) + '\n\t'

        def wrapper(self, *args):
            self.testconf = {}
            for key, value in kwargs.items():
                keyname = type(self).__name__ + "." + key
                if keyname not in self.conf.keys():
                    self.conf[keyname] = value
                    self.testconf[keyname] = value
                else:
                    self.testconf[keyname] = self.conf[keyname]
                    t = type(value)
                    if t == bool:
                        if strtobool(self.conf[keyname]):
                            self.conf[keyname] = True
                        else:
                            self.conf[keyname] = False
                    else:
                        # If value is not a boolean then typecast
                        self.conf[keyname] = t(self.conf[keyname])

            function(self, *args)
        wrapper.__doc__ = function.__doc__
        wrapper.__name__ = function.__name__
        return wrapper
    return decorated


def generate_hook_body_from_func(hook_func, *args):
    return inspect.getsource(hook_func) + textwrap.dedent(
        """
        %s%s
        """ % (hook_func.__name__, str(args)))


class PBSServiceInstanceWrapper(dict):

    """
    A wrapper class to handle multiple service
    ``(i.e., mom, server, scheduler)``instances as passed along
    through the test harness ``(pbs_benchpress)``.Returns an
    ordered dictionary of PBS service instances ``(i.e., mom/server/
    scheduler)``

    Users may invoke PTL using pointers to multiple services per
    host, for example:

    ``pbs_benchpress -p moms=hostA@/etc/pbs.conf,hostB,hostA@/etc/pbs.conf3``

    In such cases, the moms instance variable must be able to distinguish
    the ``self.moms['hostA']`` instances, each instance will be mapped
    to a unique configuration file
    """

    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self.orderedlist = list(super().keys())

    def __setitem__(self, key, value):
        super(self.__class__, self).__setitem__(key, value)
        if key not in self.orderedlist:
            self.orderedlist.append(key)

    def __getitem__(self, key):
        for k, v in self.items():
            if k == key:
                return v
            if '@' in k:
                name, _ = k.split('@')
                if key in name:
                    return v
            else:
                name = k
            # Users may have specified shortnames instead of FQDN, in order
            # to not enforce that PBS_SERVER match the hostname passed in as
            # parameter, we check if a shortname matches a FQDN entry
            if '.' in key and key.split('.')[0] in name:
                return v
            if '.' in name and name.split('.')[0] in key:
                return v
        return None

    def __contains__(self, key):
        if key in self.keys():
            return True

        for k in self.keys():
            if '@' in k:
                name, _ = k.split('@')
                if key in name:
                    return True
            else:
                name = k
            # Users may have specified shortnames instead of FQDN, in order
            # to not enforce that PBS_SERVER match the hostname passed in as
            # parameter, we check if a shortname matches a FQDN entry
            if '.' in key and key.split('.')[0] in name:
                return True
            if '.' in name and name.split('.')[0] in key:
                return True
        return False

    def __iter__(self):
        return iter(self.orderedlist)

    def host_keys(self):
        return [h.split('@')[0] for h in self.keys()]

    def keys(self):
        return list(self.orderedlist)

    def values(self):
        return list(self[key] for key in self.orderedlist)


class setUpClassError(Exception):
    pass


class tearDownClassError(Exception):
    pass


class PBSTestSuite(unittest.TestCase):

    """
    Generic ``setup``, ``teardown``, and ``logging`` functions to
    be used as parent class for most tests.
    Class instantiates:

    ``server object connected to localhost``

    ``scheduler objected connected to localhost``

    ``mom object connected to localhost``

    Custom parameters:

    :param server: The hostname on which the PBS ``server/scheduler``
                   are running
    :param mom: The hostname on which the PBS MoM is running
    :param servers: Colon-separated list of hostnames hosting a PBS server.
                    Servers are then accessible as a dictionary in the
                    instance variable servers.
    :param client: For CLI mode only, name of the host on which the PBS
                   client commands are to be run from. Format is
                   ``<host>@<path-to-config-file>``
    :param moms: Colon-separated list of hostnames hosting a PBS MoM.
                 MoMs are made accessible as a dictionary in the instance
                 variable moms.
    :param comms: Colon-separated list of hostnames hosting a PBS Comm.
                  Comms are made accessible as a dictionary in the
                  instance variable comms.
    :param nomom: expect no MoM on colon-separated set of hosts
    :param mode: Sets mode of operation to PBS server. Can be either
                 ``'cli'`` or ``'api'``.Defaults to API behavior.
    :param conn_timeout: set a timeout in seconds after which a pbs_connect
                         IFL call is refreshed (i.e., disconnected)
    :param skip-setup: Bypasses setUp of PBSTestSuite (not custom ones)
    :param skip-teardown: Bypasses tearDown of PBSTestSuite (not custom ones)
    :param procinfo: Enables process monitoring thread, logged into
                     ptl_proc_info test metrics. The value can be set to
                     _all_ to monitor all PBS processes,including
                     ``pbs_server``, ``pbs_sched``, ``pbs_mom``, or a process
                     defined by name.
    :param revert-to-defaults=<True|False>: if False, will not revert to
                                            defaults.True by default.
    :param revert-hooks=<True|False>: if False, do not revert hooks to
                                      defaults.Defaults to True.
                                      ``revert-to-defaults`` set to False
                                      overrides this setting.
    :param del-hooks=<True|False>: If False, do not delete hooks. Defaults
                                   to False.``revert-to-defaults`` set to
                                   False overrides this setting.
    :param revert-queues=<True|False>: If False, do not revert queues to
                                       defaults.Defaults to True.
                                       ``revert-to-defaults`` set to False
                                       overrides this setting.
    :param revert-resources=<True|False>: If False, do not revert resources
                                          to defaults. Defaults to True.
                                          ``revert-to-defaults`` set to False
                                          overrides this setting.
    :param del-queues=<True|False>: If False, do not delete queues. Defaults
                                    to False.``revert-to-defaults`` set to
                                    Falseoverrides this setting.
    :param del-vnodes=<True|False>: If False, do not delete vnodes on MoM
                                    instances.Defaults to True.
    :param server-revert-to-defaults=<True|False>: if False, don't revert
                                                   Server to defaults
    :param comm-revert-to-defaults=<True|False>: if False, don't revert Comm
                                                 to defaults
    :param mom-revert-to-defaults=<True|False>: if False, don't revert MoM
                                                to defaults
    :param sched-revert-to-defaults=<True|False>: if False, don't revert
                                                  Scheduler to defaults
    :param procmon: Enables process monitoring. Multiple values must be
                    colon separated. For example to monitor ``server``,
                    ``sched``, and ``mom`` use
                    ``procmon=pbs_server:pbs_sched:pbs_mom``
    :param procmon-freq: Sets a polling frequency for the process monitoring
                         tool.Defaults to 10 seconds.
    :param test-users: colon-separated list of users to use as test users.
                       The users specified override the default users in the
                       order in which they appear in the ``PBS_USERS`` list.
    :param default-testcase-timeout: Default test case timeout value.
    :param data-users: colon-separated list of data users.
    :param oper-users: colon-separated list of operator users.
    :param mgr-users: colon-separated list of manager users.
    :param root-users: colon-separated list of root users.
    :param build-users: colon-separated list of build users.
    :param clienthost: the hostnames to set in the MoM config file
    """

    logger = logging.getLogger(__name__)
    metrics_data = {}
    measurements = []
    additional_data = {}
    conf = {}
    testconf = {}
    param = None
    du = DshUtils()
    _procmon = None
    _process_monitoring = False
    revert_to_defaults = True
    server_revert_to_defaults = True
    mom_revert_to_defaults = True
    sched_revert_to_defaults = True
    revert_queues = True
    revert_resources = True
    revert_hooks = True
    del_hooks = True
    del_queues = True
    del_scheds = True
    del_vnodes = True
    config_saved = False
    server = None
    scheduler = None
    mom = None
    comm = None
    servers = None
    schedulers = {}
    scheds = {}
    moms = None
    comms = None

    @classmethod
    def setUpClass(cls):
        cls.log_enter_setup(True)
        cls._testMethodName = 'setUpClass'
        cls.parse_param()
        cls.init_param()
        cls.check_users_exist()
        cls.init_servers()
        cls.init_schedulers()
        cls.init_moms()
        if cls.use_cur_setup:
            _, path = tempfile.mkstemp(prefix="saved_custom_setup",
                                       suffix=".json")
            ret = cls.server.save_configuration()
            if not ret:
                cls.logger.error("Failed to save server's custom setup")
                raise Exception("Failed to save server's custom setup")
            for mom in cls.moms.values():
                ret = mom.save_configuration()
                if not ret:
                    cls.logger.error("Failed to save mom's custom setup")
                    raise Exception("Failed to save mom's custom setup")
            ret = cls.scheduler.save_configuration(path, 'w')
            if ret:
                cls.saved_file = path
            else:
                cls.logger.error("Failed to save scheduler's custom setup")
                raise Exception("Failed to save scheduler's custom setup")
            cls.add_mgrs_opers()
        cls.init_comms()
        a = {ATTR_license_min: len(cls.moms)}
        cls.server.manager(MGR_CMD_SET, SERVER, a, sudo=True)
        cls.server.restart()
        cls.log_end_setup(True)
        # methods for skipping tests with ptl decorators
        cls.populate_test_dict()
        cls.skip_cray_tests()
        cls.skip_shasta_tests()
        cls.skip_cpuset_tests()
        cls.run_only_on_linux()
        cls.check_mom_bash_version()

    def setUp(self):
        if 'skip-setup' in self.conf:
            return
        self.log_enter_setup()
        self.init_proc_mon()
        if not PBSTestSuite.config_saved and self.use_cur_setup:
            _, path = tempfile.mkstemp(prefix="saved_test_setup",
                                       suffix=".json")
            ret = self.server.save_configuration()
            if not ret:
                self.logger.error("Failed to save server's test setup")
                raise Exception("Failed to save server's test setup")
            for mom in self.moms.values():
                ret = mom.save_configuration()
                if not ret:
                    self.logger.error("Failed to save mom's test setup")
                    raise Exception("Failed to save mom's test setup")
            ret = self.scheduler.save_configuration(path, 'w')
            if ret:
                self.saved_file = path
            else:
                self.logger.error("Failed to save scheduler's test setup")
                raise Exception("Failed to save scheduler's test setup")
            PBSTestSuite.config_saved = True
        # Adding only server, mom & scheduler and pbs.conf methods in use
        # current setup block, rest of them to be added to this block
        # once save & load configurations are implemented for
        # comm
        if not self.use_cur_setup:
            self.revert_servers()
            self.revert_pbsconf()
            self.revert_schedulers()
            self.revert_moms()

        # turn off opt_backfill_fuzzy to avoid unexpected calendaring behavior
        # as many tests assume that scheduler will simulate each event
        a = {'opt_backfill_fuzzy': 'off'}
        for schedinfo in self.schedulers.values():
            for schedname in schedinfo.keys():
                self.server.manager(MGR_CMD_SET, SCHED, a, id=schedname)

        self.revert_comms()
        self.log_end_setup()
        self.measurements = []

    @classmethod
    def populate_test_dict(cls):
        cls.test_dict = {}
        for attr in dir(cls):
            if attr.startswith('test'):
                obj = getattr(cls, attr)
                if callable(obj):
                    cls.test_dict[attr] = obj

    @classmethod
    def skip_cray_tests(cls):
        if not cls.mom.is_cray():
            return
        msg = 'capability not supported on Cray'
        if cls.__dict__.get('__skip_on_cray__', False):
            # skip all test cases in this test suite
            for test_item in cls.test_dict.values():
                test_item.__unittest_skip__ = True
                test_item.__unittest_skip_why__ = msg
        else:
            # skip individual test cases
            for test_item in cls.test_dict.values():
                if test_item.__dict__.get('__skip_on_cray__', False):
                    test_item.__unittest_skip__ = True
                    test_item.__unittest_skip_why__ = msg

    @classmethod
    def skip_shasta_tests(cls):
        if not cls.mom.is_shasta():
            return
        msg = 'capability not supported on Cray Shasta'
        if cls.__dict__.get('__skip_on_shasta__', False):
            # skip all test cases in this test suite
            for test_item in cls.test_dict.values():
                test_item.__unittest_skip__ = True
                test_item.__unittest_skip_why__ = msg
        else:
            # skip individual test cases
            for test_item in cls.test_dict.values():
                if test_item.__dict__.get('__skip_on_shasta__', False):
                    test_item.__unittest_skip__ = True
                    test_item.__unittest_skip_why__ = msg

    @classmethod
    def skip_cpuset_tests(cls):
        skip_cpuset_tests = False
        for mom in cls.moms.values():
            if mom.is_cpuset_mom():
                skip_cpuset_tests = True
                msg = 'capability not supported on cgroup cpuset system: '
                msg += mom.shortname
                break
        if not skip_cpuset_tests:
            return
        if cls.__dict__.get('__skip_on_cpuset__', False):
            # skip all test cases in this test suite
            for test_item in cls.test_dict.values():
                test_item.__unittest_skip__ = True
                test_item.__unittest_skip_why__ = msg
        else:
            # skip individual test cases
            for test_item in cls.test_dict.values():
                if test_item.__dict__.get('__skip_on_cpuset__', False):
                    test_item.__unittest_skip__ = True
                    test_item.__unittest_skip_why__ = msg

    @classmethod
    def run_only_on_linux(cls):
        if cls.mom.is_only_linux():
            return
        msg = 'capability supported only on Linux'
        if cls.__dict__.get('__run_only_on_linux__', False):
            # skip all test cases in this test suite
            for test_item in cls.test_dict.values():
                test_item.__unittest_skip__ = True
                test_item.__unittest_skip_why__ = msg
        else:
            # skip individual test cases
            for test_item in cls.test_dict.values():
                if test_item.__dict__.get('__run_only_on_linux__', False):
                    test_item.__unittest_skip__ = True
                    test_item.__unittest_skip_why__ = msg

    @classmethod
    def check_mom_bash_version(cls):
        skip_test = False
        msg = 'capability supported only for bash version >= 4.2.46'
        for mom in cls.moms.values():
            if not mom.check_mom_bash_version():
                skip_test = True
                break
        if not skip_test:
            return
        if cls.__dict__.get('__check_mom_bash_version__', False):
            # skip all test cases in this test suite
            for test_item in cls.test_dict.values():
                test_item.__unittest_skip__ = True
                test_item.__unittest_skip_why__ = msg
        else:
            # skip individual test cases
            for test_item in cls.test_dict.values():
                if test_item.__dict__.get('__check_mom_bash_version__', False):
                    test_item.__unittest_skip__ = True
                    test_item.__unittest_skip_why__ = msg

    @classmethod
    def log_enter_setup(cls, iscls=False):
        _m = ' Entered ' + cls.__name__ + ' setUp'
        if iscls:
            _m += 'Class'
        _m_len = len(_m)
        cls.logger.info('=' * _m_len)
        cls.logger.info(_m)
        cls.logger.info('=' * _m_len)

    @classmethod
    def log_end_setup(cls, iscls=False):
        _m = 'Completed ' + cls.__name__ + ' setUp'
        if iscls:
            _m += 'Class'
        _m_len = len(_m)
        cls.logger.info('=' * _m_len)
        cls.logger.info(_m)
        cls.logger.info('=' * _m_len)

    @classmethod
    def _validate_param(cls, pname):
        """
        Check if parameter was enabled at the ``command-line``

        :param pname: parameter name
        :type pname: str
        :param pvar: class variable to set according to command-line setting
        """
        if pname not in cls.conf:
            return
        if cls.conf[pname] in PTL_TRUE:
            setattr(cls, pname.replace('-', '_'), True)
        else:
            setattr(cls, pname.replace('-', '_'), False)

    @classmethod
    def _set_user(cls, name, user_list):
        if name in cls.conf:
            for idx, u in enumerate(cls.conf[name].split(':')):
                user_list[idx].__init__(u)

    @classmethod
    def check_users_exist(cls):
        """
        Check whether the user is exist or not
        """
        testusersexist = True
        for u in PBS_ALL_USERS:
            rv = cls.du.check_user_exists(u.name, u.host, u.port)
            if not rv:
                _msg = 'User ' + str(u) + ' does not exist!'
                raise setUpClassError(_msg)
        return testusersexist

    @classmethod
    def kicksched_action(cls, server, obj_type, *args, **kwargs):
        """
        custom scheduler action to kick a scheduling cycle when expectig
        a job state change
        """
        if server is None:
            cls.logger.error('no server defined for custom action')
            return
        if obj_type == JOB:
            if (('scheduling' in server.attributes) and
                    (server.attributes['scheduling'] != 'False')):
                server.manager(MGR_CMD_SET, MGR_OBJ_SERVER,
                               {'scheduling': 'True'},
                               level=logging.DEBUG)

    @classmethod
    def parse_param(cls):
        """
        get test configuration parameters as a ``comma-separated``
        list of attributes.

        Attributes may be ``'='`` separated key value pairs or standalone
        entries.

        ``Multi-property`` attributes are colon-delimited.
        """
        if cls.param is None:
            return
        for h in cls.param.split(','):
            if '=' in h:
                k, v = h.split('=')
                cls.conf[k.strip()] = v.strip()
            else:
                cls.conf[h.strip()] = ''
        if (('clienthost' in cls.conf) and
                not isinstance(cls.conf['clienthost'], list)):
            cls.conf['clienthost'] = cls.conf['clienthost'].split(':')
        users_map = [('test-users', PBS_USERS),
                     ('oper-users', PBS_OPER_USERS),
                     ('mgr-users', PBS_MGR_USERS),
                     ('data-users', PBS_DATA_USERS),
                     ('root-users', PBS_ROOT_USERS),
                     ('build-users', PBS_BUILD_USERS),
                     ('daemon-users', PBS_DAEMON_SERVICE_USERS)]
        for k, v in users_map:
            cls._set_user(k, v)

    @classmethod
    def init_param(cls):
        cls._validate_param('revert-to-defaults')
        cls._validate_param('server-revert-to-defaults')
        cls._validate_param('comm-revert-to-defaults')
        cls._validate_param('mom-revert-to-defaults')
        cls._validate_param('sched-revert-to-defaults')
        cls._validate_param('del-hooks')
        cls._validate_param('revert-hooks')
        cls._validate_param('del-queues')
        cls._validate_param('del-vnodes')
        cls._validate_param('revert-queues')
        cls._validate_param('revert-resources')

    @classmethod
    def is_server_licensed(cls, server):
        """
        Check if server is licensed or not
        """
        for i in range(0, 10, 1):
            lic = server.status(SERVER, 'license_count', level=logging.INFOCLI)
            if lic and 'license_count' in lic[0]:
                lic = PbsTypeLicenseCount(lic[0]['license_count'])
                if ('Avail_Nodes' in lic) and (int(lic['Avail_Nodes']) > 0):
                    return True
                elif (('Avail_Sockets' in lic) and
                        (int(lic['Avail_Sockets']) > 0)):
                    return True
                elif (('Avail_Global' in lic) and
                        (int(lic['Avail_Global']) > 0)):
                    return True
                elif ('Avail_Local' in lic) and (int(lic['Avail_Local']) > 0):
                    return True
            time.sleep(i)
        return False

    @classmethod
    def init_from_conf(cls, conf, single=None, multiple=None, skip=None,
                       func=None):
        """
        Helper method to parse test parameters for`` mom/server/scheduler``
        instances.

        The supported format of each service request is:

        ``hostname@configuration/path``

        For example:

        ``pbs_benchpress -p server=remote@/etc/pbs.conf.12.0``

        initializes a remote server instance that is configured according to
        the remote file ``/etc/pbs.conf.12.0``
        """
        endpoints = []
        if ((multiple in conf) and (conf[multiple] is not None)):
            __objs = conf[multiple].split(':')
            for _m in __objs:
                tmp = _m.split('@')
                if len(tmp) == 2:
                    endpoints.append(tuple(tmp))
                elif len(tmp) == 1:
                    endpoints.append((tmp[0], None))
        elif ((single in conf) and (conf[single] is not None)):
            tmp = conf[single].split('@')
            if len(tmp) == 2:
                endpoints.append(tuple(tmp))
            elif len(tmp) == 1:
                endpoints.append((tmp[0], None))
        else:
            endpoints = [(socket.gethostname(), None)]
        objs = PBSServiceInstanceWrapper()
        for name, objconf in endpoints:
            if ((skip is not None) and (skip in conf) and
                    ((name in conf[skip]) or (conf[skip] in name))):
                continue
            if objconf is not None:
                n = name + '@' + objconf
            else:
                n = name
            if getattr(cls, "server", None) is not None:
                objs[n] = func(name, pbsconf_file=objconf,
                               server=cls.server.hostname)
            else:
                objs[n] = func(name, pbsconf_file=objconf)
            if objs[n] is None:
                _msg = 'Failed %s(%s, %s)' % (func.__name__, name, objconf)
                raise setUpClassError(_msg)
            objs[n].initialise_service()
        return objs

    @classmethod
    def init_servers(cls, init_server_func=None, skip=None):
        """
        Initialize servers
        """
        if init_server_func is None:
            init_server_func = cls.init_server
        if 'servers' in cls.conf:
            server_param = cls.conf['servers']
            if 'comms' not in cls.conf and 'comm' not in cls.conf:
                cls.conf['comms'] = server_param
            if 'scheduler' not in cls.conf and 'schedulers' not in cls.conf:
                cls.conf['schedulers'] = server_param
            if 'moms' not in cls.conf and 'mom' not in cls.conf:
                cls.conf['moms'] = server_param
        if 'server' in cls.conf:
            server_param = cls.conf['server']
            if 'comm' not in cls.conf:
                cls.conf['comm'] = server_param
            if 'scheduler' not in cls.conf:
                cls.conf['scheduler'] = server_param
            if 'mom' not in cls.conf:
                cls.conf['mom'] = server_param
        cls.servers = cls.init_from_conf(conf=cls.conf, single='server',
                                         multiple='servers', skip=skip,
                                         func=init_server_func)
        if cls.servers:
            cls.server = cls.servers.values()[0]
            for _server in cls.servers.values():
                rv = _server.isUp()
                if not rv:
                    cls.logger.error('server ' + _server.hostname + ' is down')
                    _server.pi.restart(_server.hostname)
                    msg = 'Failed to restart server ' + _server.hostname
                    cls.assertTrue(_server.isUp(), msg)

    @classmethod
    def init_comms(cls, init_comm_func=None, skip=None):
        """
        Initialize comms
        """
        if init_comm_func is None:
            init_comm_func = cls.init_comm
        cls.comms = cls.init_from_conf(conf=cls.conf,
                                       single='comm',
                                       multiple='comms', skip=skip,
                                       func=init_comm_func)
        if cls.comms:
            cls.comm = cls.comms.values()[0]
        cls.server.comms = cls.comms

    @classmethod
    def init_schedulers(cls, init_sched_func=None, skip=None):
        """
        Initialize schedulers
        """
        if init_sched_func is None:
            init_sched_func = cls.init_scheduler
        cls.scheds = cls.init_from_conf(conf=cls.conf,
                                        single='scheduler',
                                        multiple='schedulers', skip=skip,
                                        func=init_sched_func)

        for sched in cls.scheds.values():
            if sched.server.name in cls.schedulers:
                continue
            else:
                cls.schedulers[sched.server.name] = sched.server.schedulers
        # creating a short hand for current host server.schedulers
        cls.scheds = cls.server.schedulers
        try:
            cls.scheduler = cls.scheds['default']
        except KeyError:
            cls.logger.error("Could not get default scheduler:%s, "
                             "check the server(core), server.isUp:%s" %
                             (str(cls.scheds), cls.server.isUp()))
            raise

    @classmethod
    def init_moms(cls, init_mom_func=None, skip='nomom'):
        """
        Initialize moms
        """
        if init_mom_func is None:
            init_mom_func = cls.init_mom
        cls.moms = cls.init_from_conf(conf=cls.conf, single='mom',
                                      multiple='moms', skip=skip,
                                      func=init_mom_func)
        if cls.moms:
            cls.mom = cls.moms.values()[0]
        cls.server.moms = cls.moms

    @classmethod
    def init_server(cls, hostname, pbsconf_file=None):
        """
        Initialize a server instance

        Define custom expect action to trigger a scheduling cycle when job
        is not in running state

        :returns: The server instance on success and None on failure
        """
        client = hostname
        client_conf = None
        if 'client' in cls.conf:
            _cl = cls.conf['client'].split('@')
            client = _cl[0]
            if len(_cl) > 1:
                client_conf = _cl[1]
        server = Server(hostname, pbsconf_file=pbsconf_file, client=client,
                        client_pbsconf_file=client_conf)
        server._conn_timeout = 0
        if cls.conf is not None:
            if 'mode' in cls.conf:
                if cls.conf['mode'] == 'cli':
                    server.set_op_mode(PTL_CLI)
            if 'conn_timeout' in cls.conf:
                conn_timeout = int(cls.conf['conn_timeout'])
                server.set_connect_timeout(conn_timeout)
        sched_action = ExpectAction('kicksched', True, JOB,
                                    cls.kicksched_action)
        server.add_expect_action(action=sched_action)
        return server

    @classmethod
    def init_comm(cls, hostname, pbsconf_file=None, server=None):
        """
        Initialize a Comm instance associated to the given hostname.

        This method must be called after init_server

        :param hostname: The host on which the Comm is running
        :type hostname: str
        :param pbsconf_file: Optional path to an alternate pbs config file
        :type pbsconf_file: str or None
        :returns: The instantiated Comm upon success and None on failure.
        :param server: The server name associated to the Comm
        :type server: str
        Return the instantiated Comm upon success and None on failure.
        """
        try:
            server = cls.servers[server]
        except BaseException:
            server = Server(hostname, pbsconf_file=pbsconf_file)
        return Comm(server, hostname, pbsconf_file=pbsconf_file)

    @classmethod
    def init_scheduler(cls, hostname, pbsconf_file=None, server=None):
        """
        Initialize a Scheduler instance associated to the given server.
        This method must be called after ``init_server``

        :param server: The server name associated to the scheduler
        :type server: str
        :param pbsconf_file: Optional path to an alternate config file
        :type pbsconf_file: str or None
        :param hostname: The host on which Sched is running
        :type hostname: str
        :returns: The instantiated scheduler upon success and None on failure
        """
        try:
            server = cls.servers[server]
        except BaseException:
            server = Server(hostname, pbsconf_file=pbsconf_file)
        return Scheduler(server, hostname=hostname,
                         pbsconf_file=pbsconf_file)

    @classmethod
    def init_mom(cls, hostname, pbsconf_file=None, server=None):
        """
        Initialize a ``MoM`` instance associated to the given hostname.

        This method must be called after ``init_server``

        :param hostname: The host on which the MoM is running
        :type hostname: str
        :param pbsconf_file: Optional path to an alternate pbs config file
        :type pbsconf_file: str or None
        :returns: The instantiated MoM upon success and None on failure.
        """
        try:
            server = cls.servers[server]
        except BaseException:
            server = Server(hostname, pbsconf_file=pbsconf_file)
        return get_mom_obj(server, hostname, pbsconf_file=pbsconf_file)

    def init_proc_mon(self):
        """
        Initialize process monitoring when requested
        """
        if 'procmon' in self.conf:
            _proc_mon = []
            for p in self.conf['procmon'].split(':'):
                _proc_mon += ['.*' + p + '.*']
            if _proc_mon:
                if 'procmon-freq' in self.conf:
                    freq = int(self.conf['procmon-freq'])
                else:
                    freq = 10
                self.start_proc_monitor(name='|'.join(_proc_mon), regexp=True,
                                        frequency=freq)
                self._process_monitoring = True

    def _get_dflt_pbsconfval(self, conf, svr_hostname, hosttype, hostobj):
        """
        Helper function to revert_pbsconf, tries to determine and return
        default value for the pbs.conf variable given

        :param conf: the pbs.conf variable
        :type conf: str
        :param svr_hostname: hostname of the server host
        :type svr_hostname: str
        :param hosttype: type of host being reverted
        :type hosttype: str
        :param hostobj: PTL object associated with the host
        :type hostobj: PBSService

        :return default value of the pbs.conf variable if it can be determined
        as a string, otherwise None
        """
        if conf == "PBS_SERVER":
            return svr_hostname
        elif conf == "PBS_START_SCHED":
            if hosttype == "server":
                return "1"
            else:
                return "0"
        elif conf == "PBS_START_COMM":
            if hosttype == "comm":
                return "1"
            else:
                return "0"
        elif conf == "PBS_START_SERVER":
            if hosttype == "server":
                return "1"
            else:
                return "0"
        elif conf == "PBS_START_MOM":
            if hosttype == "mom":
                return "1"
            else:
                return "0"
        elif conf == "PBS_CORE_LIMIT":
            return "unlimited"
        elif conf == "PBS_SCP":
            scppath = self.du.which(hostobj.hostname, "scp")
            if scppath != "scp":
                return scppath
        elif conf == "PBS_LOG_HIGHRES_TIMESTAMP":
            return "1"
        elif conf == "PBS_PUBLIC_HOST_NAME":
            if hostobj.platform == "shasta" and hosttype == "server":
                return socket.gethostname()
            else:
                return None
        elif conf == "PBS_DAEMON_SERVICE_USER":
            # Only set if scheduler user is not default
            if DAEMON_SERVICE_USER.name == 'root':
                return None
            else:
                return DAEMON_SERVICE_USER.name

        return None

    def _revert_pbsconf_comm(self, primary_server, vals_to_set):
        """
        Helper function to revert_pbsconf to revert all comm daemons' pbs.conf

        :param primary_server: object of the primary PBS server
        :type primary_server: PBSService
        :param vals_to_set: dict of pbs.conf values to set
        :type vals_to_set: dict
        """
        svr_hostnames = [svr.hostname for svr in self.servers.values()]
        for comm in self.comms.values():
            if comm.hostname in svr_hostnames:
                continue

            new_pbsconf = dict(vals_to_set)
            restart_comm = False
            pbs_conf_val = self.du.parse_pbs_config(comm.hostname)
            if not pbs_conf_val:
                raise ValueError("Could not parse pbs.conf on host %s" %
                                 (comm.hostname))

            # to start with, set all keys in new_pbsconf with values from the
            # existing pbs.conf
            keys_to_delete = []
            for conf in new_pbsconf:
                if new_pbsconf[conf]:
                    if (conf in pbs_conf_val) and (new_pbsconf[conf] !=
                                                   pbs_conf_val[conf]):
                        restart_pbs = True
                    elif conf not in pbs_conf_val:
                        restart_pbs = True
                    continue
                elif conf in pbs_conf_val:
                    new_pbsconf[conf] = pbs_conf_val[conf]
                elif conf in pbs_conf_val:
                    new_pbsconf[conf] = pbs_conf_val[conf]
                else:
                    # existing pbs.conf doesn't have a default variable set
                    # Try to determine the default
                    val = self._get_dflt_pbsconfval(conf,
                                                    primary_server.hostname,
                                                    "comm", comm)
                    if val is None:
                        self.logger.info("Couldn't revert %s in pbs.conf"
                                         " to its default value" %
                                         (conf))
                        keys_to_delete.append(conf)
                    else:
                        new_pbsconf[conf] = val

            for key in keys_to_delete:
                del(new_pbsconf[key])

            # Set the comm start bit to 1
            if new_pbsconf["PBS_START_COMM"] != "1":
                new_pbsconf["PBS_START_COMM"] = "1"
                restart_comm = True

            # Set PBS_CORE_LIMIT, PBS_SCP and PBS_SERVER
            if new_pbsconf["PBS_CORE_LIMIT"] != "unlimited":
                new_pbsconf["PBS_CORE_LIMIT"] = "unlimited"
                restart_comm = True
            if new_pbsconf["PBS_SERVER"] != primary_server.hostname:
                new_pbsconf["PBS_SERVER"] = primary_server.hostname
                restart_comm = True
            if "PBS_SCP" not in new_pbsconf:
                scppath = self.du.which(comm.hostname, "scp")
                if scppath != "scp":
                    new_pbsconf["PBS_SCP"] = scppath
                    restart_comm = True
            if new_pbsconf["PBS_LOG_HIGHRES_TIMESTAMP"] != "1":
                new_pbsconf["PBS_LOG_HIGHRES_TIMESTAMP"] = "1"
                restart_comm = True

            # Check if existing pbs.conf has more/less entries than the
            # default list
            if len(pbs_conf_val) != len(new_pbsconf):
                restart_comm = True
            # Check if existing pbs.conf has correct ownership
            dest = self.du.get_pbs_conf_file(comm.hostname)
            (cf_uid, cf_gid) = (os.stat(dest).st_uid, os.stat(dest).st_gid)
            if cf_uid != 0 or cf_gid > 10:
                restart_comm = True

            if restart_comm:
                self.du.set_pbs_config(comm.hostname, confs=new_pbsconf)
                comm.pbs_conf = new_pbsconf
                comm.pi.initd(comm.hostname, "restart", daemon="comm")
                if not comm.isUp():
                    self.fail("comm is not up")

    def _revert_pbsconf_mom(self, primary_server, vals_to_set):
        """
        Helper function to revert_pbsconf to revert all mom daemons' pbs.conf

        :param primary_server: object of the primary PBS server
        :type primary_server: PBSService
        :param vals_to_set: dict of pbs.conf values to set
        :type vals_to_set: dict
        """
        svr_hostnames = [svr.hostname for svr in self.servers.values()]
        for mom in self.moms.values():
            if mom.hostname in svr_hostnames:
                continue
            mom.revert_mom_pbs_conf(primary_server, vals_to_set)

    def _revert_pbsconf_server(self, vals_to_set):
        """
        Helper function to revert_pbsconf to revert all servers' pbs.conf

        :param vals_to_set: dict of pbs.conf values to set
        :type vals_to_set: dict
        """
        for server in self.servers.values():
            new_pbsconf = dict(vals_to_set)
            cmds_to_exec = []
            dmns_to_restart = 0
            restart_pbs = False
            pbs_conf_val = self.du.parse_pbs_config(server.hostname)
            if not pbs_conf_val:
                raise ValueError("Could not parse pbs.conf on host %s" %
                                 (server.hostname))

            # to start with, set all keys in new_pbsconf with values from the
            # existing pbs.conf
            keys_to_delete = []
            for conf in new_pbsconf:
                if new_pbsconf[conf]:
                    if (conf in pbs_conf_val) and (new_pbsconf[conf] !=
                                                   pbs_conf_val[conf]):
                        restart_pbs = True
                    elif conf not in pbs_conf_val:
                        restart_pbs = True
                    continue
                elif conf in pbs_conf_val:
                    new_pbsconf[conf] = pbs_conf_val[conf]
                elif conf in pbs_conf_val:
                    new_pbsconf[conf] = pbs_conf_val[conf]
                else:
                    # existing pbs.conf doesn't have a default variable set
                    # Try to determine the default
                    val = self._get_dflt_pbsconfval(conf,
                                                    server.hostname,
                                                    "server", server)
                    if val is None:
                        self.logger.error("Couldn't revert %s in pbs.conf"
                                          " to its default value" %
                                          (conf))
                        keys_to_delete.append(conf)
                    else:
                        new_pbsconf[conf] = val

            for key in keys_to_delete:
                del(new_pbsconf[key])

            # Set all start bits
            if (new_pbsconf["PBS_START_SERVER"] != "1"):
                new_pbsconf["PBS_START_SERVER"] = "1"
                dmns_to_restart += 1
                cmds_to_exec.append(["server", "start"])
            if (new_pbsconf["PBS_START_SCHED"] != "1"):
                new_pbsconf["PBS_START_SCHED"] = "1"
                cmds_to_exec.append(["sched", "start"])
                dmns_to_restart += 1
            if self.moms and server.hostname not in self.moms:
                if new_pbsconf["PBS_START_MOM"] != "0":
                    new_pbsconf["PBS_START_MOM"] = "0"
                    cmds_to_exec.append(["mom", "stop"])
                    dmns_to_restart += 1
            else:
                if (new_pbsconf["PBS_START_MOM"] != "1"):
                    new_pbsconf["PBS_START_MOM"] = "1"
                    cmds_to_exec.append(["mom", "start"])
                    dmns_to_restart += 1
            if self.comms and server.hostname not in self.comms:
                if new_pbsconf["PBS_START_COMM"] != "0":
                    new_pbsconf["PBS_START_COMM"] = "0"
                    cmds_to_exec.append(["comm", "stop"])
            else:
                if (new_pbsconf["PBS_START_COMM"] != "1"):
                    new_pbsconf["PBS_START_COMM"] = "1"
                    cmds_to_exec.append(["comm", "start"])
                    dmns_to_restart += 1

            if dmns_to_restart == 4:
                # If all daemons need to be started again, just restart PBS
                # instead of making PTL start each of them one at a time
                restart_pbs = True

            # Set PBS_CORE_LIMIT, PBS_SCP, PBS_SERVER
            # and PBS_LOG_HIGHRES_TIMESTAMP
            if new_pbsconf["PBS_CORE_LIMIT"] != "unlimited":
                new_pbsconf["PBS_CORE_LIMIT"] = "unlimited"
                restart_pbs = True
            if new_pbsconf["PBS_SERVER"] != server.shortname:
                new_pbsconf["PBS_SERVER"] = server.shortname
                restart_pbs = True
            if "PBS_SCP" not in new_pbsconf:
                scppath = self.du.which(server.hostname, "scp")
                if scppath != "scp":
                    new_pbsconf["PBS_SCP"] = scppath
                    restart_pbs = True
            if new_pbsconf["PBS_LOG_HIGHRES_TIMESTAMP"] != "1":
                new_pbsconf["PBS_LOG_HIGHRES_TIMESTAMP"] = "1"
                restart_pbs = True
            if DAEMON_SERVICE_USER.name == 'root':
                if "PBS_DAEMON_SERVICE_USER" in new_pbsconf:
                    del(new_pbsconf['PBS_DAEMON_SERVICE_USER'])
                    restart_pbs = True
            elif (new_pbsconf["PBS_DAEMON_SERVICE_USER"] !=
                  DAEMON_SERVICE_USER.name):
                new_pbsconf["PBS_DAEMON_SERVICE_USER"] = \
                    DAEMON_SERVICE_USER.name
                restart_pbs = True
            # if shasta, set PBS_PUBLIC_HOST_NAME
            if server.platform == 'shasta':
                localhost = socket.gethostname()
                if new_pbsconf["PBS_PUBLIC_HOST_NAME"] != localhost:
                    new_pbsconf["PBS_PUBLIC_HOST_NAME"] = localhost
                    restart_pbs = True

            # Check if existing pbs.conf has more/less entries than the
            # default list
            if len(pbs_conf_val) != len(new_pbsconf):
                restart_pbs = True
            # Check if existing pbs.conf has correct ownership
            dest = self.du.get_pbs_conf_file(server.hostname)
            (cf_uid, cf_gid) = (os.stat(dest).st_uid, os.stat(dest).st_gid)
            if cf_uid != 0 or cf_gid > 10:
                restart_pbs = True

            if restart_pbs or dmns_to_restart > 0:
                # Write out the new pbs.conf file
                self.du.set_pbs_config(server.hostname, confs=new_pbsconf,
                                       append=False)
                server.pbs_conf = new_pbsconf

                if restart_pbs:
                    # Restart all
                    server.pi.restart(server.hostname)
                    self._check_daemons_on_server(server, "server")
                    if new_pbsconf["PBS_START_MOM"] == "1":
                        self._check_daemons_on_server(server, "mom")
                    self._check_daemons_on_server(server, "sched")
                    if new_pbsconf["PBS_START_COMM"] == "1":
                        self._check_daemons_on_server(server, "comm")
                else:
                    for initcmd in cmds_to_exec:
                        # start/stop the particular daemon
                        server.pi.initd(server.hostname, initcmd[1],
                                        daemon=initcmd[0])
                        if initcmd[1] == "start":
                            if initcmd[0] == "server":
                                self._check_daemons_on_server(server, "server")
                            if initcmd[0] == "sched":
                                self._check_daemons_on_server(server, "sched")
                            if initcmd[0] == "mom":
                                self._check_daemons_on_server(server, "mom")
                            if initcmd[0] == "comm":
                                self._check_daemons_on_server(server, "comm")

    def _check_daemons_on_server(self, server_obj, daemon_name):
        """
        Checks if specified daemon is up and running on server host
        server_obj : server
        daemon_name : server/sched/mom/comm
        """
        if daemon_name == "server":
            if not server_obj.isUp():
                self.fail("Server is not up")
        elif daemon_name == "sched":
            if not server_obj.schedulers['default'].isUp():
                self.fail("Scheduler is not up")
        elif daemon_name == "mom":
            if not server_obj.moms.values()[0].isUp():
                self.fail("Mom is not up")
        elif daemon_name == "comm":
            if not server_obj.comms.values()[0].isUp():
                self.fail("Comm is not up")
        else:
            self.fail("Incorrect daemon specified")

    def revert_pbsconf(self):
        """
        Revert contents and ownership of the pbs.conf file
        Also start/stop the appropriate daemons
        """
        primary_server = self.server

        vals_to_set = {
            "PBS_HOME": None,
            "PBS_EXEC": None,
            "PBS_SERVER": None,
            "PBS_START_SCHED": None,
            "PBS_START_COMM": None,
            "PBS_START_SERVER": None,
            "PBS_START_MOM": None,
            "PBS_CORE_LIMIT": None,
            "PBS_SCP": None,
            "PBS_LOG_HIGHRES_TIMESTAMP": None
        }

        self._revert_pbsconf_mom(primary_server, vals_to_set)

        server_vals_to_set = copy.deepcopy(vals_to_set)

        server_vals_to_set["PBS_DAEMON_SERVICE_USER"] = None
        if primary_server.platform == 'shasta':
            server_vals_to_set["PBS_PUBLIC_HOST_NAME"] = None

        self._revert_pbsconf_server(server_vals_to_set)

        self._revert_pbsconf_comm(primary_server, vals_to_set)

    def revert_servers(self, force=False):
        """
        Revert the values set for servers
        """
        for server in self.servers.values():
            self.revert_server(server, force)

    def revert_comms(self, force=False):
        """
        Revert the values set for comms
        """
        for comm in self.comms.values():
            self.revert_comm(comm, force)

    def revert_schedulers(self, force=False):
        """
        Revert the values set for schedulers
        """
        for scheds in self.schedulers.values():
            if 'default' in scheds:
                self.revert_scheduler(scheds['default'], force)

    def revert_moms(self, force=False):
        """
        Revert the values set for moms
        """
        self.del_all_nodes = True
        for mom in self.moms.values():
            self.revert_mom(mom, force)

    @classmethod
    def add_mgrs_opers(cls):
        """
        Adding manager and operator users
        """
        if not cls.use_cur_setup:
            try:
                # Unset managers list
                cls.server.manager(MGR_CMD_UNSET, SERVER, 'managers',
                                   sudo=True)
                # Unset operators list
                cls.server.manager(MGR_CMD_UNSET, SERVER, 'operators',
                                   sudo=True)
            except PbsManagerError as e:
                cls.logger.error(e.msg)
        attr = {}
        current_user = pwd.getpwuid(os.getuid())[0]
        if str(current_user) in str(MGR_USER):
            mgrs_opers = {"managers": [str(MGR_USER) + '@*'],
                          "operators": [str(OPER_USER) + '@*']}
        else:
            current_user += '@*'
            mgrs_opers = {"managers": [current_user, str(MGR_USER) + '@*'],
                          "operators": [str(OPER_USER) + '@*']}
        server_stat = cls.server.status(SERVER, ["managers", "operators"])
        if len(server_stat) > 0:
            server_stat = server_stat[0]
        for role, users in mgrs_opers.items():
            if role not in server_stat:
                attr[role] = (INCR, ','.join(users))
            else:
                add_users = []
                for user in users:
                    if user not in server_stat[role]:
                        add_users.append(user)
                if len(add_users) > 0:
                    attr[role] = (INCR, ",".join(add_users))
        if len(attr) > 0:
            cls.server.manager(MGR_CMD_SET, SERVER, attr, sudo=True)

    def revert_server(self, server, force=False):
        """
        Revert the values set for server
        """
        rv = server.isUp()
        if not rv:
            self.logger.error('server ' + server.hostname + ' is down')
            server.start()
            msg = 'Failed to restart server ' + server.hostname
            self.assertTrue(server.isUp(), msg)
        server_stat = server.status(SERVER)[0]
        self.add_mgrs_opers()
        if ((self.revert_to_defaults and self.server_revert_to_defaults) or
                force):
            server.revert_to_defaults(reverthooks=self.revert_hooks,
                                      delhooks=self.del_hooks,
                                      revertqueues=self.revert_queues,
                                      delqueues=self.del_queues,
                                      delscheds=self.del_scheds,
                                      revertresources=self.revert_resources,
                                      server_stat=server_stat)
        rv = self.is_server_licensed(server)
        _msg = 'No license found on server %s' % (server.shortname)
        self.assertTrue(rv, _msg)
        self.logger.info('server: %s licensed', server.hostname)
        server.update_special_attr(SERVER, id=server.hostname)

    def revert_comm(self, comm, force=False):
        """
        Revert the values set for comm
        """
        rv = comm.isUp()
        if not rv:
            self.logger.error('comm ' + comm.hostname + ' is down')
            comm.start()
            msg = 'Failed to restart comm ' + comm.hostname
            self.assertTrue(comm.isUp(), msg)

    def revert_scheduler(self, scheduler, force=False):
        """
        Revert the values set for scheduler
        """
        rv = scheduler.isUp()
        if not rv:
            self.logger.error('scheduler ' + scheduler.hostname + ' is down')
            scheduler.start()
            msg = 'Failed to restart scheduler ' + scheduler.hostname
            self.assertTrue(scheduler.isUp(), msg)
        if ((self.revert_to_defaults and self.sched_revert_to_defaults) or
                force):
            rv = scheduler.revert_to_defaults()
            _msg = 'Failed to revert sched %s' % (scheduler.hostname)
            self.assertTrue(rv, _msg)
        self.server.update_special_attr(SCHED)

    def revert_mom(self, mom, force=False):
        """
        Revert the values set for mom
        :param mom: the MoM object whose values are to be reverted
        :type mom: MoM object
        :param force: Option to reverse forcibly
        :type force: bool
        """
        rv = mom.isUp()
        if not rv:
            self.logger.error('mom ' + mom.hostname + ' is down')
            mom.start()
            msg = 'Failed to restart mom ' + mom.hostname
            self.assertTrue(mom.isUp(), msg)
        restart = False
        enabled_cpuset = False
        if ((self.revert_to_defaults and self.mom_revert_to_defaults and
             mom.revert_to_default) or force):
            # no need to delete vnodes as it is already deleted in
            # server revert_to_defaults
            mom.delete_pelog()
            if mom.has_vnode_defs():
                mom.delete_vnode_defs()
                restart = True
            mom.config = {}
            conf = mom.dflt_config
            if 'clienthost' in self.conf:
                conf.update({'$clienthost': self.conf['clienthost']})
            mom.apply_config(conf=conf, hup=False, restart=False)
            if mom.is_cpuset_mom():
                enabled_cpuset = True
        if restart:
            mom.restart()
        else:
            mom.signal('-HUP')
        if not mom.isUp():
            self.logger.error('mom ' + mom.shortname + ' is down after revert')
        # give mom enough time to network sync with the server on cpuset system
        if enabled_cpuset:
            time.sleep(4)
        a = {'state': 'free'}
        self.server.manager(MGR_CMD_CREATE, NODE, None, mom.shortname,
                            runas=ROOT_USER)
        if enabled_cpuset:
            # In order to avoid intermingling CF/HK/PY file copies from the
            # create node and those caused by the following call, wait
            # until the dialogue between MoM and the server is complete
            time.sleep(4)
            just_before_enable_cgroup_cset = time.time()
            mom.enable_cgroup_cset()
            # a high max_attempts is needed to tolerate delay receiving
            # hook-related files, due to temporary network interruptions
            mom.log_match('pbs_cgroups.CF;copy hook-related '
                          'file request received', max_attempts=120,
                          starttime=just_before_enable_cgroup_cset - 1,
                          interval=1)
            # Make sure that the MoM will generate per-NUMA node vnodes
            # when the natural node was created above.
            # HUP may not be enough if exechost_startup is delayed
            time.sleep(2)
            mom.signal('-HUP')
            self.server.expect(NODE, a, id=mom.shortname + '[0]', interval=1)
        else:
            self.server.expect(NODE, a, id=mom.shortname, interval=1)
            self.server.update_special_attr(NODE, id=mom.shortname)

        return mom

    def analyze_logs(self):
        """
        analyze accounting and scheduler logs from time test was started
        until it finished
        """
        pla = PBSLogAnalyzer()
        self.metrics_data = pla.analyze_logs(serverlog=self.server.logfile,
                                             schedlog=self.scheduler.logfile,
                                             momlog=self.mom.logfile,
                                             acctlog=self.server.acctlogfile,
                                             start=self.server.ctime,
                                             end=time.time())

    def set_test_measurements(self, mdic=None):
        """
        set dictionary of analytical results of the test
        in order to include it in test report

        :param mdic: dictionary with analytical data
        :type mdic: dict

        :returns: True on successful append or False on failure
        """
        if not (mdic and isinstance(mdic, dict)):
            return False
        self.measurements.append(mdic)
        return True

    def add_additional_data_to_report(self, datadic=None):
        """
        set dictionary that will be merged with the test report
        for the overall test run

        :param datadic: dictionary with analytical data
        :type datadic: dict

        :returns: True on succssful update or False on failure
        """
        if not (datadic and isinstance(datadic, dict)):
            return False
        self.additional_data.update(datadic)
        return True

    def start_proc_monitor(self, name=None, regexp=False, frequency=60):
        """
        Start the process monitoring

        :param name: Process name
        :type name: str or None
        :param regexp: Regular expression to match
        :type regexp: bool
        :param frequency: Frequency of monitoring
        :type frequency: int
        """
        if self._process_monitoring:
            self.logger.info('A process monitor is already instantiated')
            return
        self.logger.info('starting process monitoring of ' + name +
                         ' every ' + str(frequency) + 'seconds')
        self._procmon = ProcMonitor(name=name, regexp=regexp,
                                    frequency=frequency)
        self._procmon.start()

    def stop_proc_monitor(self):
        """
        Stop the process monitoring
        """
        if not self._process_monitoring:
            return
        self.logger.info('stopping process monitoring')
        self._procmon.stop()
        self.metrics_data['procs'] = self._procmon.db_proc_info
        self.set_test_measurements(self.metrics_data)
        self._process_monitoring = False

    def skipTest(self, reason=None):
        """
        Skip Test

        :param reason: message to indicate why test is skipped
        :type reason: str or None
        """
        if reason:
            self.logger.warning('test skipped: ' + reason)
        else:
            reason = 'unknown'
        raise SkipTest(reason)

    skip_test = skipTest

    def add_pbs_python_path_to_sys_path(self):
        """
        Add the path to the installed PBS Python modules located in the PBS
        installation directory to the module search path if the path is not
        already present.
        """
        for lib_dir in ['lib64', 'lib']:
            pbs_python_path = os.path.join(
                self.server.pbs_conf['PBS_EXEC'], lib_dir, 'python', 'altair')
            if os.path.isdir(pbs_python_path):
                if pbs_python_path not in sys.path:
                    sys.path.append(pbs_python_path)
                return
        raise Exception(
            "Unable to determine the path to the PBS Python modules in the " +
            "PBS installation directory.")

    @classmethod
    def log_enter_teardown(cls, iscls=False):
        _m = ' Entered ' + cls.__name__ + ' tearDown'
        if iscls:
            _m += 'Class'
        _m_len = len(_m)
        cls.logger.info('=' * _m_len)
        cls.logger.info(_m)
        cls.logger.info('=' * _m_len)

    @classmethod
    def log_end_teardown(cls, iscls=False):
        _m = 'Completed ' + cls.__name__ + ' tearDown'
        if iscls:
            _m += 'Class'
        _m_len = len(_m)
        cls.logger.info('=' * _m_len)
        cls.logger.info(_m)
        cls.logger.info('=' * _m_len)

    @staticmethod
    def delete_current_state(svr, moms):
        """
        Delete nodes, queues, site hooks, reservations and
        vnodedef file
        """
        # unset server attributes
        svr.unset_svr_attrib()
        # Delete site hooks
        svr.delete_site_hooks()
        # cleanup reservations
        svr.cleanup_reservations()
        # Delete vnodedef file & vnodes
        for m in moms:
            # Check if vnodedef file is present
            if moms[m].has_vnode_defs():
                moms[m].delete_vnode_defs()
                moms[m].delete_vnodes()
                moms[m].restart()
        # Delete nodes
        svr.delete_nodes()
        # Delete queues
        svr.delete_queues()
        # Delete resources
        svr.delete_resources()

    def tearDown(self):
        """
        verify that ``server`` and ``scheduler`` are up
        clean up jobs and reservations
        """
        if self.conf:
            self.set_test_measurements({'testconfig': self.testconf})
        if 'skip-teardown' in self.conf:
            return
        self.log_enter_teardown()
        self.server.cleanup_jobs()
        self.stop_proc_monitor()

        for server in self.servers.values():
            server.cleanup_files()

        for mom in self.moms.values():
            mom.cleanup_files()

        for sched in self.scheds:
            self.scheds[sched].cleanup_files()
        self.server.delete_sched_config()
        if self.use_cur_setup:
            self.delete_current_state(self.server, self.moms)
            ret = self.server.load_configuration(self.saved_file)
            if not ret:
                raise Exception("Failed to load server's test setup")
            ret = self.scheduler.load_configuration(self.saved_file)
            if not ret:
                raise Exception("Failed to load scheduler's test setup")
            for mom in self.moms.values():
                ret = mom.load_configuration(self.saved_file)
                if not ret:
                    raise Exception("Failed to load mom's test setup")
            self.du.rm(path=self.saved_file)
        self.log_end_teardown()

    @classmethod
    def tearDownClass(cls):
        cls._testMethodName = 'tearDownClass'
        if cls.use_cur_setup:
            PBSTestSuite.delete_current_state(cls.server, cls.moms)
            PBSTestSuite.config_saved = False
            ret = cls.server.load_configuration(cls.saved_file)
            if not ret:
                raise Exception("Failed to load server's custom setup")
            ret = cls.scheduler.load_configuration(cls.saved_file)
            if not ret:
                raise Exception("Failed to load scheduler's custom setup")
            for mom in cls.moms.values():
                ret = mom.load_configuration(cls.saved_file)
                if not ret:
                    raise Exception("Failed to load mom's custom setup")
            cls.du.rm(path=cls.saved_file)
