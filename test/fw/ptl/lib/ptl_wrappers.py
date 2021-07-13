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


import ast
import base64
import collections
import copy
import datetime
import grp
import json
import logging
import os
import pickle
import pwd
import random
import re
import socket
import string
import sys
import tempfile
import threading
import time
import traceback
from collections import OrderedDict
from distutils.version import LooseVersion
from operator import itemgetter

from ptl.lib.pbs_api_to_cli import api_to_cli
from ptl.lib.ptl_batchutils import BatchUtils
from ptl.utils.pbs_cliutils import CliUtils
from ptl.utils.pbs_dshutils import DshUtils, PtlUtilError, get_method_name
from ptl.utils.pbs_procutils import ProcUtils
from ptl.utils.pbs_testusers import (ROOT_USER, TEST_USER, PbsUser,
                                     DAEMON_SERVICE_USER)

try:
    import psycopg2
    PSYCOPG = True
except Exception:
    PSYCOPG = False
from ptl.lib.ptl_error import (PbsStatusError, PbsSubmitError,
                               PbsDeljobError, PbsDelresvError,
                               PbsDeleteError, PbsSelectError,
                               PbsManagerError, PbsSignalError,
                               PbsAlterError, PbsHoldError,
                               PbsRerunError, PbsOrderError,
                               PbsRunError, PbsMoveError,
                               PbsQtermError, PbsQdisableError,
                               PbsQenableError, PbsQstartError,
                               PbsQstopError, PbsResourceError,
                               PbsResvAlterError, PtlExpectError,
                               PbsConnectError, PbsServiceError,
                               PbsInitServicesError, PbsMessageError,
                               PtlLogMatchError)
from ptl.lib.ptl_types import PbsAttribute
from ptl.lib.ptl_constants import *
from ptl.lib.ptl_entities import (Hook, Queue, Entity, Limit,
                                  EquivClass, Resource)
from ptl.lib.ptl_resourceresv import Job, Reservation, InteractiveJob
from ptl.lib.ptl_sched import Scheduler
from ptl.lib.ptl_mom import MoM, get_mom_obj
from ptl.lib.ptl_service import PBSService, PBSInitServices
from ptl.lib.ptl_expect_action import ExpectActions
try:
    from nose.plugins.skip import SkipTest
except ImportError:
    class SkipTest(Exception):
        pass


class Wrappers(PBSService):
    dflt_attributes = {
        ATTR_dfltque: "workq",
        ATTR_nodefailrq: "310",
        ATTR_FlatUID: 'True',
        ATTR_DefaultChunk + ".ncpus": "1",
    }

    dflt_sched_name = 'default'

    # this pattern is a bit relaxed to match common developer build numbers
    version_tag = re.compile(r"[a-zA-Z_]*(?P<version>[\d\.]+.[\w\d\.]*)[\s]*")

    actions = ExpectActions()

    # these server attributes revert back to default value when unset
    __special_attr_keys = {SERVER: [ATTR_scheduling, ATTR_logevents,
                                    ATTR_mailfrom, ATTR_queryother,
                                    ATTR_rescdflt + '.ncpus', ATTR_schedit,
                                    ATTR_ResvEnable, ATTR_maxarraysize,
                                    ATTR_license_min, ATTR_license_max,
                                    ATTR_license_linger,
                                    ATTR_EligibleTimeEnable,
                                    ATTR_max_concurrent_prov],
                           SCHED: [ATTR_sched_cycle_len, ATTR_scheduling,
                                   ATTR_schedit, ATTR_logevents,
                                   ATTR_sched_server_dyn_res_alarm,
                                   ATTR_SchedHost,
                                   'preempt_prio', 'preempt_queue_prio',
                                   'throughput_mode', 'job_run_wait',
                                   'partition', 'sched_priv', 'sched_log'],
                           NODE: [ATTR_rescavail + '.ncpus'],
                           HOOK: [ATTR_HOOK_type,
                                  ATTR_HOOK_enable,
                                  ATTR_HOOK_event,
                                  ATTR_HOOK_alarm,
                                  ATTR_HOOK_order,
                                  ATTR_HOOK_debug,
                                  ATTR_HOOK_fail_action,
                                  ATTR_HOOK_user]}

    __special_attr = {SERVER: None,
                      SCHED: None,
                      NODE: None,
                      HOOK: None}

    def __init__(self, name=None, attrs={}, defaults={}, pbsconf_file=None,
                 snapmap={}, snap=None, client=None, client_pbsconf_file=None,
                 db_access=None, stat=True):
        self.jobs = {}
        self.nodes = {}
        self.reservations = {}
        self.queues = {}
        self.resources = {}
        self.hooks = {}
        self.pbshooks = {}
        self.entities = {}
        self.schedulers = {}
        self.version = None
        self.default_queue = None
        self.last_error = []  # type: array. Set for CLI IFL errors. Not reset
        self.last_out = []  # type: array. Set for CLI IFL output. Not reset
        self.last_rc = None  # Set for CLI IFL return code. Not thread-safe
        self.moms = {}

        # default timeout on connect/disconnect set to 60s to mimick the qsub
        # buffer introduced in PBS 11
        self._conn_timeout = 60
        self._conn_timer = None
        self._conn = None
        self._db_conn = None
        self.current_user = pwd.getpwuid(os.getuid())[0]

        if len(defaults.keys()) == 0:
            defaults = self.dflt_attributes

        self.pexpect_timeout = 15
        self.pexpect_sleep_time = .1
        super().__init__(name, attrs, defaults, pbsconf_file, snapmap,
                         snap)
        _m = ['server ', self.shortname]
        if pbsconf_file is not None:
            _m += ['@', pbsconf_file]
        _m += [': ']
        self.logprefix = "".join(_m)
        self.pi = PBSInitServices(hostname=self.hostname,
                                  conf=self.pbs_conf_file)
        self.set_client(client)

        if client_pbsconf_file is None:
            self.client_pbs_conf_file = self.du.get_pbs_conf_file(self.client)
        else:
            self.client_pbs_conf_file = client_pbsconf_file

        self.client_conf = self.du.parse_pbs_config(
            self.client, file=self.client_pbs_conf_file)

        if self.client_pbs_conf_file == '/etc/pbs.conf':
            self.default_client_pbs_conf = True
        elif (('PBS_CONF_FILE' not in os.environ) or
              (os.environ['PBS_CONF_FILE'] != self.client_pbs_conf_file)):
            self.default_client_pbs_conf = False
        else:
            self.default_client_pbs_conf = True

        a = {}
        if os.getuid() == 0:
            a = {ATTR_aclroot: 'root'}
        self.dflt_attributes.update(a)

        if not API_OK:
            # mode must be set before the first stat call
            self.set_op_mode(PTL_CLI)

        if stat:
            try:
                tmp_attrs = self.status(SERVER, level=logging.DEBUG,
                                        db_access=db_access)
            except (PbsConnectError, PbsStatusError):
                tmp_attrs = None

            if tmp_attrs is not None and len(tmp_attrs) > 0:
                self.attributes = tmp_attrs[0]

            if ATTR_dfltque in self.attributes:
                self.default_queue = self.attributes[ATTR_dfltque]

            self.update_version_info()

    def update_version_info(self):
        """
        Update the version information.
        """
        if ATTR_version not in self.attributes:
            self.attributes[ATTR_version] = 'unknown'
        else:
            m = self.version_tag.match(self.attributes[ATTR_version])
            if m:
                v = m.group('version')
                self.version = LooseVersion(v)
        self.logger.info(self.logprefix + 'version ' +
                         self.attributes[ATTR_version])

    def set_client(self, name=None):
        """
        Set server client

        :param name: Client name
        :type name: str
        """
        if name is None:
            self.client = socket.gethostname()
        else:
            self.client = name

    def get_op_mode(self):
        """
        Returns operating mode for calls to the PBS server.
        Currently, two modes are supported, either the ``API``
        or the ``CLI``. Default is ``API``
        """
        if (not API_OK or (self.ptl_conf['mode'] == PTL_CLI)):
            return PTL_CLI
        return PTL_API

    def set_connect_timeout(self, timeout=0):
        """
        Set server connection timeout
        :param timeout: Timeout value
        :type timeout: int
        """
        self._conn_timeout = timeout

    def set_op_mode(self, mode):
        """
        set operating mode to one of either ``PTL_CLI`` or
        ``PTL_API``.Returns the mode that was set which can
        be different from the value requested, for example, if
        requesting to set ``PTL_API``, in the absence of the
        appropriate SWIG wrappers, the library will fall back to
        ``CLI``, or if requesting ``PTL_CLI`` and there is no
        ``PBS_EXEC`` on the system, None is returned.
        :param mode: Operating mode
        :type mode: str
        """
        if mode == PTL_API:
            if self._conn is not None or self._conn < 0:
                self._conn = None
            if not API_OK:
                self.logger.error(self.logprefix +
                                  'API submission is not available')
                return PTL_CLI
        elif mode == PTL_CLI:
            if ((not self.has_snap) and
                not os.path.isdir(os.path.join(self.client_conf['PBS_EXEC'],
                                               'bin'))):
                self.logger.error(self.logprefix +
                                  'PBS commands are not available')
                return None
        else:
            self.logger.error(self.logprefix + "Unrecognized operating mode")
            return None

        self.ptl_conf['mode'] = mode
        self.logger.info(self.logprefix + 'server operating mode set to ' +
                         mode)
        return mode

    def update_special_attr(self, obj_type, id=None):
        """
        Update special attributes(__special_attr) dictionary
        :param obj_type: The type of object to update attribute values
                         in special attribute dictionary.
        :type obj_type: str
        :param id: The id of the object to act upon
        :type id: str
        """
        if not id:
            if obj_type in (SERVER, NODE):
                id = self.hostname
            elif obj_type == SCHED:
                id = 'default'
        id_attr_dict = {}
        obj_stat = self.status(obj_type, id=id)[0]
        for key in obj_stat.keys():
            if key in self.__special_attr_keys[obj_type]:
                id_attr_dict[key] = obj_stat[key]

        id_attr = {id: id_attr_dict}
        self.__special_attr[obj_type] = id_attr

    def get_special_attr_val(self, obj_type, attr, id=None):
        """
        Get value for given attribute from
        special attributes(__special_attr) dictionary
        :param obj_type: The type of object to update attribute values
                         in special attribute dictionary.
        :type obj_type: str
        :param attr: The attribute for which value requested.
        :type id: str
        :param id: The id of the object to act upon
        :type id: str
        """

        if not id:
            if obj_type in (SERVER, NODE):
                id = self.hostname
            elif obj_type == SCHED:
                id = 'default'
        res_val = ATTR_rescavail + '.ncpus'
        if obj_type in (NODE, VNODE) and attr == res_val:
            obj_stat = self.status(obj_type, id=id)[0]
            if 'pcpus' not in obj_stat.keys():
                return 1
            else:
                return self.__special_attr[obj_type][id][attr]
        elif obj_type == HOOK and (id == 'pbs_cgroups' and attr == 'freq'):
            return 120
        else:
            return self.__special_attr[obj_type][id][attr]

    def _filter(self, obj_type=None, attrib=None, id=None, extend=None,
                op=None, attrop=None, bslist=None, mode=PTL_COUNTER,
                idonly=True, grandtotal=False, db_access=None, runas=None,
                resolve_indirectness=False, level=logging.DEBUG):

        if bslist is None:
            try:
                _a = resolve_indirectness
                tmp_bsl = self.status(obj_type, attrib, id,
                                      level=level, extend=extend,
                                      db_access=db_access, runas=runas,
                                      resolve_indirectness=_a)
                del _a
            except PbsStatusError:
                return None

            bslist = self.utils.filter_batch_status(tmp_bsl, attrib)
            del tmp_bsl

        if bslist is None:
            return None

        if isinstance(attrib, str):
            attrib = attrib.split(',')

        total = {}
        for bs in bslist:
            if isinstance(attrib, list):
                # when filtering on multiple values, ensure that they are
                # all present on the object, otherwise skip
                if attrop == PTL_AND:
                    match = True
                    for k in attrib:
                        if k not in bs:
                            match = False
                    if not match:
                        continue

                for a in attrib:
                    if a in bs:
                        if op == SET:
                            k = a
                        else:
                            # Since this is a list of attributes, no operator
                            # was provided so we settle on "equal"
                            k = a + '=' + str(bs[a])
                        if mode == PTL_COUNTER:
                            amt = 1
                            if grandtotal:
                                amt = PbsAttribute.decode_value(bs[a])
                                if not isinstance(amt, (int, float)):
                                    amt = 1
                                if a in total:
                                    total[a] += amt
                                else:
                                    total[a] = amt
                            else:
                                if k in total:
                                    total[k] += amt
                                else:
                                    total[k] = amt
                        elif mode == PTL_FILTER:
                            if k in total:
                                if idonly:
                                    total[k].append(bs['id'])
                                else:
                                    total[k].append(bs)
                            else:
                                if idonly:
                                    total[k] = [bs['id']]
                                else:
                                    total[k] = [bs]
                        else:
                            self.logger.error("Unhandled mode " + str(mode))
                            return None

            elif isinstance(attrib, dict):
                tmptotal = {}  # The running count that will be used for total

                # when filtering on multiple values, ensure that they are
                # all present on the object, otherwise skip
                match = True
                for k, v in attrib.items():
                    if k not in bs:
                        match = False
                        if attrop == PTL_AND:
                            break
                        else:
                            continue
                    amt = PbsAttribute.decode_value(bs[k])
                    if isinstance(v, tuple):
                        op = v[0]
                        val = PbsAttribute.decode_value(v[1])
                    elif op == SET:
                        val = None
                        pass
                    else:
                        op = EQ
                        val = PbsAttribute.decode_value(v)

                    if ((op == LT and amt < val) or
                            (op == LE and amt <= val) or
                            (op == EQ and amt == val) or
                            (op == GE and amt >= val) or
                            (op == GT and amt > val) or
                            (op == NE and amt != val) or
                            (op == MATCH and str(amt).find(str(val)) != -1) or
                            (op == MATCH_RE and
                             re.search(str(val), str(amt))) or
                            (op == SET)):
                        # There is a match, proceed to track the attribute
                        self._filter_helper(bs, k, val, amt, op, mode,
                                            tmptotal, idonly, grandtotal)
                    elif attrop == PTL_AND:
                        match = False
                        if mode == PTL_COUNTER:
                            # requesting specific key/value pairs should result
                            # in 0 available elements
                            tmptotal[str(k) + PTL_OP_TO_STR[op] + str(val)] = 0
                        break
                    elif mode == PTL_COUNTER:
                        tmptotal[str(k) + PTL_OP_TO_STR[op] + str(val)] = 0

                if attrop != PTL_AND or (attrop == PTL_AND and match):
                    for k, v in tmptotal.items():
                        if k not in total:
                            total[k] = v
                        else:
                            total[k] += v
        return total

    def _filter_helper(self, bs, k, v, amt, op, mode, total, idonly,
                       grandtotal):
        # default operation to '='
        if op is None or op not in PTL_OP_TO_STR:
            op = '='
        op_str = PTL_OP_TO_STR[op]

        if op == SET:
            # override PTL_OP_TO_STR fro SET operations
            op_str = ''
            v = ''

        ky = k + op_str + str(v)
        if mode == PTL_COUNTER:
            incr = 1
            if grandtotal:
                if not isinstance(amt, (int, float)):
                    incr = 1
                else:
                    incr = amt
            if ky in total:
                total[ky] += incr
            else:
                total[ky] = incr
        elif mode == PTL_FILTER:
            if ky in total:
                if idonly:
                    total[ky].append(bs['id'])
                else:
                    total[ky].append(bs)
            else:
                if idonly:
                    total[ky] = [bs['id']]
                else:
                    total[ky] = [bs]

    def _connect(self, hostname, attempt=1):
        if ((self._conn is None or self._conn < 0) or
                (self._conn_timeout == 0 or self._conn_timer is None)):
            self._conn = pbs_connect(hostname)
            self._conn_timer = time.time()

        if self._conn is None or self._conn < 0:
            if attempt > 5:
                m = self.logprefix + 'unable to connect'
                raise PbsConnectError(rv=None, rc=-1, msg=m)
            else:
                self._disconnect(self._conn, force=True)
                time.sleep(1)
                return self._connect(hostname, attempt + 1)

        return self._conn

    def _disconnect(self, conn, force=False):
        """
        disconnect a connection to a Server.
        For performance of the API calls, a connection is
        maintained up to _conn_timer, unless the force parameter
        is set to True
        :param conn: Server connection
        :param force: If true then diconnect forcefully
        :type force: bool
        """
        if ((conn is not None and conn >= 0) and
            (force or
             (self._conn_timeout == 0 or
              (self._conn_timer is not None and
               (time.time() - self._conn_timer > self._conn_timeout))))):
            pbs_disconnect(conn)
            self._conn_timer = None
            self._conn = None

    def update_attributes(self, obj_type, bs, overwrite=False):
        """
        Populate objects from batch status data
        """
        if bs is None:
            return

        for binfo in bs:
            if 'id' not in binfo:
                continue
            id = binfo['id']
            obj = None
            if obj_type == JOB:
                if ATTR_owner in binfo:
                    user = binfo[ATTR_owner].split('@')[0]
                else:
                    user = None
                if id in self.jobs:
                    if overwrite:
                        self.jobs[id].attributes = copy.deepcopy(binfo)
                    else:
                        self.jobs[id].attributes.update(binfo)
                    if self.jobs[id].username != user:
                        self.jobs[id].username = user
                else:
                    self.jobs[id] = Job(user, binfo)
                obj = self.jobs[id]
            elif obj_type in (VNODE, NODE):
                if id in self.nodes:
                    if overwrite:
                        self.nodes[id].attributes = copy.deepcopy(binfo)
                    else:
                        self.nodes[id].attributes.update(binfo)
                else:
                    if 'Mom' in binfo:
                        self.nodes[id] = get_mom_obj(self, binfo['Mom'], binfo,
                                                     snapmap={NODE: None})
                    else:
                        self.nodes[id] = get_mom_obj(self, id, binfo,
                                                     snapmap={NODE: None})
                obj = self.nodes[id]
            elif obj_type == SERVER:
                if overwrite:
                    self.attributes = copy.deepcopy(binfo)
                else:
                    self.attributes.update(binfo)
                obj = self
            elif obj_type == QUEUE:
                if id in self.queues:
                    if overwrite:
                        self.queues[id].attributes = copy.deepcopy(binfo)
                    else:
                        self.queues[id].attributes.update(binfo)
                else:
                    self.queues[id] = Queue(id, binfo, server=self)
                obj = self.queues[id]
            elif obj_type == RESV:
                if id in self.reservations:
                    if overwrite:
                        self.reservations[id].attributes = copy.deepcopy(binfo)
                    else:
                        self.reservations[id].attributes.update(binfo)
                else:
                    self.reservations[id] = Reservation(id, binfo)
                obj = self.reservations[id]
            elif obj_type == HOOK:
                if id in self.hooks:
                    if overwrite:
                        self.hooks[id].attributes = copy.deepcopy(binfo)
                    else:
                        self.hooks[id].attributes.update(binfo)
                else:
                    self.hooks[id] = Hook(id, binfo, server=self)
                obj = self.hooks[id]
            elif obj_type == PBS_HOOK:
                if id in self.pbshooks:
                    if overwrite:
                        self.pbshooks[id].attributes = copy.deepcopy(binfo)
                    else:
                        self.pbshooks[id].attributes.update(binfo)
                else:
                    self.pbshooks[id] = Hook(id, binfo, server=self)
                obj = self.pbshooks[id]
            elif obj_type == SCHED:
                if id in self.schedulers:
                    if overwrite:
                        self.schedulers[id].attributes = copy.deepcopy(binfo)
                    else:
                        self.schedulers[id].attributes.update(binfo)
                    if 'sched_priv' in binfo:
                        self.schedulers[id].setup_sched_priv(
                            binfo['sched_priv'])
                else:
                    if 'sched_host' not in binfo:
                        hostname = self.hostname
                    else:
                        hostname = binfo['sched_host']
                    if SCHED in self.snapmap:
                        snap = self.snap
                        snapmap = self.snapmap
                    else:
                        snap = None
                        snapmap = {}
                    spriv = None
                    if 'sched_priv' in binfo:
                        spriv = binfo['sched_priv']
                    self.schedulers[id] = Scheduler(server=self,
                                                    hostname=hostname,
                                                    snap=snap,
                                                    snapmap=snapmap,
                                                    id=id,
                                                    sched_priv=spriv)
                    if overwrite:
                        self.schedulers[id].attributes = copy.deepcopy(binfo)
                    else:
                        self.schedulers[id].attributes.update(binfo)
                obj = self.schedulers[id]

            elif obj_type == RSC:
                if id in self.resources:
                    if overwrite:
                        self.resources[id].attributes = copy.deepcopy(binfo)
                    else:
                        self.resources[id].attributes.update(binfo)
                else:
                    rtype = None
                    rflag = None
                    if 'type' in binfo:
                        rtype = binfo['type']
                    if 'flag' in binfo:
                        rflag = binfo['flag']
                    self.resources[id] = Resource(id, rtype, rflag)

            if obj is not None:
                self.utils.update_attributes_list(obj)
                obj.__dict__.update(binfo)

    def pbs_api_as(self, cmd=None, obj=None, user=None, **kwargs):
        """
        Generic handler to run an ``API`` call impersonating
        a given user.This method is only used for impersonation
        over the ``API`` because ``CLI`` impersonation takes place
        through the generic ``DshUtils`` run_cmd mechanism.
        :param cmd: PBS command
        :type cmd: str or None
        :param user: PBS user or current user
        :type user: str or None
        :raises: eval
        """
        fn = None
        objid = None
        _data = None

        if user is None:
            user = self.du.get_current_user()
        else:
            # user may be a PbsUser object, cast it to string for the remainder
            # of the function
            user = str(user)

        if cmd == 'submit':
            if obj is None:
                return None

            _data = copy.copy(obj)
            # the following attributes cause problems 'pickling',
            # since they are not needed we unset them
            _data.attrl = None
            _data.attropl = None
            _data.logger = None
            _data.utils = None

        elif cmd in ('alterjob', 'holdjob', 'sigjob', 'msgjob', 'rlsjob',
                     'rerunjob', 'orderjob', 'runjob', 'movejob',
                     'select', 'delete', 'status', 'manager', 'terminate',
                     'deljob', 'delresv', 'alterresv'):
            objid = obj
            if 'data' in kwargs:
                _data = kwargs['data']

        if _data is not None:
            fn = self.du.create_temp_file()
            with open(fn, 'w+b') as tmpfile:
                pickle.dump(_data, tmpfile)

            os.chmod(fn, 0o755)

            if self._is_local:
                os.chdir(tempfile.gettempdir())
            else:
                self.du.run_copy(self.hostname, src=fn, dest=fn)

        if not self._is_local:
            p_env = '"import os; print(os.environ[\'PTL_EXEC\'])"'
            ret = self.du.run_cmd(self.hostname, ['python3', '-c', p_env],
                                  logerr=False)
            if ret['out']:
                runcmd = [os.path.join(ret['out'][0], 'pbs_as')]
            else:
                runcmd = ['pbs_as']
        elif 'PTL_EXEC' in os.environ:
            runcmd = [os.path.join(os.environ['PTL_EXEC'], 'pbs_as')]
        else:
            runcmd = ['pbs_as']

        runcmd += ['-c', cmd, '-u', user]

        if objid is not None:
            runcmd += ['-o']
            if isinstance(objid, list):
                runcmd += [','.join(objid)]
            else:
                runcmd += [objid]

        if fn is not None:
            runcmd += ['-f', fn]

        if 'hostname' in kwargs:
            hostname = kwargs['hostname']
        else:
            hostname = self.hostname
        runcmd += ['-s', hostname]

        if 'extend' in kwargs and kwargs['extend'] is not None:
            runcmd += ['-e', kwargs['extend']]

        ret = self.du.run_cmd(self.hostname, runcmd, logerr=False, runas=user)
        out = ret['out']
        if ret['err']:
            if cmd in CMD_ERROR_MAP:
                m = CMD_ERROR_MAP[cmd]
                if m in ret['err'][0]:
                    if fn is not None:
                        os.remove(fn)
                        if not self._is_local:
                            self.du.rm(self.hostname, fn)
                    raise eval(str(ret['err'][0]))
            self.logger.debug("<" + get_method_name(self) + '>err: ' +
                              str(ret['err']))

        if fn is not None:
            os.remove(fn)
            if not self._is_local:
                self.du.rm(self.hostname, fn)

        if cmd == 'submit':
            if out:
                return out[0].strip()
            else:
                return None
        elif cmd in ('alterjob', 'holdjob', 'sigjob', 'msgjob', 'rlsjob',
                     'rerunjob', 'orderjob', 'runjob', 'movejob', 'delete',
                     'terminate', 'alterresv'):
            if ret['out']:
                return int(ret['out'][0])
            else:
                return 1

        elif cmd in ('manager', 'select', 'status'):
            return eval(out[0])

    def logit(self, msg, obj_type, attrib, id, level=logging.INFO):
        """
        Generic logging routine for ``IFL`` commands
        :param msg: The message to log
        :type msg: str
        :param obj_type: object type, i.e *
        :param attrib: attributes to log
        :param id: name of object to log
        :type id: str or list
        :param level: log level, defaults to ``INFO``
        """
        s = []
        if self.logger is not None:
            if obj_type is None:
                obj_type = MGR_OBJ_NONE
            s = [msg + PBS_OBJ_MAP[obj_type]]
            if id:
                if isinstance(id, list):
                    s += [' ' + ",".join(id)]
                else:
                    s += [' ' + str(id)]
            if attrib:
                s += [' ' + str(attrib)]
            self.logger.log(level, "".join(s))

    def status(self, obj_type=SERVER, attrib=None, id=None,
               extend=None, level=logging.INFO, db_access=None, runas=None,
               resolve_indirectness=False, logerr=True):
        """
        Stat any PBS object ``[queue, server, node, hook, job,
        resv, sched]``.If the Server is setup from snap input,
        see snap or snapmap member, the status calls are routed
        directly to the data on files from snap.
        The server can be queried either through the 'qstat'
        command line tool or through the wrapped PBS IFL api,
        see set_op_mode.
        Return a dictionary representation of a batch status object
        raises ``PbsStatsuError on error``.
        :param obj_type: The type of object to query, one of the *
                         objects.Default: SERVER
        :param attrib: Attributes to query, can be a string, a
                       list, a dictionary.Default is to query all
                       attributes.
        :type attrib: str or list or dictionary
        :param id: An optional id, the name of the object to status
        :type id: str
        :param extend: Optional extension to the IFL call
        :param level: The logging level, defaults to INFO
        :type level: str
        :param db_acccess: set to either file containing credentials
                           to DB access or dictionary containing
                           ``{'dbname':...,'user':...,'port':...}``
        :type db_access: str or dictionary
        :param runas: run stat as user
        :type runas: str
        :param resolve_indirectness: If True resolves indirect node
                                     resources values
        :type resolve_indirectness: bool
        :param logerr: If True (default) logs run_cmd errors
        :type logerr: bool
        In addition to standard IFL stat call, this wrapper handles
        a few cases that aren't implicitly offered by pbs_stat*,
        those are for Hooks,Resources, and a formula evaluation.
        """

        prefix = 'status on ' + self.shortname
        if runas:
            prefix += ' as ' + str(runas)
        prefix += ': '
        self.logit(prefix, obj_type, attrib, id, level)

        bs = None
        bsl = []
        freebs = False
        # 2 - Special handling for gathering the job formula value.
        if attrib is not None and PTL_FORMULA in attrib:
            if (((isinstance(attrib, list) or isinstance(attrib, dict)) and
                 (len(attrib) == 1)) or
                    (isinstance(attrib, str) and len(attrib.split(',')) == 1)):
                bsl = self.status(
                    JOB, 'Resource_List.select', id=id, extend='t')
            if self.schedulers[self.dflt_sched_name] is None:
                self.schedulers[self.dflt_sched_name] = Scheduler(
                    self, name=self.hostname)
            _prev_events = self.status(SCHED, 'log_events',
                                       id=self.dflt_schd_name)[0]['log_events']

            # Job sort formula events are logged at DEBUG2 (256)
            if not int(_prev_events) & 256:
                self.manager(MGR_CMD_SET, SCHED, {'log_events': 2047},
                             id=self.dflt_schd_name)
            self.manager(MGR_CMD_SET, SERVER, {'scheduling': 'True'})
            if id is None:
                _formulas = self.schedulers[self.dflt_sched_name].job_formula()
            else:
                _formulas = {
                    id: self.schedulers[
                        self.dflt_sched_name].job_formula(
                        jobid=id)
                }
            if not int(_prev_filter) & 256:
                self.manager(MGR_CMD_SET, SCHED, {'log_events': _prev_events},
                             id=self.dflt_schd_name)
            if len(bsl) == 0:
                bsl = [{'id': id}]
            for _b in bsl:
                if _b['id'] in _formulas:
                    _b[PTL_FORMULA] = _formulas[_b['id']]
            return bsl

        # 3- Serve data from database if requested... and available for the
        # given object type
        if db_access and obj_type in (SERVER, SCHED, NODE, QUEUE, RESV, JOB):
            bsl = self.status_db(obj_type, attrib, id, db_access=db_access,
                                 logerr=logerr)

        # 4- Serve data from snap files
        elif obj_type in self.snapmap:
            if obj_type in (HOOK, PBS_HOOK):
                for f in self.snapmap[obj_type]:
                    _b = self.utils.file_to_dictlist(f, attrib)
                    if _b and 'hook_name' in _b[0]:
                        _b[0]['id'] = _b[0]['hook_name']
                    else:
                        _b[0]['id'] = os.path.basename(f)
                    if id is None or id == _b[0]['id']:
                        bsl.extend(_b)
            else:
                bsl = self.utils.file_to_dictlist(self.snapmap[obj_type],
                                                  attrib, id=id)
        # 6- Stat using PBS CLI commands
        elif self.get_op_mode() == PTL_CLI:
            tgt = self.client
            if obj_type in (JOB, QUEUE, SERVER):
                pcmd = [os.path.join(
                        self.client_conf['PBS_EXEC'],
                        'bin',
                        'qstat')]

                if extend:
                    pcmd += ['-' + extend]

                if obj_type == JOB:
                    pcmd += ['-f']
                    if id:
                        pcmd += [id]
                    else:
                        pcmd += ['@' + self.hostname]
                elif obj_type == QUEUE:
                    pcmd += ['-Qf']
                    if id:
                        if '@' not in id:
                            pcmd += [id + '@' + self.hostname]
                        else:
                            pcmd += [id]
                    else:
                        pcmd += ['@' + self.hostname]
                elif obj_type == SERVER:
                    pcmd += ['-Bf', self.hostname]

            elif obj_type in (NODE, VNODE, HOST):
                pcmd = [os.path.join(self.client_conf['PBS_EXEC'], 'bin',
                                     'pbsnodes')]
                pcmd += ['-s', self.hostname]
                if obj_type in (NODE, VNODE):
                    pcmd += ['-v']
                if obj_type == HOST:
                    pcmd += ['-H']
                if id:
                    pcmd += [id]
                else:
                    pcmd += ['-a']
            elif obj_type == RESV:
                pcmd = [os.path.join(self.client_conf['PBS_EXEC'], 'bin',
                                     'pbs_rstat')]
                pcmd += ['-f']
                if id:
                    pcmd += [id]
            elif obj_type in (SCHED, PBS_HOOK, HOOK, RSC):
                try:
                    rc = self.manager(MGR_CMD_LIST, obj_type, attrib, id,
                                      runas=runas, level=level, logerr=logerr)
                except PbsManagerError as e:
                    rc = e.rc
                    # PBS bug, no hooks yields a return code of 1, we ignore
                    if obj_type != HOOK:
                        raise PbsStatusError(
                            rc=rc, rv=[], msg=self.geterrmsg())
                if rc == 0:
                    if obj_type == HOOK:
                        o = self.hooks
                    elif obj_type == PBS_HOOK:
                        o = self.pbshooks
                    elif obj_type == SCHED:
                        o = self.schedulers
                    elif obj_type == RSC:
                        o = self.resources
                    if id:
                        if id in o:
                            return [o[id].attributes]
                        else:
                            return None
                    return [h.attributes for h in o.values()]
                return []

            else:
                self.logger.error(self.logprefix + "unrecognized object type")
                raise PbsStatusError(rc=-1, rv=[],
                                     msg="unrecognized object type")
                return None

            # as_script is used to circumvent some shells that will not pass
            # along environment variables when invoking a command through sudo
            if not self.default_client_pbs_conf:
                pcmd = ['PBS_CONF_FILE=' + self.client_pbs_conf_file] + pcmd
                as_script = True
            elif obj_type == RESV and not self._is_local:
                pcmd = ['PBS_SERVER=' + self.hostname] + pcmd
                as_script = True
            else:
                as_script = False

            ret = self.du.run_cmd(tgt, pcmd, runas=runas, as_script=as_script,
                                  level=logging.INFOCLI, logerr=logerr)
            o = ret['out']
            if ret['err'] != ['']:
                self.last_error = ret['err']
            self.last_rc = ret['rc']
            if ret['rc'] != 0:
                raise PbsStatusError(rc=ret['rc'], rv=[], msg=self.geterrmsg())

            bsl = self.utils.convert_to_dictlist(
                o, attrib, mergelines=True, obj_type=obj_type)

        # 7- Stat with impersonation over PBS IFL swig-wrapped API
        elif runas is not None:
            _data = {'obj_type': obj_type, 'attrib': attrib, 'id': id}
            bsl = self.pbs_api_as('status', user=runas, data=_data,
                                  extend=extend)
        else:
            # 8- Stat over PBS IFL API
            #
            # resources are special attributes, all resources are queried as
            # a single attribute.
            # e.g. querying the resources_available attribute returns all
            # resources such as ncpus, mem etc. when querying for
            # resources_available.ncpus and resources_available.mem only query
            # resources_available once and retrieve the resources desired from
            # there
            if isinstance(attrib, dict):
                attribcopy = {}
                restype = []
                for k, v in attrib.items():
                    if isinstance(v, tuple):
                        # SET requires a special handling because status may
                        # have been called through counter to count the number
                        # of objects have a given attribute set, in this case
                        # we set the attribute to an empty string rather than
                        # the number of elements requested. This is a
                        # side-effect of the way pbs_statjob works
                        if v[0] in (SET, MATCH_RE):
                            v = ''
                        else:
                            v = v[1]
                    if isinstance(v, collections.Callable):
                        v = ''
                    if '.' in k:
                        _r = k.split('.')[0]
                        if _r not in restype:
                            attribcopy[k] = v
                            restype.append(_r)
                    else:
                        attribcopy[k] = v
            elif isinstance(attrib, list):
                attribcopy = []
                for k in attrib:
                    if '.' in k:
                        _found = False
                        for _e in attribcopy:
                            _r = k.split('.')[0]
                            if _r == _e.split('.')[0]:
                                _found = True
                                break
                        if not _found:
                            attribcopy.append(k)
                    else:
                        attribcopy.append(k)
            else:
                attribcopy = attrib

            a = self.utils.convert_to_attrl(attribcopy)
            c = self._connect(self.hostname)

            if obj_type == JOB:
                bs = pbs_statjob(c, id, a, extend)
            elif obj_type == QUEUE:
                bs = pbs_statque(c, id, a, extend)
            elif obj_type == SERVER:
                bs = pbs_statserver(c, a, extend)
            elif obj_type == HOST:
                bs = pbs_statnode(c, id, a, extend)
            elif obj_type == VNODE:
                bs = pbs_statvnode(c, id, a, extend)
            elif obj_type == RESV:
                bs = pbs_statresv(c, id, a, extend)
            elif obj_type == SCHED:
                bs = pbs_statsched(c, a, extend)
            elif obj_type == RSC:
                # up to PBS 12.3 pbs_statrsc was not in pbs_ifl.h
                bs = pbs_statrsc(c, id, a, extend)
            elif obj_type in (HOOK, PBS_HOOK):
                if os.getuid() != 0:
                    try:
                        rc = self.manager(MGR_CMD_LIST, obj_type, attrib,
                                          id, level=level)
                        if rc == 0:
                            if id:
                                if (obj_type == HOOK and
                                        id in self.hooks):
                                    return [self.hooks[id].attributes]
                                elif (obj_type == PBS_HOOK and
                                      id in self.pbshooks):
                                    return [self.pbshooks[id].attributes]
                                else:
                                    return None
                            if obj_type == HOOK:
                                return [h.attributes for h in
                                        self.hooks.values()]
                            elif obj_type == PBS_HOOK:
                                return [h.attributes for h in
                                        self.pbshooks.values()]
                    except Exception:
                        pass
                else:
                    bs = pbs_stathook(c, id, a, extend)
            else:
                self.logger.error(self.logprefix +
                                  "unrecognized object type " + str(obj_type))

            freebs = True
            err = self.geterrmsg()
            self._disconnect(c)

            if err:
                raise PbsStatusError(rc=-1, rv=[], msg=err)

            if not isinstance(bs, list):
                bsl = self.utils.batch_status_to_dictlist(bs, attrib)
            else:
                bsl = self.utils.filter_batch_status(bs, attrib)

        # Update each object's dictionary with corresponding attributes and
        # values
        self.update_attributes(obj_type, bsl)

        # Hook stat is done through CLI, no need to free the batch_status
        if (not isinstance(bs, list) and freebs and
                obj_type not in (HOOK, PBS_HOOK) and os.getuid() != 0):
            pbs_statfree(bs)

        # 9- Resolve indirect resources
        if obj_type in (NODE, VNODE) and resolve_indirectness:
            nodes = {}
            for _b in bsl:
                for k, v in _b.items():
                    if v.startswith('@'):
                        if v[1:] in nodes:
                            _b[k] = nodes[v[1:]][k]
                        else:
                            for l in bsl:
                                if l['id'] == v[1:]:
                                    nodes[k] = l[k]
                                    _b[k] = l[k]
                                    break
            del nodes
        return bsl

    def submit_interactive_job(self, job, cmd):
        """
        submit an ``interactive`` job. Returns a job identifier
        or raises PbsSubmitError on error
        :param cmd: The command to run to submit the interactive
                    job
        :type cmd: str
        :param job: the job object. The job must have the attribute
                    'interactive_job' populated. That attribute is
                    a list of tuples of the form:
                    (<command>, <expected output>, <...>)
                    for example to send the command
                    hostname and expect 'myhost.mydomain' one would
                    set:job.interactive_job =
                    [('hostname', 'myhost.mydomain')]
                    If more than one lines are expected they are
                    appended to the tuple.
        :raises: PbsSubmitError
        """
        ij = InteractiveJob(job, cmd, self.client)
        # start the interactive job submission thread and wait to pickup the
        # actual job identifier
        ij.start()
        while ij.jobid is None:
            continue
        return ij.jobid

    def expect(self, obj_type, attrib=None, id=None, op=EQ, attrop=PTL_AND,
               attempt=0, max_attempts=None, interval=None, count=None,
               extend=None, offset=0, runas=None, level=logging.INFO,
               msg=None, trigger_sched_cycle=True):
        """
        expect an attribute to match a given value as per an
        operation.
        :param obj_type: The type of object to query, JOB, SERVER,
                         SCHEDULER, QUEUE, NODE
        :type obj_type: str
        :param attrib: Attributes to query, can be a string, a list,
                       or a dict
        :type attrib: str or list or dictionary
        :param id: The id of the object to act upon
        :param op: An operation to perform on the queried data,
                   e.g., EQ, SET, LT,..
        :param attrop: Operation on multiple attributes, either
                       PTL_AND, PTL_OR when an PTL_AND is used, only
                       batch objects having all matches are
                       returned, otherwise an OR is applied
        :param attempt: The number of times this function has been
                        called
        :type attempt: int
        :param max_attempts: The maximum number of attempts to
                             perform
        :type max_attempts: int or None
        :param interval: The interval time between attempts.
        :param count: If True, attrib will be accumulated using
                      function counter
        :type count: bool
        :param extend: passed to the stat call
        :param offset: the time to wait before the initial check.
                       Defaults to 0.
        :type offset: int
        :param runas: query as a given user. Defaults to current
                      user
        :type runas: str or None
        :param msg: Message from last call of this function, this
                    message will be used while raising
                    PtlExpectError.
        :type msg: str or None
        :param trigger_sched_cycle: True by default can be set to False if
                          kicksched_action is not supposed to be called
        :type trigger_sched_cycle: Boolean
        :returns: True if attributes are as expected
        :raises: PtlExpectError if attributes are not as expected
        """

        if attempt == 0 and offset > 0:
            self.logger.log(level, self.logprefix + 'expect offset set to ' +
                            str(offset))
            time.sleep(offset)

        if attrib is None:
            attrib = {}

        if ATTR_version in attrib and max_attempts is None:
            max_attempts = 3

        if max_attempts is None:
            max_attempts = self.ptl_conf['max_attempts']

        if interval is None:
            interval = self.ptl_conf['attempt_interval']

        if attempt >= max_attempts:
            _msg = "expected on " + self.logprefix + msg
            raise PtlExpectError(rc=1, rv=False, msg=_msg)

        if obj_type == SERVER and id is None:
            id = self.hostname

        if isinstance(attrib, str):
            attrib = {attrib: ''}
        elif isinstance(attrib, list):
            d = {}
            for l in attrib:
                d[l] = ''
            attrib = d

        # Add check for substate=42 for jobstate=R, if not added explicitly.
        if obj_type == JOB:
            add_attribs = {}
            substate = False
            for k, v in attrib.items():
                if k == 'job_state' and ((isinstance(v, tuple) and
                                          'R' in v[-1]) or v == 'R'):
                    add_attribs['substate'] = 42
                elif k == 'job_state=R':
                    add_attribs['substate=42'] = v
                elif 'substate' in k:
                    substate = True
            if add_attribs and not substate:
                attrib.update(add_attribs)
                attrop = PTL_AND
            del add_attribs, substate

        prefix = 'expect on ' + self.logprefix
        msg = []
        attrs_to_ignore = []
        for k, v in attrib.items():
            args = None
            if isinstance(v, tuple):
                operator = v[0]
                if len(v) > 2:
                    args = v[2:]
                val = v[1]
            else:
                operator = op
                val = v
            if operator not in PTL_OP_TO_STR:
                self.logger.log(level, "Operator not supported by expect(), "
                                "cannot verify change in " + str(k))
                attrs_to_ignore.append(k)
                continue
            msg += [k, PTL_OP_TO_STR[operator].strip()]
            if isinstance(val, collections.Callable):
                msg += ['callable(' + val.__name__ + ')']
                if args is not None:
                    msg.extend([str(x) for x in args])
            else:
                msg += [str(val)]
            msg += [PTL_ATTROP_TO_STR[attrop]]

        # Delete the attributes that we cannot verify
        for k in attrs_to_ignore:
            del(attrib[k])

        if attrs_to_ignore and len(attrib) < 1 and op == SET:
            return True

        # remove the last converted PTL_ATTROP_TO_STR
        if len(msg) > 1:
            msg = msg[:-1]

        if len(attrib) == 0:
            msg += [PTL_OP_TO_STR[op]]

        msg += [PBS_OBJ_MAP[obj_type]]
        if id is not None:
            msg += [str(id)]
        if attempt > 0:
            msg += ['attempt:', str(attempt + 1)]

        # Default count to True if the attribute contains an '=' in its name
        # for example 'job_state=R' implies that a count of job_state is needed
        if count is None and self.utils.operator_in_attribute(attrib):
            count = True

        if count:
            newattr = self.utils.convert_attributes_by_op(attrib)
            if len(newattr) == 0:
                newattr = attrib

            statlist = [self._filter(obj_type, newattr, id, extend, op=op,
                                     attrop=attrop, runas=runas,
                                     level=logging.DEBUG)]
        else:
            try:
                statlist = self.status(obj_type, attrib, id=id,
                                       level=logging.DEBUG, extend=extend,
                                       runas=runas, logerr=False)
            except PbsStatusError:
                statlist = []

        if (statlist is None or len(statlist) == 0 or
                statlist[0] is None or len(statlist[0]) == 0):
            if op == UNSET or list(set(attrib.values())) == [0]:
                self.logger.log(level, prefix + " ".join(msg) + ' ...  OK')
                return True
            else:
                time.sleep(interval)
                msg = " no data for " + " ".join(msg)
                self.logger.log(level, prefix + msg)
                return self.expect(obj_type, attrib, id, op, attrop,
                                   attempt + 1, max_attempts, interval, count,
                                   extend, level=level, msg=msg)
        else:
            if op == UNSET and obj_type in (SERVER, SCHED, NODE, HOOK, QUEUE):
                for key in attrib.keys():
                    if key in self.__special_attr_keys[obj_type]:
                        val = self.get_special_attr_val(obj_type, key, id)
                        attrib = {key: val}
                        op = EQ
                        return self.expect(obj_type, attrib, id, op, attrop,
                                           attempt, max_attempts, interval,
                                           count, extend, runas=runas,
                                           level=level, msg=msg)

        if attrib is None:
            time.sleep(interval)
            return self.expect(obj_type, attrib, id, op, attrop, attempt + 1,
                               max_attempts, interval, count, extend,
                               runas=runas, level=level, msg=" ".join(msg))
        inp_op = op
        for k, v in attrib.items():
            varargs = None
            if isinstance(v, tuple):
                op = v[0]
                if len(v) > 2:
                    varargs = v[2:]
                v = v[1]
            else:
                op = inp_op

            for stat in statlist:
                if k not in stat:
                    if op == UNSET:
                        continue

                    # Sometimes users provide the wrong case for attributes
                    # Convert to lowercase and compare
                    attrs_lower = {
                        ks.lower(): [ks, vs] for ks, vs in stat.items()}
                    k_lower = k.lower()
                    if k_lower not in attrs_lower:
                        if (statlist.index(stat) + 1) < len(statlist):
                            continue
                        time.sleep(interval)
                        _tsc = trigger_sched_cycle
                        return self.expect(obj_type, attrib, id, op, attrop,
                                           attempt + 1, max_attempts,
                                           interval, count, extend,
                                           level=level, msg=" ".join(msg),
                                           trigger_sched_cycle=_tsc)
                    stat_v = attrs_lower[k_lower][1]
                    stat_k = attrs_lower[k_lower][0]
                else:
                    stat_v = stat[k]
                    stat_k = k

                if stat_k == ATTR_version:
                    m = self.version_tag.match(stat_v)
                    if m:
                        stat_v = m.group('version')
                    else:
                        time.sleep(interval)
                        return self.expect(obj_type, attrib, id, op, attrop,
                                           attempt + 1, max_attempts, interval,
                                           count, extend, runas=runas,
                                           level=level, msg=" ".join(msg))

                # functions/methods are invoked and their return value
                # used on expect
                if isinstance(v, collections.Callable):
                    if varargs is not None:
                        rv = v(stat_v, *varargs)
                    else:
                        rv = v(stat_v)
                    if isinstance(rv, bool):
                        if op == NOT:
                            if not rv:
                                continue
                        if rv:
                            continue
                    else:
                        v = rv

                stat_v = PbsAttribute.decode_value(stat_v)
                v = PbsAttribute.decode_value(str(v))

                if stat_k == ATTR_version:
                    stat_v = LooseVersion(str(stat_v))
                    v = LooseVersion(str(v))

                if op == EQ and stat_v == v:
                    continue
                elif op == SET and count and stat_v == v:
                    continue
                elif op == SET and count in (False, None):
                    continue
                elif op == NE and stat_v != v:
                    continue
                elif op == LT:
                    if stat_v < v:
                        continue
                elif op == GT:
                    if stat_v > v:
                        continue
                elif op == LE:
                    if stat_v <= v:
                        continue
                elif op == GE:
                    if stat_v >= v:
                        continue
                elif op == MATCH_RE:
                    if re.search(str(v), str(stat_v)):
                        continue
                elif op == MATCH:
                    if str(stat_v).find(str(v)) != -1:
                        continue

                msg += [' got: ' + stat_k + ' = ' + str(stat_v)]
                self.logger.info(prefix + " ".join(msg))
                time.sleep(interval)

                # run custom actions defined for this object type
                if trigger_sched_cycle and self.actions:
                    for act_obj in self.actions.get_actions_by_type(obj_type):
                        if act_obj.enabled:
                            act_obj.action(self, obj_type, attrib, id, op,
                                           attrop)
                return self.expect(obj_type, attrib, id, op, attrop,
                                   attempt + 1, max_attempts, interval, count,
                                   extend, level=level, msg=" ".join(msg),
                                   trigger_sched_cycle=trigger_sched_cycle)

        self.logger.log(level, prefix + " ".join(msg) + ' ...  OK')
        return True

    def submit(self, obj, script=None, extend=None, submit_dir=None,
               env=None):
        """
        Submit a job or reservation. Returns a job identifier
        or raises PbsSubmitError on error
        :param obj: The Job or Reservation instance to submit
        :param script: Path to a script to submit. Default: None
                       as an executable /bin/sleep 100 is submitted
        :type script: str or None
        :param extend: Optional extension to the IFL call.
                       see pbs_ifl.h
        :type extend: str or None
        :param submit_dir: directory from which job is submitted.
                           Defaults to temporary directory
        :type submit_dir: str or None
        :raises: PbsSubmitError
        """

        _interactive_job = False
        as_script = False
        rc = None
        if isinstance(obj, Job):
            if self.platform == 'cray' or self.platform == 'craysim':
                m = False
                vncompute = False
                if 'Resource_List.select' in obj.attributes:
                    select = obj.attributes['Resource_List.select']
                    start = select.startswith('vntype=cray_compute')
                    m = start or ':vntype=cray_compute' in select
                if 'Resource_List.vntype' in obj.attributes:
                    vn_type = obj.attributes['Resource_List.vntype']
                    if vn_type == 'cray_compute':
                        vncompute = True
                if obj.script is not None:
                    script = obj.script
                elif m or vncompute:
                    aprun_cmd = "aprun -b -B"
                    executable = obj.attributes[ATTR_executable]
                    start = executable.startswith('aprun ')
                    aprun_exist = start or '/aprun' in executable
                    if script:
                        aprun_cmd += " " + script
                    else:
                        if aprun_exist:
                            aprun_cmd = executable
                        else:
                            aprun_cmd += " " + executable
                        arg_list = obj.attributes[ATTR_Arglist]
                        aprun_cmd += " " + self.utils.convert_arglist(arg_list)
                    fn = self.du.create_temp_file(hostname=None,
                                                  prefix='PtlPbsJobScript',
                                                  asuser=obj.username,
                                                  body=aprun_cmd)
                    self.du.chmod(path=fn, mode=0o755)
                    script = fn
            elif script is None and obj.script is not None:
                script = obj.script
            if ATTR_inter in obj.attributes:
                _interactive_job = True
                if ATTR_executable in obj.attributes:
                    del obj.attributes[ATTR_executable]
                if ATTR_Arglist in obj.attributes:
                    del obj.attributes[ATTR_Arglist]
        elif not isinstance(obj, Reservation):
            m = self.logprefix + "unrecognized object type"
            self.logger.error(m)
            return None

        if not submit_dir:
            submit_dir = pwd.getpwnam(obj.username)[5]

        cwd = os.getcwd()
        if self.platform != 'shasta':
            if submit_dir:
                os.chdir(submit_dir)
        c = None

        # Revisit this after fixing submitting of executables without
        # the whole path
        # Get sleep command depending on which Mom the job will run
        if ((ATTR_executable in obj.attributes) and
                ('sleep' in obj.attributes[ATTR_executable])):
            obj.attributes[ATTR_executable] = (
                list(self.moms.values())[0]).sleep_cmd

        # 1- Submission using the command line tools
        runcmd = []
        if env:
            runcmd += ['#!/bin/bash\n']
            for k, v in env.items():
                if '()' in k:
                    f_name = k.replace('()', '')
                    runcmd += [k, v, "\n", "export", "-f", f_name]
                else:
                    runcmd += ['export %s=\"%s\"' % (k, v)]
                runcmd += ["\n"]

        script_file = None
        if self.get_op_mode() == PTL_CLI:
            exclude_attrs = []  # list of attributes to not convert to CLI
            if isinstance(obj, Job):
                runcmd += [os.path.join(self.client_conf['PBS_EXEC'], 'bin',
                                        'qsub')]
            elif isinstance(obj, Reservation):
                runcmd += [os.path.join(self.client_conf['PBS_EXEC'], 'bin',
                                        'pbs_rsub')]
                if ATTR_resv_start in obj.custom_attrs:
                    start = obj.custom_attrs[ATTR_resv_start]
                    obj.custom_attrs[ATTR_resv_start] = \
                        self.utils.convert_seconds_to_datetime(start)
                if ATTR_resv_end in obj.custom_attrs:
                    end = obj.custom_attrs[ATTR_resv_end]
                    obj.custom_attrs[ATTR_resv_end] = \
                        self.utils.convert_seconds_to_datetime(end)
                if ATTR_resv_timezone in obj.custom_attrs:
                    exclude_attrs += [ATTR_resv_timezone, ATTR_resv_standing]
                    # handling of impersonation differs widely across OS's,
                    # when setting PBS_TZID we standardize on running the cmd
                    # as a script instead of customizing for each OS flavor
                    _tz = obj.custom_attrs[ATTR_resv_timezone]
                    runcmd = ['PBS_TZID=' + _tz] + runcmd
                    as_script = True
                    if ATTR_resv_rrule in obj.custom_attrs:
                        _rrule = obj.custom_attrs[ATTR_resv_rrule]
                        if _rrule[0] not in ("'", '"'):
                            _rrule = "'" + _rrule + "'"
                        obj.custom_attrs[ATTR_resv_rrule] = _rrule
                if ATTR_job in obj.attributes:
                    runcmd += ['--job', obj.attributes[ATTR_job]]
                    exclude_attrs += [ATTR_job]

            if not self._is_local:
                if ATTR_queue not in obj.attributes:
                    runcmd += ['-q@' + self.hostname]
                elif '@' not in obj.attributes[ATTR_queue]:
                    curq = obj.attributes[ATTR_queue]
                    runcmd += ['-q' + curq + '@' + self.hostname]
                if obj.custom_attrs and (ATTR_queue in obj.custom_attrs):
                    del obj.custom_attrs[ATTR_queue]

            _conf = self.default_client_pbs_conf
            cmd = self.utils.convert_to_cli(obj.custom_attrs, IFL_SUBMIT,
                                            self.hostname, dflt_conf=_conf,
                                            exclude_attrs=exclude_attrs)

            if cmd is None:
                try:
                    os.chdir(cwd)
                except OSError:
                    pass
                return None
            runcmd += cmd

            if script:
                runcmd += [script]
            else:
                if ATTR_executable in obj.attributes:
                    runcmd += ['--', obj.attributes[ATTR_executable]]
                    if ((ATTR_Arglist in obj.attributes) and
                            (obj.attributes[ATTR_Arglist] is not None)):
                        args = obj.attributes[ATTR_Arglist]
                        arglist = self.utils.convert_arglist(args)
                        if arglist is None:
                            try:
                                os.chdir(cwd)
                            except OSError:
                                pass
                            return None
                        runcmd += [arglist]
            if obj.username != self.current_user:
                runas = obj.username
            else:
                runas = None

            if isinstance(obj, Reservation) and obj.hosts:
                runcmd += ['--hosts'] + obj.hosts

            if _interactive_job:
                ijid = self.submit_interactive_job(obj, runcmd)
                try:
                    os.chdir(cwd)
                except OSError:
                    pass
                return ijid

            if not self.default_client_pbs_conf:
                runcmd = [
                    'PBS_CONF_FILE=' + self.client_pbs_conf_file] + runcmd
                as_script = True
            if env:
                user = PbsUser.get_user(obj.username)
                host = user.host
                run_str = " ".join(runcmd)
                script_file = self.du.create_temp_file(hostname=host,
                                                       body=run_str)
                self.du.chmod(hostname=host, path=script_file, mode=0o755)
                runcmd = [script_file]
            ret = self.du.run_cmd(self.client, runcmd, runas=runas,
                                  level=logging.INFOCLI, as_script=as_script,
                                  env=env, logerr=False)
            if ret['rc'] != 0:
                objid = None
            else:
                objid = ret['out'][0]
            if ret['err'] != ['']:
                self.last_error = ret['err']
            self.last_rc = rc = ret['rc']

        # 2- Submission with impersonation over API
        elif obj.username != self.current_user:
            # submit job as a user requires setting uid to that user. It's
            # done in a separate process
            obj.set_variable_list(obj.username, submit_dir)
            obj.set_attributes()
            if (obj.script is not None and not self._is_local):
                # This copy assumes that the file system layout on the
                # remote host is identical to the local host. When not
                # the case, this code will need to be updated to copy
                # to a known remote location and update the obj.script
                self.du.run_copy(
                    self.hostname, src=obj.script, dest=obj.script)
                os.remove(obj.script)
            objid = self.pbs_api_as('submit', obj, user=obj.username,
                                    extend=extend)
        # 3- Submission as current user over API
        else:
            c = self._connect(self.hostname)

            if isinstance(obj, Job):
                if script:
                    if ATTR_o not in obj.attributes:
                        obj.attributes[ATTR_o] = (self.hostname + ':' +
                                                  obj.script + '.o')
                    if ATTR_e not in obj.attributes:
                        obj.attributes[ATTR_e] = (self.hostname + ':' +
                                                  obj.script + '.e')
                    sc = os.path.basename(script)
                    obj.unset_attributes([ATTR_executable, ATTR_Arglist])
                    if ATTR_N not in obj.custom_attrs:
                        obj.attributes[ATTR_N] = sc
                if ATTR_queue in obj.attributes:
                    destination = obj.attributes[ATTR_queue]
                    # queue must be removed otherwise will cause the submit
                    # to fail silently
                    del obj.attributes[ATTR_queue]
                else:
                    destination = None

                    if (ATTR_o not in obj.attributes or
                            ATTR_e not in obj.attributes):
                        fn = PbsAttribute.random_str(
                            length=4, prefix='PtlPbsJob')
                        tmp = self.du.get_tempdir(self.hostname)
                        fn = os.path.join(tmp, fn)
                    if ATTR_o not in obj.attributes:
                        obj.attributes[ATTR_o] = (self.hostname + ':' +
                                                  fn + '.o')
                    if ATTR_e not in obj.attributes:
                        obj.attributes[ATTR_e] = (self.hostname + ':' +
                                                  fn + '.e')

                obj.attropl = self.utils.dict_to_attropl(obj.attributes)
                objid = pbs_submit(c, obj.attropl, script, destination,
                                   extend)
            elif isinstance(obj, Reservation):
                if ATTR_resv_duration in obj.attributes:
                    # reserve_duration is not a valid attribute, the API call
                    # will get rejected if it is used
                    wlt = ATTR_l + '.walltime'
                    obj.attributes[wlt] = obj.attributes[ATTR_resv_duration]
                    del obj.attributes[ATTR_resv_duration]

                obj.attropl = self.utils.dict_to_attropl(obj.attributes)
                objid = pbs_submit_resv(c, obj.attropl, extend)

        prefix = 'submit to ' + self.shortname + ' as '
        if isinstance(obj, Job):
            self.logit(prefix + '%s: ' % obj.username, JOB, obj.custom_attrs,
                       objid)
            if obj.script_body:
                self.logger.log(logging.INFOCLI, 'job script ' + script +
                                '\n---\n' + obj.script_body + '\n---')
            if objid is not None:
                self.jobs[objid] = obj
        elif isinstance(obj, Reservation):
            # Reservations without -I option return as 'R123 UNCONFIRMED'
            # so split to get the R123 only

            self.logit(prefix + '%s: ' % obj.username, RESV, obj.attributes,
                       objid)
            if objid is not None:
                objid = objid.split()[0]
                self.reservations[objid] = obj

        if objid is not None:
            obj.server[self.hostname] = objid
        else:
            try:
                os.chdir(cwd)
            except OSError:
                pass
            raise PbsSubmitError(rc=rc, rv=None, msg=self.geterrmsg(),
                                 post=self._disconnect, conn=c)

        if c:
            self._disconnect(c)

        try:
            os.chdir(cwd)
        except OSError:
            pass

        return objid

    def submit_resv(self, offset, duration, select='1:ncpus=1', rrule='',
                    times=False, conf=None):
        """
        Helper function to submit an advance/a standing reservation.
        :param int offset: Time in seconds from time this is called to set the
                       advance reservation's start time.
        :param int duration: Duration in seconds of advance reservation
        :param str select: Select statement for reservation placement.
                           Default: "1:ncpus=1"
        :param str rrule: Recurrence rule.  Default is an empty string.
        :param boolean times: If true, return a tuple of reservation id, start
                              time and end time of created reservation.
                              Otherwise return just the reservation id.
                              Default: False
        :param conf: Configuration for test case for PBS_TZID information.
        :return The reservation id if times is false.  Otherwise a tuple of
                reservation id, start time and end time of the reservation.

        """
        start_time = int(time.time()) + offset
        end_time = start_time + duration

        attrs = {
            'reserve_start': start_time,
            'reserve_end': end_time,
            'Resource_List.select': select
        }

        if rrule:
            if conf is None:
                self.logger.info('conf not set. Falling back to Asia/Kolkata')
                tzone = 'Asia/Kolkata'
            elif 'PBS_TZID' in conf:
                tzone = conf['PBS_TZID']
            elif 'PBS_TZID' in os.environ:
                tzone = os.environ['PBS_TZID']
            else:
                self.logger.info('Missing timezone, using Asia/Kolkata')
                tzone = 'Asia/Kolkata'
            attrs[ATTR_resv_rrule] = rrule
            attrs[ATTR_resv_timezone] = tzone

        rid = self.submit(Reservation(TEST_USER, attrs))

        if times:
            return rid, start_time, end_time
        return rid

    def alter_a_reservation(self, r, start, end, shift=0,
                            alter_s=False, alter_e=False,
                            whichMessage=1, confirm=True, check_log=True,
                            interactive=0, sequence=1,
                            a_duration=None, select=None, extend=None,
                            runas=None, sched_down=False):
        """
        Helper method for altering a reservation.
        This method also checks for the server and accounting logs.

        :param r: Reservation id.
        :type  r: string.

        :param start: Start time of the reservation.
        :type  start: int.

        :param end: End time of the reservation.
        :type  end: int

        :param shift: Time in seconds the reservation times will be moved.
        :type  shift: int.

        :param alter_s: Whether the caller intends to change the start time.
                       Default - False.
        :type  alter_s: bool.

        :param alter_e: Whether the caller intends to change the end time.
                       Default - False.
        :type  alter_e: bool.

        :param whichMessage: Which message is expected to be returned.
                            Default: 1.
                             =-1 - No exception, don't check logs
                             =0 - PbsResvAlterError exception will be raised,
                                  so check for appropriate error response.
                             =1 - No exception, check for "CONFIRMED" message
                             =2 - No exception, check for "UNCONFIRMED" message
                             =3 - No exception, check for "DENIED" message
        :type  whichMessage: int.
        :param check_log: If False, do not check the log of confirmation of the
                          reservation.  Default: True
        :tupe check_log boolean

        :param confirm: The expected state of the reservation after it is
                       altered. It can be either Confirmed or Running.
                       Default - Confirmed State.
        :type  confirm: bool.

        :param sched_down: The test is being run with the scheduler down.
                           Don't wait for confirmed or running states.
                           Default - False
        :type sched_down: bool

        :param interactive: Time in seconds the CLI waits for a reply.
                           Default - 0 seconds.
        :type  interactive: int.

        :param sequence: To check the number of log matches corresponding
                        to alter.
                        Default: 1
        :type  sequence: int.

        :param a_duration: The duration to modify.
        :type a_duration: int.
        :param extend: extend parameter.
        :type extend: str.
        :param runas: User who own alters the reservation.
                      Default: user running the test.
        :type runas: PbsUser.

        raises: PBSResvAlterError
        """
        fmt = "%a %b %d %H:%M:%S %Y"
        new_start = start
        new_end = end
        attrs = {}
        bu = BatchUtils()

        if alter_s:
            new_start = start + shift
            new_start_conv = bu.convert_seconds_to_datetime(
                new_start)
            attrs['reserve_start'] = new_start_conv

        if alter_e:
            new_end = end + shift
            new_end_conv = bu.convert_seconds_to_datetime(new_end)
            attrs['reserve_end'] = new_end_conv

        if interactive > 0:
            attrs['interactive'] = interactive

        if a_duration:
            if isinstance(a_duration, str) and ':' in a_duration:
                new_duration_conv = bu.convert_duration(a_duration)
            else:
                new_duration_conv = a_duration

            if not alter_s and not alter_e:
                new_end = start + new_duration_conv + shift
            elif alter_s and not alter_e:
                new_end = new_start + new_duration_conv
            elif not alter_s and alter_e:
                new_start = new_end - new_duration_conv
            # else new_start and new_end have already been calculated
        else:
            new_duration_conv = new_end - new_start

        if a_duration:
            attrs['reserve_duration'] = new_duration_conv

        if select:
            attrs['Resource_List.select'] = select

        if runas is None:
            runas = self.du.get_current_user()

        if whichMessage:
            msg = ['']
            acct_msg = ['']

            if interactive:
                if whichMessage == 1:
                    msg = "pbs_ralter: " + r + " CONFIRMED"
                elif whichMessage == 2:
                    msg = "pbs_ralter: " + r + " UNCONFIRMED"
                else:
                    msg = "pbs_ralter: " + r + " DENIED"
            else:
                msg = "pbs_ralter: " + r + " ALTER REQUESTED"

            self.alterresv(r, attrs, extend=extend, runas=runas)

            if msg != self.last_out[0]:
                raise PBSResvAlterError(
                    msg=f"Wrong Message expected {msg} got {self.last_out[0]}")
            self.logger.info(msg + " displayed")

            if check_log:
                msg = "Resv;" + r + ";Attempting to modify reservation "
                if start != new_start:
                    msg += "start="
                    msg += time.strftime(fmt,
                                         time.localtime(int(new_start)))
                    msg += " "

                if end != new_end:
                    msg += "end="
                    msg += time.strftime(fmt,
                                         time.localtime(int(new_end)))
                    msg += " "

                if select:
                    msg += "select=" + select + " "

                # strip the last space
                msg = msg[:-1]
                self.log_match(msg, interval=2, max_attempts=30)

            if whichMessage == -1:
                return new_start, new_end
            elif whichMessage == 1:
                if alter_s:
                    new_start_conv = bu.convert_seconds_to_datetime(
                        new_start, fmt)
                    attrs['reserve_start'] = new_start_conv

                if alter_e:
                    new_end_conv = bu.convert_seconds_to_datetime(
                        new_end, fmt)
                    attrs['reserve_end'] = new_end_conv

                if a_duration:
                    attrs['reserve_duration'] = new_duration_conv

                if sched_down:
                    attrs['reserve_state'] = (MATCH_RE,
                                              'RESV_BEING_ALTERED|11')
                elif confirm:
                    attrs['reserve_state'] = (MATCH_RE, 'RESV_CONFIRMED|2')
                else:
                    attrs['reserve_state'] = (MATCH_RE, 'RESV_RUNNING|5')

                self.expect(RESV, attrs, id=r)
                if check_log:
                    acct_msg = "Y;" + r + ";requestor=Scheduler@.*" + " start="
                    acct_msg += str(new_start) + " end=" + str(new_end)
                    self.status(RESV, 'resv_nodes', id=r)
                    acct_msg += " nodes="
                    acct_msg += re.escape(self.reservations[r].
                                          resvnodes())

                    if r[0] == 'S':
                        self.status(RESV, 'reserve_count', id=r)
                        count = self.reservations[r].attributes[
                            'reserve_count']
                        acct_msg += " count=" + count

                    self.accounting_match(acct_msg, regexp=True,
                                          interval=2,
                                          max_attempts=30, n='ALL')

                # Check if reservation reports new start time
                # and updated duration.

                msg = "Resv;" + r + ";Reservation alter confirmed"
            else:
                msg = "Resv;" + r + ";Reservation alter denied"
            interval = 0.5
            max_attempts = 20
            if sched_down:
                self.logger.info("Scheduler Down: Modify should not succeed.")
                return start, end
            for attempts in range(1, max_attempts + 1):
                lines = self.log_match(msg, n='ALL', allmatch=True,
                                              max_attempts=5)
                info_msg = "log_match: searching " + \
                    str(sequence) + " sequence of message: " + \
                    msg + ": Got: " + str(len(lines))
                self.logger.info(info_msg)
                if len(lines) == sequence:
                    break
                else:
                    attempts = attempts + 1
                    time.sleep(interval)
            if attempts > max_attempts:
                raise PtlLogMatchError(rc=1, rv=False, msg=info_msg)
            return new_start, new_end
        else:
            try:
                self.alterresv(r, attrs, extend=extend, runas=runas)
            except PbsResvAlterError:
                self.logger.info(
                    "Reservation Alteration failed.  This is expected.")
                return start, end
            else:
                self.assertFalse("Reservation alter allowed when it should" +
                                 "not be.")

    def deljob(self, id=None, extend=None, runas=None, wait=False,
               logerr=True, attr_W=None):
        """
        delete a single job or list of jobs specified by id
        raises ``PbsDeljobError`` on error
        :param id: The identifier(s) of the jobs to delete
        :type id: str or list
        :param extend: Optional parameters to pass along to PBS
        :type extend: str or None
        :param runas: run as user
        :type runas: str or None
        :param wait: Set to True to wait for job(s) to no longer
                     be reported by PBS. False by default
        :type wait: bool
        :param logerr: Whether to log errors. Defaults to True.
        :type logerr: bool
        :param attr_w: -W args to qdel (Only for cli mode)
        :type attr_w: str
        :raises: PbsDeljobError
        """
        prefix = 'delete job on ' + self.shortname
        if runas is not None:
            prefix += ' as ' + str(runas)
        prefix += ': '
        if id is not None:
            if not isinstance(id, list):
                id = id.split(',')
            prefix += ', '.join(id)
        self.logger.info(prefix)
        c = None
        rc = 0
        if self.get_op_mode() == PTL_CLI:
            pcmd = [os.path.join(self.client_conf['PBS_EXEC'], 'bin', 'qdel')]
            if extend is not None:
                pcmd += self.utils.convert_to_cli(extend, op=IFL_DELETE,
                                                  hostname=self.hostname)
            if attr_W is not None:
                pcmd += ['-W']
                if attr_W != PTL_NOARG:
                    pcmd += [attr_W]
            if not self.default_client_pbs_conf:
                pcmd = ['PBS_CONF_FILE=' + self.client_pbs_conf_file] + pcmd
                as_script = True
            elif not self._is_local:
                pcmd = ['PBS_SERVER=' + self.hostname] + pcmd
                as_script = True
            else:
                as_script = False
            if id is not None:
                chunks = [id[i:i + 2000] for i in range(0, len(id), 2000)]
                for chunk in chunks:
                    ret = self.du.run_cmd(self.client, pcmd + chunk,
                                          runas=runas, as_script=as_script,
                                          logerr=logerr, level=logging.INFOCLI)
                    rc = ret['rc']
                    if ret['err'] != ['']:
                        self.last_error = ret['err']
                    self.last_rc = rc
                    if rc != 0:
                        break
            else:
                ret = self.du.run_cmd(self.client, pcmd, runas=runas,
                                      as_script=as_script, logerr=logerr,
                                      level=logging.INFOCLI)
                rc = ret['rc']
                if ret['err'] != ['']:
                    self.last_error = ret['err']
                self.last_rc = rc
        elif runas is not None:
            rc = self.pbs_api_as('deljob', id, user=runas, extend=extend)
        else:
            c = self._connect(self.hostname)
            rc = 0
            for ajob in id:
                tmp_rc = pbs_deljob(c, ajob, extend)
                if tmp_rc != 0:
                    rc = tmp_rc
        if rc != 0:
            raise PbsDeljobError(rc=rc, rv=False, msg=self.geterrmsg(),
                                 post=self._disconnect, conn=c)
        if self.jobs is not None:
            for j in id:
                if j in self.jobs:
                    if self.jobs[j].interactive_handle is not None:
                        self.jobs[j].interactive_handle.close()
                    del self.jobs[j]
        if c:
            self._disconnect(c)
        if wait and id is not None:
            for oid in id:
                self.expect(JOB, 'queue', id=oid, op=UNSET, runas=runas,
                            level=logging.DEBUG)
        return rc

    def delresv(self, id=None, extend=None, runas=None, wait=False,
                logerr=True):
        """
        delete a single job or list of jobs specified by id
        raises ``PbsDeljobError`` on error
        :param id: The identifier(s) of the jobs to delete
        :type id: str or list
        :param extend: Optional parameters to pass along to PBS
        :type extend: str or None
        :param runas: run as user
        :type runas: str or None
        :param wait: Set to True to wait for job(s) to no longer
                     be reported by PBS. False by default
        :type wait: bool
        :param logerr: Whether to log errors. Defaults to True.
        :type logerr: bool
        :raises: PbsDeljobError
        """
        prefix = 'delete resv on ' + self.shortname
        if runas is not None:
            prefix += ' as ' + str(runas)
        prefix += ': '
        if id is not None:
            if not isinstance(id, list):
                id = id.split(',')
            prefix += ', '.join(id)
        self.logger.info(prefix)
        c = None
        rc = 0
        if self.get_op_mode() == PTL_CLI:
            pcmd = [os.path.join(self.client_conf['PBS_EXEC'], 'bin',
                                 'pbs_rdel')]
            if not self.default_client_pbs_conf:
                pcmd = ['PBS_CONF_FILE=' + self.client_pbs_conf_file] + pcmd
                as_script = True
            elif not self._is_local:
                pcmd = ['PBS_SERVER=' + self.hostname] + pcmd
                as_script = True
            else:
                as_script = False
            if id is not None:
                chunks = [id[i:i + 2000] for i in range(0, len(id), 2000)]
                for chunk in chunks:
                    ret = self.du.run_cmd(self.client, pcmd + chunk,
                                          runas=runas, as_script=as_script,
                                          logerr=logerr, level=logging.INFOCLI)
                    rc = ret['rc']
                    if ret['err'] != ['']:
                        self.last_error = ret['err']
                    self.last_rc = rc
                    if rc != 0:
                        break
            else:
                ret = self.du.run_cmd(self.client, pcmd, runas=runas,
                                      as_script=as_script, logerr=logerr,
                                      level=logging.INFOCLI)
                rc = ret['rc']
                if ret['err'] != ['']:
                    self.last_error = ret['err']
                self.last_rc = rc
        elif runas is not None:
            rc = self.pbs_api_as('delresv', id, user=runas, extend=extend)
        else:
            c = self._connect(self.hostname)
            rc = 0
            for ajob in id:
                tmp_rc = pbs_delresv(c, ajob, extend)
                if tmp_rc != 0:
                    rc = tmp_rc
        if rc != 0:
            raise PbsDelresvError(rc=rc, rv=False, msg=self.geterrmsg(),
                                  post=self._disconnect, conn=c)
        if self.reservations is not None:
            for j in id:
                if j in self.reservations:
                    del self.reservations[j]
        if c:
            self._disconnect(c)
        if wait and id is not None:
            for oid in id:
                self.expect(RESV, 'queue', id=oid, op=UNSET, runas=runas,
                            level=logging.DEBUG)
        return rc

    def delete(self, id=None, extend=None, runas=None, wait=False,
               logerr=True):
        """
        delete a single job or list of jobs specified by id
        raises ``PbsDeleteError`` on error
        :param id: The identifier(s) of the jobs/resvs to delete
        :type id: str or list
        :param extend: Optional parameters to pass along to PBS
        :type extend: str or none
        :param runas: run as user
        :type runas: str
        :param wait: Set to True to wait for job(s)/resv(s) to
                     no longer be reported by PBS. False by default
        :type wait: bool
        :param logerr: Whether to log errors. Defaults to True.
        :type logerr: bool
        :raises: PbsDeleteError
        """
        prefix = 'delete on ' + self.shortname
        if runas is not None:
            prefix += ' as ' + str(runas)
        prefix += ': '
        if id is not None:
            if not isinstance(id, list):
                id = id.split(',')
            prefix += ','.join(id)
        if extend is not None:
            prefix += ' with ' + str(extend)
        self.logger.info(prefix)

        if not len(id) > 0:
            return 0

        obj_type = {}
        for j in id:
            if j[0] in ('R', 'S', 'M'):
                obj_type[j] = RESV
                try:
                    rc = self.delresv(j, extend, runas, logerr=logerr)
                except PbsDelresvError as e:
                    rc = e.rc
                    msg = e.msg
                    rv = e.rv
            else:
                obj_type[j] = JOB
                try:
                    rc = self.deljob(j, extend, runas, logerr=logerr)
                except PbsDeljobError as e:
                    rc = e.rc
                    msg = e.msg
                    rv = e.rv

        if rc != 0:
            raise PbsDeleteError(rc=rc, rv=rv, msg=msg)

        if wait:
            for oid in id:
                self.expect(obj_type[oid], 'queue', id=oid, op=UNSET,
                            runas=runas, level=logging.DEBUG)

        return rc

    def select(self, attrib=None, extend=None, runas=None, logerr=True):
        """
        Select jobs that match attributes list or all jobs if no
        attributes raises ``PbsSelectError`` on error
        :param attrib: A string, list, or dictionary of attributes
        :type attrib: str or list or dictionary
        :param extend: the extended attributes to pass to select
        :type extend: str or None
        :param runas: run as user
        :type runas: str or None
        :param logerr: If True (default) logs run_cmd errors
        :type logerr: bool
        :returns: A list of job identifiers that match the
                  attributes specified
        :raises: PbsSelectError
        """
        prefix = "select on " + self.shortname
        if runas is not None:
            prefix += " as " + str(runas)
        prefix += ": "
        if attrib is None:
            s = PTL_ALL
        elif not isinstance(attrib, dict):
            self.logger.error(prefix + "attributes must be a dictionary")
            return
        else:
            s = str(attrib)
        self.logger.info(prefix + s)

        c = None
        if self.get_op_mode() == PTL_CLI:
            pcmd = [os.path.join(self.client_conf['PBS_EXEC'],
                                 'bin', 'qselect')]

            cmd = self.utils.convert_to_cli(attrib, op=IFL_SELECT,
                                            hostname=self.hostname)
            if extend is not None:
                pcmd += ['-' + extend]

            if not self._is_local and ((attrib is None) or
                                       (ATTR_queue not in attrib)):
                pcmd += ['-q', '@' + self.hostname]

            pcmd += cmd
            if not self.default_client_pbs_conf:
                pcmd = ['PBS_CONF_FILE=' + self.client_pbs_conf_file] + pcmd
                as_script = True
            else:
                as_script = False

            ret = self.du.run_cmd(self.client, pcmd, runas=runas,
                                  as_script=as_script, level=logging.INFOCLI,
                                  logerr=logerr)
            if ret['err'] != ['']:
                self.last_error = ret['err']
            self.last_rc = ret['rc']
            if self.last_rc != 0:
                raise PbsSelectError(rc=self.last_rc, rv=False,
                                     msg=self.geterrmsg())
            jobs = ret['out']
            # command returns no jobs as empty, since we expect a valid id,
            # we reset the jobs to an empty array
            if len(jobs) == 1 and jobs[0] == '':
                jobs = []
        elif runas is not None:
            jobs = self.pbs_api_as('select', user=runas, data=attrib,
                                   extend=extend)
        else:
            attropl = self.utils.convert_to_attropl(attrib, op=EQ)
            c = self._connect(self.hostname)
            jobs = pbs_selectjob(c, attropl, extend)
            err = self.geterrmsg()
            if err:
                raise PbsSelectError(rc=-1, rv=False, msg=err,
                                     post=self._disconnect, conn=c)
            self._disconnect(c)

        return jobs

    def selstat(self, select_list, rattrib, runas=None, extend=None):
        """
        stat and filter jobs attributes.
        :param select_list: The filter criteria
        :type select: List
        :param rattrib: The attributes to query
        :type rattrib: List
        :param runas: run as user
        :type runas: str or None
        .. note:: No ``CLI`` counterpart for this call
        """

        attrl = self.utils.convert_to_attrl(rattrib)
        attropl = self.utils.convert_to_attropl(select_list)

        c = self._connect(self.hostname)
        bs = pbs_selstat(c, attropl, attrl, extend)
        self._disconnect(c)
        return bs

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

    def manager(self, cmd, obj_type, attrib=None, id=None, extend=None,
                level=logging.INFO, sudo=None, runas=None, logerr=True):
        """
        issue a management command to the server, e.g to set an
        attribute
        Returns 0 for Success and non 0 number for Failure
        :param cmd: The command to issue,
                    ``MGR_CMD_[SET,UNSET, LIST,...]`` see pbs_ifl.h
        :type cmd: str
        :param obj_type: The type of object to query, one of
                         the * objects
        :param attrib: Attributes to operate on, can be a string, a
                       list,a dictionary
        :type attrib: str or list or dictionary
        :param id: The name or list of names of the object(s) to act
                   upon.
        :type id: str or list
        :param extend: Optional extension to the IFL call. see
                       pbs_ifl.h
        :type extend: str or None
        :param level: logging level
        :param sudo: If True, run the manager command as super user.
                     Defaults to None. Some attribute settings
                     should be run with sudo set to True, those are
                     acl_roots, job_sort_formula, hook operations,
                     no_sched_hook_event, in those cases, setting
                     sudo to False is only needed for testing
                     purposes
        :type sudo: bool
        :param runas: run as user
        :type runas: str
        :param logerr: If False, CLI commands do not log error,
                       i.e. silent mode
        :type logerr: bool
        :raises: PbsManagerError
        """

        if cmd == MGR_CMD_DELETE and obj_type == NODE and id != '@default':
            for cmom, momobj in self.moms.items():
                if momobj.is_cpuset_mom():
                    self.skipTest("Do not delete nodes on cpuset moms")

        if ((cmd == MGR_CMD_SET or cmd == MGR_CMD_CREATE) and
                id is not None and obj_type == NODE):
            for cmom, momobj in self.moms.items():
                cpuset_nodes = []
                if momobj.is_cpuset_mom() and attrib:
                    momobj.check_mem_request(attrib)
                    if len(attrib) == 0:
                        return True
                    vnodes = self.status(HOST, id=momobj.shortname)
                    del vnodes[0]  # don't set anything on a naturalnode
                    for vn in vnodes:
                        momobj.check_ncpus_request(attrib, vn)
                    if len(attrib) == 0:
                        return True

        if isinstance(id, str):
            oid = id.split(',')
        else:
            oid = id

        self.logit('manager on ' + self.shortname +
                   [' as ' + str(runas), ''][runas is None] + ': ' +
                   PBS_CMD_MAP[cmd] + ' ', obj_type, attrib, oid, level=level)

        c = None  # connection handle

        if (self.get_op_mode() == PTL_CLI or
            sudo is not None or
            obj_type in (HOOK, PBS_HOOK) or
            (attrib is not None and ('job_sort_formula' in attrib or
                                     'acl_roots' in attrib or
                                     'no_sched_hook_event' in attrib))):

            execcmd = [PBS_CMD_MAP[cmd], PBS_OBJ_MAP[obj_type]]

            if oid is not None:
                if cmd == MGR_CMD_DELETE and obj_type == NODE and oid[0] == "":
                    oid[0] = "@default"
                execcmd += [",".join(oid)]

            if attrib is not None and cmd != MGR_CMD_LIST:
                if cmd == MGR_CMD_IMPORT:
                    execcmd += [attrib['content-type'],
                                attrib['content-encoding'],
                                attrib['input-file']]
                else:
                    if isinstance(attrib, (dict, OrderedDict)):
                        kvpairs = []
                        for k, v in attrib.items():
                            if isinstance(v, tuple):
                                if v[0] == INCR:
                                    op = '+='
                                elif v[0] == DECR:
                                    op = '-='
                                else:
                                    msg = 'Invalid operation: %s' % (v[0])
                                    raise PbsManagerError(rc=1, rv=False,
                                                          msg=msg)
                                v = v[1]
                            else:
                                op = '='
                            if isinstance(v, list) and not isinstance(v, str):
                                # handle string arrays with strings
                                # that contain special characters
                                # with multiple manager calls
                                if any((c in vv) for c in set(', \'\n"')
                                       for vv in v):
                                    if op == '+=' or op == '=':
                                        oper = INCR
                                    else:
                                        oper = DECR
                                    for vv in v:
                                        a = {k: (oper, vv)}
                                        rc = self.manager(cmd=cmd,
                                                          obj_type=obj_type,
                                                          attrib=a, id=id,
                                                          extend=extend,
                                                          level=level,
                                                          sudo=sudo,
                                                          runas=runas,
                                                          logerr=logerr)
                                        if rc:
                                            return rc
                                    return 0
                                # if there are no special characters, then
                                # join the list and parse it normally.
                                v = ','.join(v)
                            if isinstance(v, str):
                                # don't quote if already quoted
                                if v[0] == v[-1] and v[0] in set('"\''):
                                    pass
                                # handle string arrays
                                elif ',' in v and v[0] != '"':
                                    v = '"' + v + '"'
                                # handle strings that need to be quoted
                                elif any((c in v) for c in set(', \'\n"')):
                                    if '"' in v:
                                        v = "'%s'" % v
                                    else:
                                        v = '"%s"' % v
                            kvpairs += [str(k) + op + str(v)]
                        if kvpairs:
                            execcmd += [",".join(kvpairs)]
                            del kvpairs
                    elif isinstance(attrib, list):
                        execcmd += [",".join(attrib)]
                    elif isinstance(attrib, str):
                        execcmd += [attrib]

            if not self.default_pbs_conf or not self.default_client_pbs_conf:
                as_script = True
            else:
                as_script = False

            if (not self._is_local or as_script or
                (runas and
                 not self.du.is_localhost(PbsUser.get_user(runas).host))):
                execcmd = '\'' + " ".join(execcmd) + '\''
            else:
                execcmd = " ".join(execcmd)

            # Hooks can only be queried as a privileged user on the host where
            # the server is running, care must be taken to use the appropriate
            # path to qmgr and appropriate escaping sequences
            # VERSION INFO: no_sched_hook_event introduced in 11.3.120 only
            if sudo is None:
                if (obj_type in (HOOK, PBS_HOOK) or
                    (attrib is not None and
                     ('job_sort_formula' in attrib or
                      'acl_roots' in attrib or
                      'no_sched_hook_event' in attrib))):
                    sudo = True
                else:
                    sudo = False

            pcmd = [os.path.join(self.pbs_conf['PBS_EXEC'], 'bin', 'qmgr'),
                    '-c', execcmd]

            if as_script:
                pcmd = ['PBS_CONF_FILE=' + self.client_pbs_conf_file] + pcmd

            ret = self.du.run_cmd(self.hostname, pcmd, sudo=sudo, runas=runas,
                                  level=logging.INFOCLI, as_script=as_script,
                                  logerr=logerr)
            rc = ret['rc']
            # NOTE: workaround the fact that qmgr overloads the return code in
            # cases where the list returned is empty an error flag is set even
            # through there is no error. Handled here by checking if there is
            # no err and out message, in which case return code is set to 0
            if rc != 0 and (ret['out'] == [''] and ret['err'] == ['']):
                rc = 0
            if rc == 0:
                if cmd == MGR_CMD_LIST:
                    bsl = self.utils.convert_to_dictlist(ret['out'],
                                                         mergelines=True,
                                                         obj_type=obj_type)
                    # Since we stat everything, overwrite the cache
                    self.update_attributes(obj_type, bsl, overwrite=True)
                    # Filter out the attributes requested
                    if attrib:
                        bsl_attr = []
                        for obj in bsl:
                            dnew = {}
                            for k in obj.keys():
                                if k in attrib:
                                    dnew[k] = obj[k]
                            bsl_attr.append(dnew)
                        bsl = bsl_attr
            else:
                # Need to rework setting error, this is not thread safe
                self.last_error = ret['err']
            self.last_rc = ret['rc']
        elif runas is not None:
            _data = {'cmd': cmd, 'obj_type': obj_type, 'attrib': attrib,
                     'id': oid}
            rc = self.pbs_api_as('manager', user=runas, data=_data,
                                 extend=extend)
        else:
            a = self.utils.convert_to_attropl(attrib, cmd)
            c = self._connect(self.hostname)
            rc = 0
            if obj_type == SERVER and oid is None:
                oid = [self.hostname]
            if oid is None:
                # server will run strlen on id, it can not be NULL
                oid = ['']
            if cmd == MGR_CMD_LIST:
                if oid is None:
                    bsl = self.status(obj_type, attrib, oid, extend)
                else:
                    bsl = None
                    for i in oid:
                        tmpbsl = self.status(obj_type, attrib, i, extend)
                        if tmpbsl is None:
                            rc = 1
                        else:
                            if bsl is None:
                                bsl = tmpbsl
                            else:
                                bsl += tmpbsl
            else:
                rc = 0
                if oid is None:
                    rc = pbs_manager(c, cmd, obj_type, i, a, extend)
                else:
                    for i in oid:
                        tmprc = pbs_manager(c, cmd, obj_type, i, a, extend)
                        if tmprc != 0:
                            rc = tmprc
                            break
                    if rc == 0:
                        rc = tmprc

        if id is None and obj_type == SERVER:
            id = self.pbs_conf['PBS_SERVER']
        bs_list = []
        if cmd == MGR_CMD_DELETE and oid is not None and rc == 0:
            for i in oid:
                if obj_type == MGR_OBJ_HOOK and i in self.hooks:
                    del self.hooks[i]
                if obj_type in (NODE, VNODE) and i in self.nodes:
                    del self.nodes[i]
                if obj_type == MGR_OBJ_QUEUE and i in self.queues:
                    del self.queues[i]
                if obj_type == MGR_OBJ_RSC and i in self.resources:
                    del self.resources[i]
                if obj_type == SCHED and i in self.schedulers:
                    del self.schedulers[i]

        elif cmd == MGR_CMD_SET and rc == 0 and id is not None:
            if isinstance(id, list):
                for name in id:
                    tbsl = copy.deepcopy(attrib)
                    tbsl['name'] = name
                    bs_list.append(tbsl)
                    self.update_attributes(obj_type, bs_list)
            else:
                tbsl = copy.deepcopy(attrib)
                tbsl['id'] = id
                bs_list.append(tbsl)
                self.update_attributes(obj_type, bs_list)

        elif cmd == MGR_CMD_CREATE and rc == 0:
            if isinstance(id, list):
                for name in id:
                    bsl = self.status(obj_type, id=name, extend=extend)
                    self.update_attributes(obj_type, bsl)
            else:
                bsl = self.status(obj_type, id=id, extend=extend)
                self.update_attributes(obj_type, bsl)

        if rc != 0:
            raise PbsManagerError(rv=False, rc=rc, msg=self.geterrmsg(),
                                  post=self._disconnect, conn=c)

        if c is not None:
            self._disconnect(c)
        if cmd == MGR_CMD_SET and 'scheduling' in attrib:
            if attrib['scheduling'] in PTL_FALSE:
                if obj_type == SERVER:
                    sname = 'default'
                else:
                    sname = id

                # Default max cycle length is 1200 seconds (20m)
                self.expect(SCHED, {'state': 'scheduling'}, op=NE, id=sname,
                            interval=1, max_attempts=1200,
                            trigger_sched_cycle=False)
        return rc

    def sigjob(self, jobid=None, signal=None, extend=None, runas=None,
               logerr=True):
        """
        Send a signal to a job. Raises ``PbsSignalError`` on error.
        :param jobid: identifier of the job or list of jobs to send
                      the signal to
        :type jobid: str or list
        :param signal: The signal to send to the job, see pbs_ifl.h
        :type signal: str or None
        :param extend: extend options
        :param runas: run as user
        :type runas: str or None
        :param logerr: If True (default) logs run_cmd errors
        :type logerr: bool
        :raises: PbsSignalError
        """

        prefix = 'signal on ' + self.shortname
        if runas is not None:
            prefix += ' as ' + str(runas)
        prefix += ': '
        if jobid is not None:
            if not isinstance(jobid, list):
                jobid = jobid.split(',')
            prefix += ', '.join(jobid)
        if signal is not None:
            prefix += ' with signal = ' + str(signal)
        self.logger.info(prefix)

        c = None
        if self.get_op_mode() == PTL_CLI:
            pcmd = [os.path.join(self.client_conf['PBS_EXEC'], 'bin', 'qsig')]
            if signal is not None:
                pcmd += ['-s']
                if signal != PTL_NOARG:
                    pcmd += [str(signal)]
            if jobid is not None:
                pcmd += jobid
            if not self.default_client_pbs_conf:
                pcmd = ['PBS_CONF_FILE=' + self.client_pbs_conf_file] + pcmd
                as_script = True
            else:
                as_script = False
            ret = self.du.run_cmd(self.client, pcmd, runas=runas,
                                  as_script=as_script, level=logging.INFOCLI,
                                  logerr=logerr)
            rc = ret['rc']
            if ret['err'] != ['']:
                self.last_error = ret['err']
            self.last_rc = rc
        elif runas is not None:
            rc = self.pbs_api_as('sigjob', jobid, runas, data=signal)
        else:
            c = self._connect(self.hostname)
            rc = 0
            for ajob in jobid:
                tmp_rc = pbs_sigjob(c, ajob, signal, extend)
                if tmp_rc != 0:
                    rc = tmp_rc
        if rc != 0:
            raise PbsSignalError(rc=rc, rv=False, msg=self.geterrmsg(),
                                 post=self._disconnect, conn=c)

        if c:
            self._disconnect(c)

        return rc

    def msgjob(self, jobid=None, to_file=None, msg=None, extend=None,
               runas=None, logerr=True):
        """
        Send a message to a job. Raises ``PbsMessageError`` on
        error.
        :param jobid: identifier of the job or list of jobs to
                      send the message to
        :type jobid: str or List
        :param msg: The message to send to the job
        :type msg: str or None
        :param to_file: one of ``MSG_ERR`` or ``MSG_OUT`` or
                        ``MSG_ERR|MSG_OUT``
        :type to_file: str or None
        :param extend: extend options
        :param runas: run as user
        :type runas: str or None
        :param logerr: If True (default) logs run_cmd errors
        :type logerr: bool
        :raises: PbsMessageError
        """
        prefix = 'msgjob on ' + self.shortname
        if runas is not None:
            prefix += ' as ' + str(runas)
        prefix += ': '
        if jobid is not None:
            if not isinstance(jobid, list):
                jobid = jobid.split(',')
            prefix += ', '.join(jobid)
        if to_file is not None:
            prefix += ' with to_file = '
            if MSG_ERR == to_file:
                prefix += 'MSG_ERR'
            elif MSG_OUT == to_file:
                prefix += 'MSG_OUT'
            elif MSG_OUT | MSG_ERR == to_file:
                prefix += 'MSG_ERR|MSG_OUT'
            else:
                prefix += str(to_file)
        if msg is not None:
            prefix += ' msg = %s' % (str(msg))
        if extend is not None:
            prefix += ' extend = %s' % (str(extend))
        self.logger.info(prefix)

        c = None
        if self.get_op_mode() == PTL_CLI:
            pcmd = [os.path.join(self.client_conf['PBS_EXEC'], 'bin', 'qmsg')]
            if to_file is not None:
                if MSG_ERR == to_file:
                    pcmd += ['-E']
                elif MSG_OUT == to_file:
                    pcmd += ['-O']
                elif MSG_OUT | MSG_ERR == to_file:
                    pcmd += ['-E', '-O']
                else:
                    pcmd += ['-' + str(to_file)]
            if msg is not None:
                pcmd += [msg]
            if jobid is not None:
                pcmd += jobid
            if not self.default_client_pbs_conf:
                pcmd = ['PBS_CONF_FILE=' + self.client_pbs_conf_file] + pcmd
                as_script = True
            else:
                as_script = False

            ret = self.du.run_cmd(self.client, pcmd, runas=runas,
                                  as_script=as_script, level=logging.INFOCLI,
                                  logerr=logerr)
            rc = ret['rc']
            if ret['err'] != ['']:
                self.last_error = ret['err']
            self.last_rc = rc
        elif runas is not None:
            data = {'msg': msg, 'to_file': to_file}
            rc = self.pbs_api_as('msgjob', jobid, runas, data=data,
                                 extend=extend)
        else:
            c = self._connect(self.hostname)
            if c < 0:
                return c
            for ajob in jobid:
                tmp_rc = pbs_msgjob(c, ajob, to_file, msg, extend)
                if tmp_rc != 0:
                    rc = tmp_rc

        if rc != 0:
            raise PbsMessageError(rc=rc, rv=False, msg=self.geterrmsg(),
                                  post=self._disconnect, conn=c)

        if c:
            self._disconnect(c)

        return rc

    def alterjob(self, jobid=None, attrib=None, extend=None, runas=None,
                 logerr=True):
        """
        Alter attributes associated to a job. Raises
        ``PbsAlterError`` on error.
        :param jobid: identifier of the job or list of jobs to
                      operate on
        :type jobid: str or list
        :param attrib: A dictionary of attributes to set
        :type attrib: dictionary
        :param extend: extend options
        :param runas: run as user
        :type runas: str or None
        :param logerr: If False, CLI commands do not log error,
                       i.e. silent mode
        :type logerr: bool
        :raises: PbsAlterError
        """
        prefix = 'alter on ' + self.shortname
        if runas is not None:
            prefix += ' as ' + str(runas)
        prefix += ': '
        if jobid is not None:
            if not isinstance(jobid, list):
                jobid = jobid.split(',')
            prefix += ', '.join(jobid)
        if attrib is not None:
            prefix += ' %s' % (str(attrib))
        self.logger.info(prefix)

        c = None
        if self.get_op_mode() == PTL_CLI:
            pcmd = [os.path.join(self.client_conf['PBS_EXEC'], 'bin',
                                 'qalter')]
            if attrib is not None:
                _conf = self.default_client_pbs_conf
                pcmd += self.utils.convert_to_cli(attrib, op=IFL_ALTER,
                                                  hostname=self.client,
                                                  dflt_conf=_conf)
            if jobid is not None:
                pcmd += jobid
            if not self.default_client_pbs_conf:
                pcmd = ['PBS_CONF_FILE=' + self.client_pbs_conf_file] + pcmd
                as_script = True
            else:
                as_script = False
            ret = self.du.run_cmd(self.client, pcmd, runas=runas,
                                  as_script=as_script, level=logging.INFOCLI,
                                  logerr=logerr)
            rc = ret['rc']
            if ret['err'] != ['']:
                self.last_error = ret['err']
            self.last_rc = rc
        elif runas is not None:
            rc = self.pbs_api_as('alterjob', jobid, runas, data=attrib)
        else:
            c = self._connect(self.hostname)
            if c < 0:
                return c
            a = self.utils.convert_to_attrl(attrib)
            rc = 0
            for ajob in jobid:
                tmp_rc = pbs_alterjob(c, ajob, a, extend)
                if tmp_rc != 0:
                    rc = tmp_rc
        if rc != 0:
            raise PbsAlterError(rc=rc, rv=False, msg=self.geterrmsg(),
                                post=self._disconnect, conn=c)

        if c:
            self._disconnect(c)

        return rc

    def alterresv(self, resvid, attrib, extend=None, runas=None,
                  logerr=True):
        """
        Alter attributes associated to a reservation. Raises
        ``PbsResvAlterError`` on error.
        :param resvid: identifier of the reservation.
        :type resvid: str.
        :param attrib: A dictionary of attributes to set.
        :type attrib: dictionary.
        :param extend: extend options.
        :param runas: run as user.
        :type runas: str or None.
        :param logerr: If False, CLI commands do not log error,
                       i.e. silent mode.
        :type logerr: bool.
        :raises: PbsResvAlterError.
        """
        prefix = 'reservation alter on ' + self.shortname
        if runas is not None:
            prefix += ' as ' + str(runas)
        prefix += ': ' + resvid

        if attrib is not None:
            prefix += ' %s' % (str(attrib))
        self.logger.info(prefix)

        c = None
        resvid = resvid.split()
        if self.get_op_mode() == PTL_CLI:
            pcmd = [os.path.join(self.client_conf['PBS_EXEC'], 'bin',
                                 'pbs_ralter')]
            if attrib is not None:
                if extend is not None:
                    attrib['extend'] = extend
                _conf = self.default_client_pbs_conf
                pcmd += self.utils.convert_to_cli(attrib, op=IFL_RALTER,
                                                  hostname=self.client,
                                                  dflt_conf=_conf)
            pcmd += resvid
            if not self.default_client_pbs_conf:
                pcmd = ['PBS_CONF_FILE=' + self.client_pbs_conf_file] + pcmd
                as_script = True
            else:
                as_script = False
            ret = self.du.run_cmd(self.client, pcmd, runas=runas,
                                  as_script=as_script, level=logging.INFOCLI,
                                  logerr=logerr)
            rc = ret['rc']
            if ret['err'] != ['']:
                self.last_error = ret['err']
            if ret['out'] != ['']:
                self.last_out = ret['out']
            self.last_rc = rc
        elif runas is not None:
            rc = self.pbs_api_as('alterresv', resvid, runas, data=attrib,
                                 extend=extend)
        else:
            c = self._connect(self.hostname)
            if c < 0:
                return c
            a = self.utils.convert_to_attrl(attrib)
            rc = pbs_modify_resv(c, resvid, a, extend)

        if rc != 0:
            raise PbsResvAlterError(rc=rc, rv=False, msg=self.geterrmsg(),
                                    post=self._disconnect, conn=c)
        else:
            return rc

        if c:
            self._disconnect(c)

    def holdjob(self, jobid=None, holdtype=None, extend=None, runas=None,
                logerr=True):
        """
        Hold a job. Raises ``PbsHoldError`` on error.
        :param jobid: identifier of the job or list of jobs to hold
        :type jobid: str or list
        :param holdtype: The type of hold to put on the job
        :type holdtype: str or None
        :param extend: extend options
        :param runas: run as user
        :type runas: str or None
        :param logerr: If True (default) logs run_cmd errors
        :type logerr: bool
        :raises: PbsHoldError
        """
        prefix = 'holdjob on ' + self.shortname
        if runas is not None:
            prefix += ' as ' + str(runas)
        prefix += ': '
        if jobid is not None:
            if not isinstance(jobid, list):
                jobid = jobid.split(',')
            prefix += ', '.join(jobid)
        if holdtype is not None:
            prefix += ' with hold_list = %s' % (holdtype)
        self.logger.info(prefix)

        c = None
        if self.get_op_mode() == PTL_CLI:
            pcmd = [os.path.join(self.client_conf['PBS_EXEC'], 'bin', 'qhold')]
            if holdtype is not None:
                pcmd += ['-h']
                if holdtype != PTL_NOARG:
                    pcmd += [holdtype]
            if jobid is not None:
                pcmd += jobid
            if not self.default_client_pbs_conf:
                pcmd = ['PBS_CONF_FILE=' + self.client_pbs_conf_file] + pcmd
                as_script = True
            else:
                as_script = False
            ret = self.du.run_cmd(self.client, pcmd, runas=runas,
                                  logerr=logerr, as_script=as_script,
                                  level=logging.INFOCLI)
            rc = ret['rc']
            if ret['err'] != ['']:
                self.last_error = ret['err']
            self.last_rc = rc
        elif runas is not None:
            rc = self.pbs_api_as('holdjob', jobid, runas, data=holdtype,
                                 logerr=logerr)
        else:
            c = self._connect(self.hostname)
            if c < 0:
                return c
            rc = 0
            for ajob in jobid:
                tmp_rc = pbs_holdjob(c, ajob, holdtype, extend)
                if tmp_rc != 0:
                    rc = tmp_rc
        if rc != 0:
            raise PbsHoldError(rc=rc, rv=False, msg=self.geterrmsg(),
                               post=self._disconnect, conn=c)

        if c:
            self._disconnect(c)

        return rc

    def rlsjob(self, jobid, holdtype, extend=None, runas=None, logerr=True):
        """
        Release a job. Raises ``PbsReleaseError`` on error.
        :param jobid: job or list of jobs to release
        :type jobid: str or list
        :param holdtype: The type of hold to release on the job
        :type holdtype: str
        :param extend: extend options
        :param runas: run as user
        :type runas: str or None
        :param logerr: If True (default) logs run_cmd errors
        :type logerr: bool
        :raises: PbsReleaseError
        """
        prefix = 'release on ' + self.shortname
        if runas is not None:
            prefix += ' as ' + str(runas)
        prefix += ': '
        if jobid is not None:
            if not isinstance(jobid, list):
                jobid = jobid.split(',')
            prefix += ', '.join(jobid)
        if holdtype is not None:
            prefix += ' with hold_list = %s' % (holdtype)
        self.logger.info(prefix)

        c = None
        if self.get_op_mode() == PTL_CLI:
            pcmd = [os.path.join(self.client_conf['PBS_EXEC'], 'bin', 'qrls')]
            if holdtype is not None:
                pcmd += ['-h']
                if holdtype != PTL_NOARG:
                    pcmd += [holdtype]
            if jobid is not None:
                pcmd += jobid
            if not self.default_client_pbs_conf:
                pcmd = ['PBS_CONF_FILE=' + self.client_pbs_conf_file] + pcmd
                as_script = True
            else:
                as_script = False
            ret = self.du.run_cmd(self.client, pcmd, runas=runas,
                                  as_script=as_script, level=logging.INFOCLI,
                                  logerr=logerr)
            rc = ret['rc']
            if ret['err'] != ['']:
                self.last_error = ret['err']
            self.last_rc = rc
        elif runas is not None:
            rc = self.pbs_api_as('rlsjob', jobid, runas, data=holdtype)
        else:
            c = self._connect(self.hostname)
            if c < 0:
                return c
            rc = 0
            for ajob in jobid:
                tmp_rc = pbs_rlsjob(c, ajob, holdtype, extend)
                if tmp_rc != 0:
                    rc = tmp_rc
        if rc != 0:
            raise PbsHoldError(rc=rc, rv=False, msg=self.geterrmsg(),
                               post=self._disconnect, conn=c)

        if c:
            self._disconnect(c)

        return rc

    def rerunjob(self, jobid=None, extend=None, runas=None, logerr=True):
        """
        Rerun a job. Raises ``PbsRerunError`` on error.
        :param jobid: job or list of jobs to release
        :type jobid: str or list
        :param extend: extend options
        :param runas: run as user
        :type runas: str or None
        :param logerr: If True (default) logs run_cmd errors
        :type logerr: bool
        :raises: PbsRerunError
        """
        prefix = 'rerun on ' + self.shortname
        if runas is not None:
            prefix += ' as ' + str(runas)
        prefix += ': '
        if jobid is not None:
            if not isinstance(jobid, list):
                jobid = jobid.split(',')
            prefix += ', '.join(jobid)
        if extend is not None:
            prefix += extend
        self.logger.info(prefix)

        c = None
        if self.get_op_mode() == PTL_CLI:
            pcmd = [os.path.join(self.client_conf['PBS_EXEC'], 'bin',
                                 'qrerun')]
            if extend:
                pcmd += ['-W', extend]
            if jobid is not None:
                pcmd += jobid
            if not self.default_client_pbs_conf:
                pcmd = ['PBS_CONF_FILE=' + self.client_pbs_conf_file] + pcmd
                as_script = True
            else:
                as_script = False
            ret = self.du.run_cmd(self.client, pcmd, runas=runas,
                                  as_script=as_script, level=logging.INFOCLI,
                                  logerr=logerr)
            rc = ret['rc']
            if ret['err'] != ['']:
                self.last_error = ret['err']
            self.last_rc = rc
        elif runas is not None:
            rc = self.pbs_api_as('rerunjob', jobid, runas, extend=extend)
        else:
            c = self._connect(self.hostname)
            if c < 0:
                return c
            rc = 0
            for ajob in jobid:
                tmp_rc = pbs_rerunjob(c, ajob, extend)
                if tmp_rc != 0:
                    rc = tmp_rc
        if rc != 0:
            raise PbsRerunError(rc=rc, rv=False, msg=self.geterrmsg(),
                                post=self._disconnect, conn=c)

        if c:
            self._disconnect(c)

        return rc

    def orderjob(self, jobid1=None, jobid2=None, extend=None, runas=None,
                 logerr=True):
        """
        reorder position of ``jobid1`` and ``jobid2``. Raises
        ``PbsOrderJob`` on error.
        :param jobid1: first jobid
        :type jobid1: str or None
        :param jobid2: second jobid
        :type jobid2: str or None
        :param extend: extend options
        :param runas: run as user
        :type runas: str or None
        :param logerr: If True (default) logs run_cmd errors
        :type logerr: bool
        :raises: PbsOrderJob
        """
        prefix = 'orderjob on ' + self.shortname
        if runas is not None:
            prefix += ' as ' + str(runas)
        prefix += ': '
        prefix += str(jobid1) + ', ' + str(jobid2)
        if extend is not None:
            prefix += ' ' + str(extend)
        self.logger.info(prefix)

        c = None
        if self.get_op_mode() == PTL_CLI:
            pcmd = [os.path.join(self.client_conf['PBS_EXEC'], 'bin',
                                 'qorder')]
            if jobid1 is not None:
                pcmd += [jobid1]
            if jobid2 is not None:
                pcmd += [jobid2]
            if not self.default_client_pbs_conf:
                pcmd = ['PBS_CONF_FILE=' + self.client_pbs_conf_file] + pcmd
                as_script = True
            else:
                as_script = False
            ret = self.du.run_cmd(self.client, pcmd, runas=runas,
                                  as_script=as_script, level=logging.INFOCLI,
                                  logerr=logerr)
            rc = ret['rc']
            if ret['err'] != ['']:
                self.last_error = ret['err']
            self.last_rc = rc
        elif runas is not None:
            rc = self.pbs_api_as('orderjob', jobid1, runas, data=jobid2,
                                 extend=extend)
        else:
            c = self._connect(self.hostname)
            if c < 0:
                return c
            rc = pbs_orderjob(c, jobid1, jobid2, extend)
        if rc != 0:
            raise PbsOrderError(rc=rc, rv=False, msg=self.geterrmsg(),
                                post=self._disconnect, conn=c)

        if c:
            self._disconnect(c)

        return rc

    def runjob(self, jobid=None, location=None, run_async=False, extend=None,
               runas=None, logerr=False):
        """
        Run a job on given nodes. Raises ``PbsRunError`` on error.
        :param jobid: job or list of jobs to run
        :type jobid: str or list
        :param location: An execvnode on which to run the job
        :type location: str or None
        :param run_async: If true the call will return immediately
                      assuming success.
        :type run_async: bool
        :param extend: extend options
        :param runas: run as user
        :type runas: str or None
        :param logerr: If True (default) logs run_cmd errors
        :type logerr: bool
        :raises: PbsRunError
        """
        if run_async:
            prefix = 'Async run on ' + self.shortname
        else:
            prefix = 'run on ' + self.shortname
        if runas is not None:
            prefix += ' as ' + str(runas)
        prefix += ': '
        if jobid is not None:
            if not isinstance(jobid, list):
                jobid = jobid.split(',')
            prefix += ', '.join(jobid)
        if location is not None:
            prefix += ' with location = %s' % (location)
        self.logger.info(prefix)

        if self.has_snap:
            return 0

        c = None
        if self.get_op_mode() == PTL_CLI:
            pcmd = [os.path.join(self.client_conf['PBS_EXEC'], 'bin', 'qrun')]
            if run_async:
                pcmd += ['-a']
            if location is not None:
                pcmd += ['-H']
                if location != PTL_NOARG:
                    pcmd += [location]
            if jobid:
                pcmd += jobid
            if not self.default_client_pbs_conf:
                pcmd = ['PBS_CONF_FILE=' + self.client_pbs_conf_file] + pcmd
                as_script = True
            else:
                as_script = False
            ret = self.du.run_cmd(self.client, pcmd, runas=runas,
                                  as_script=as_script, level=logging.INFOCLI,
                                  logerr=logerr)
            rc = ret['rc']
            if ret['err'] != ['']:
                self.last_error = ret['err']
            self.last_rc = rc
        elif runas is not None:
            rc = self.pbs_api_as(
                'runjob', jobid, runas, data=location, extend=extend)
        else:
            c = self._connect(self.hostname)
            if c < 0:
                return c
            rc = 0
            for ajob in jobid:
                if run_async:
                    tmp_rc = pbs_asyrunjob(c, ajob, location, extend)
                else:
                    tmp_rc = pbs_runjob(c, ajob, location, extend)
                if tmp_rc != 0:
                    rc = tmp_rc
        if rc != 0:
            raise PbsRunError(rc=rc, rv=False, msg=self.geterrmsg(),
                              post=self._disconnect, conn=c)

        if c:
            self._disconnect(c)

        return rc

    def movejob(self, jobid=None, destination=None, extend=None, runas=None,
                logerr=True):
        """
        Move a job or list of job ids to a given destination queue.
        Raises ``PbsMoveError`` on error.
        :param jobid: A job or list of job ids to move
        :type jobid: str or list
        :param destination: The destination queue@server
        :type destination: str or None
        :param extend: extend options
        :param runas: run as user
        :type runas: str or None
        :param logerr: If True (default) logs run_cmd errors
        :type logerr: bool
        :raises: PbsMoveError
        """
        prefix = 'movejob on ' + self.shortname
        if runas is not None:
            prefix += ' as ' + str(runas)
        prefix += ': '
        if jobid is not None:
            if not isinstance(jobid, list):
                jobid = jobid.split(',')
            prefix += ', '.join(jobid)
        if destination is not None:
            prefix += ' destination = %s' % (destination)
        self.logger.info(prefix)

        c = None
        rc = 0

        if self.get_op_mode() == PTL_CLI:
            pcmd = [os.path.join(self.client_conf['PBS_EXEC'], 'bin', 'qmove')]
            if destination is not None:
                pcmd += [destination]
            if jobid is not None:
                pcmd += jobid
            if not self.default_client_pbs_conf:
                pcmd = ['PBS_CONF_FILE=' + self.client_pbs_conf_file] + pcmd
                as_script = True
            else:
                as_script = False

            ret = self.du.run_cmd(self.client, pcmd, runas=runas,
                                  logerr=logerr, as_script=as_script,
                                  level=logging.INFOCLI)
            rc = ret['rc']
            if ret['err'] != ['']:
                self.last_error = ret['err']
            self.last_rc = rc
        elif runas is not None:
            rc = self.pbs_api_as('movejob', jobid, runas, data=destination,
                                 extend=extend)
        else:
            c = self._connect(self.hostname)
            if c < 0:
                return c
            for ajob in jobid:
                tmp_rc = pbs_movejob(c, ajob, destination, extend)
                if tmp_rc != 0:
                    rc = tmp_rc

        if rc != 0:
            raise PbsMoveError(rc=rc, rv=False, msg=self.geterrmsg(),
                               post=self._disconnect, conn=c)

        if c:
            self._disconnect(c)

        return rc

    def qterm(self, manner=None, extend=None, server_name=None, runas=None,
              logerr=True):
        """
        Terminate the ``pbs_server`` daemon
        :param manner: one of ``(SHUT_IMMEDIATE | SHUT_DELAY |
                       SHUT_QUICK)`` and can be\
                       combined with SHUT_WHO_SCHED, SHUT_WHO_MOM,
                       SHUT_WHO_SECDRY, \
                       SHUT_WHO_IDLESECDRY, SHUT_WHO_SECDONLY. \
        :param extend: extend options
        :param server_name: name of the pbs server
        :type server_name: str or None
        :param runas: run as user
        :type runas: str or None
        :param logerr: If True (default) logs run_cmd errors
        :type logerr: bool
        :raises: PbsQtermError
        """
        prefix = 'terminate ' + self.shortname
        if runas is not None:
            prefix += ' as ' + str(runas)
        prefix += ': with manner '
        attrs = manner
        if attrs is None:
            prefix += "None "
        elif isinstance(attrs, str):
            prefix += attrs
        else:
            if ((attrs & SHUT_QUICK) == SHUT_QUICK):
                prefix += "quick "
            if ((attrs & SHUT_IMMEDIATE) == SHUT_IMMEDIATE):
                prefix += "immediate "
            if ((attrs & SHUT_DELAY) == SHUT_DELAY):
                prefix += "delay "
            if ((attrs & SHUT_WHO_SCHED) == SHUT_WHO_SCHED):
                prefix += "schedular "
            if ((attrs & SHUT_WHO_MOM) == SHUT_WHO_MOM):
                prefix += "mom "
            if ((attrs & SHUT_WHO_SECDRY) == SHUT_WHO_SECDRY):
                prefix += "secondary server "
            if ((attrs & SHUT_WHO_IDLESECDRY) == SHUT_WHO_IDLESECDRY):
                prefix += "idle secondary "
            if ((attrs & SHUT_WHO_SECDONLY) == SHUT_WHO_SECDONLY):
                prefix += "shoutdown secondary only "

        self.logger.info(prefix)

        if self.has_snap:
            return 0

        c = None
        rc = 0

        if self.get_op_mode() == PTL_CLI:
            pcmd = [os.path.join(self.client_conf['PBS_EXEC'], 'bin', 'qterm')]
            _conf = self.default_client_pbs_conf
            pcmd += self.utils.convert_to_cli(manner, op=IFL_TERMINATE,
                                              hostname=self.hostname,
                                              dflt_conf=_conf)
            if server_name is not None:
                pcmd += [server_name]

            if not self.default_client_pbs_conf:
                pcmd = ['PBS_CONF_FILE=' + self.client_pbs_conf_file] + pcmd
                as_script = True
            else:
                as_script = False

            ret = self.du.run_cmd(self.client, pcmd, runas=runas,
                                  level=logging.INFOCLI, as_script=as_script)
            rc = ret['rc']
            if ret['err'] != ['']:
                self.last_error = ret['err']
            self.last_rc = rc
        elif runas is not None:
            attrs = {'manner': manner, 'server_name': server_name}
            rc = self.pbs_api_as('terminate', None, runas, data=attrs,
                                 extend=extend)
        else:
            if server_name is None:
                server_name = self.hostname
            c = self._connect(self.hostname)
            rc = pbs_terminate(c, manner, extend)
        if rc != 0:
            raise PbsQtermError(rc=rc, rv=False, msg=self.geterrmsg(),
                                post=self._disconnect, conn=c, force=True)

        if c:
            self._disconnect(c, force=True)

        return rc
    teminate = qterm

    def geterrmsg(self):
        """
        Get the error message
        """
        mode = self.get_op_mode()
        if mode == PTL_CLI:
            return self.last_error
        elif self._conn is not None and self._conn >= 0:
            m = pbs_geterrmsg(self._conn)
            if m is not None:
                m = m.split('\n')
            return m
