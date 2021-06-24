# coding: utf-8
"""

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


"""

__doc__ = """

This module captures all the python types representing the PBS Server objects
(server,queue,job,resv, etc.)
"""
from ._base_types import (PbsAttributeDescriptor, PbsReadOnlyDescriptor,
                          pbs_resource, pbs_bool, _LOG,
                          )
import _pbs_v1
from _pbs_v1 import (_event_accept, _event_reject,
                     _event_param_mod_allow, _event_param_mod_disallow,
                     iter_nextfunc)

from ._exc_types import *

NAS_mod = 0

try:
    if _pbs_v1.get_python_daemon_name() == "pbs_python":
        from _pbs_ifl import *
        from pbs_ifl import *
except:
    pass

# Set global hook_config_filename parameter.
hook_config_filename = None
try:
    import os

    if "PBS_HOOK_CONFIG_FILE" in os.environ:
        hook_config_filename = os.environ["PBS_HOOK_CONFIG_FILE"]
except:
    pass

# Set global pbs_conf parameter.
pbs_conf = _pbs_v1.get_pbs_conf()


#
# get_server_data_fp: returns the file object representing the
#                     hook debug data file.
def get_server_data_fp():
    data_file = _pbs_v1.get_server_data_file()
    if data_file is None:
        return None
    try:
        return open(data_file, "a+");
    except:
        _pbs_v1.logmsg(_pbs_v1.LOG_WARNING,
                       "warning: error opening debug data file %s" % data_file)
        return None

#
# get_local_nodename: returns the name of the current host as it would appear
#                      as a vnode name. This is usually the short form of the
#                      hostname.


def get_local_nodename():
    return(_pbs_v1.get_local_host_name())


#
# pbs_statobj: general-purpose function that connects to server named
#           'connect_server' or if None, use "localhost", and depending
#            on 'objtype', then performs pbs_statjob(), ps_statque(),
#            pbs_statresv(), pbs_statvnode(), or pbs_statserver(), and
#            returning results in a new object of type _job, _queue,
#            _resv, _vnode, or _server.
#            NOTE: 'filter_queue' is used for a "job" type, which means
#                  the job must be in the queue 'filter_queue' for the
#                  job object to be instantiated.
def pbs_statobj(objtype, name=None, connect_server=None, filter_queue=None):
    """
    Returns a PBS (e.g. _job, _queue, _resv, _vnode, _server) object
    that is populated with data obtained by calling PBS APIs:
    pbs_statjob(), pbs_statque(), pbs_statresv(), pbs_statvnode(),
    pbs_statserver(), using a connection handle to 'connect_server'.

    If 'objtype'  is "job", then return the _job object.
    If 'objtype'  is "queue", then return the _queue object.
    If 'objtype'  is "resv", then return the _resv object.
    If 'objtype'  is "vnode", then return the _vnode object.
    If 'objtype'  is "server", then return the _server object.

    'filter_queue' is used for a "job" type, which means
    the job must be in the queue 'filter_queue' for the
    job object to be instantiated.
    """

    _pbs_v1.set_c_mode()

    if(connect_server == None):
        con = pbs_connect("localhost")
    else:
        con = pbs_connect(connect_server)

    if con < 0:
        _pbs_v1.logmsg(_pbs_v1.LOG_DEBUG,
                       "pbs_statobj: Unable to connect to server %s"
                       % (connect_server))
        _pbs_v1.set_python_mode()
        return None

    if(objtype == "job"):
        bs = pbs_statjob(con, name, None, None)
        header_str = "pbs.server().job(%s)" % (name,)
    elif(objtype == "queue"):
        bs = pbs_statque(con, name, None, None)
        header_str = "pbs.server().queue(%s)" % (name,)
    elif(objtype == "vnode"):
        bs = pbs_statvnode(con, name, None, None)
        header_str = "pbs.server().vnode(%s)" % (name,)
    elif(objtype == "resv"):
        bs = pbs_statresv(con, name, None, None)
        header_str = "pbs.server().resv(%s)" % (name,)
    elif(objtype == "server"):
        bs = pbs_statserver(con, None, None)
        header_str = "pbs.server()"
    else:
        _pbs_v1.logmsg(_pbs_v1.LOG_DEBUG,
                       "pbs_statobj: Bad object type %s" % (objtype))
        pbs_disconnect(con)
        _pbs_v1.set_python_mode()
        return None

    server_data_fp = get_server_data_fp()

    b = bs
    obj = None
    while(b):
        if(objtype == "job"):
            obj = _job(b.name, connect_server)
        elif(objtype == "queue"):
            obj = _queue(b.name, connect_server)
        elif(objtype == "vnode"):
            obj = _vnode(b.name, connect_server)
        elif(objtype == "resv"):
            obj = _resv(b.name, connect_server)
        elif(objtype == "server"):
            obj = _server(b.name, connect_server)
        else:
            _pbs_v1.logmsg(_pbs_v1.LOG_DEBUG,
                           "pbs_statobj: Bad object type %s" % (objtype))
            pbs_disconnect(con)
            if server_data_fp:
                server_data_fp.close()
            _pbs_v1.set_python_mode()
            return None

        a = b.attribs

        while(a):
            n = a.name
            r = a.resource
            v = a.value

            if(objtype == "vnode"):
                if(n == ATTR_NODE_state):
                    v = _pbs_v1.str_to_vnode_state(v)
                elif(n == ATTR_NODE_ntype):
                    v = _pbs_v1.str_to_vnode_ntype(v)
                elif(n == ATTR_NODE_Sharing):
                    v = _pbs_v1.str_to_vnode_sharing(v)

            elif(objtype == "job"):
                if((filter_queue != None) and (n == ATTR_queue) and
                        (filter_queue != v)):
                    pbs_disconnect(con)
                    if server_data_fp:
                        server_data_fp.close()
                    _pbs_v1.set_python_mode()
                    return None
                if n == ATTR_inter or n == ATTR_block or n == ATTR_X11_port:
                    v = int(pbs_bool(v))

            if(r):
                pr = getattr(obj, n)

                # instantiate Resource_List object if not set
                if(pr == None):
                    setattr(obj, n)

                pr = getattr(obj, n)
                if (pr == None):
                    _pbs_v1.logmsg(_pbs_v1.LOG_DEBUG,
                                   "pbs_statobj: missing %s" % (n))
                    a = a.next
                    continue

                vo = getattr(pr, r)
                if(vo == None):
                    setattr(pr, r, v)
                    if server_data_fp:
                        server_data_fp.write(
                            "%s.%s[%s]=%s\n" % (header_str, n, r, v))
                else:
                    # append value...
                    # example: "select=1:ncpus=1,ncpus=1,nodect=1,place=pack"
                    vl = [vo, v]
                    setattr(pr, r, ",".join(vl))
                    if server_data_fp:
                        server_data_fp.write("%s.%s[%s]=%s\n" % (
                            header_str, n, r, ",".join(vl)))

            else:
                vo = getattr(obj, n)

                if(vo == None):
                    setattr(obj, n, v)
                    if server_data_fp:
                        server_data_fp.write("%s.%s=%s\n" % (header_str, n, v))
                else:
                    # append value
                    vl = [vo, v]
                    setattr(obj, n, ",".join(vl))
                    if server_data_fp:
                        server_data_fp.write("%s.%s=%s\n" %
                                             (header_str, n, ",".join(vl)))

            a = a.next

        b = b.next

    pbs_disconnect(con)
    if server_data_fp:
        server_data_fp.close()
    _pbs_v1.set_python_mode()
    return obj


# Allow the C implementation of hooks to call pbs_statobj function.
_pbs_v1.set_pbs_statobj(pbs_statobj)

#:------------------------------------------------------------------------
#                       JOB TYPE
#:-------------------------------------------------------------------------


class _job():
    """
    This represents a PBS job.
    """

    attributes = PbsReadOnlyDescriptor('attributes', {})
    _attributes_hook_set = {}

    def __new__(cls, value, connect_server=None):
        return object.__new__(cls)

    def __init__(self, jid, connect_server=None,
                 failed_node_list=None, node_list=None):
        """__init__"""

        self.id = jid
        self._connect_server = connect_server
        self._readonly = False
        self._rerun = False
        self._delete = False
        self._checkpointed = False
        self._msmom = False
        self._stdout_file = None
        self._stderr_file = None
        self.failed_mom_list = failed_node_list
        self.succeeded_mom_list = node_list
    #: m(__init__)

    def __str__(self):
        """String representation of the object"""

        return str(self.id)
    #: m(__str__)

    def __setattr__(self, name, value):
        if name == "_readonly":
            if _pbs_v1.in_python_mode() and \
                    hasattr(self, "_readonly") and not value:
                raise BadAttributeValueError(
                    "_readonly can only be set to True!")
        elif ((name != "_rerun") and (name != "_delete") and
              (name != "_checkpointed") and (name != "_msmom") and
              (name != "_stdout_file") and (name != "_stderr_file") and
              name not in _job.attributes):
            raise UnsetAttributeNameError(
                "job attribute '%s' not found" % (name,))

        super(_job, self).__setattr__(name, value)

        # attributes that are set in python mode will be reflected in
        # _attributes_hook_set dictionary.
        # For example,
        # _attributes_hook_set[<job object>]=['Priority', 'comment']
        # if 'comment' or 'Priority' has been assigned a value within the hook
        # script, or been unset.

        if _pbs_v1.in_python_mode():
            if self not in self._attributes_hook_set:
                self._attributes_hook_set[self] = {}
            # using a dictionary value as easier to search for keys
            self._attributes_hook_set[self].update({name: None})

    #: m(__setattr__)

    def rerun(self):
        """rerun"""
        ev_type = _pbs_v1.event().type
        if ((ev_type & _pbs_v1.MOM_EVENTS) == 0):
            raise NotImplementedError("rerun(): only for mom hooks")
        self._rerun = True
    #: m(rerun)

    def delete(self):
        """delete"""
        ev_type = _pbs_v1.event().type
        if ((ev_type & _pbs_v1.MOM_EVENTS) == 0):
            raise NotImplementedError("delete(): only for mom hooks")
        self._delete = True
    #: m(rerun)

    def is_checkpointed(self):
        """is_checkpointed"""
        return self._checkpointed
    #: m(is_checkpointed)

    def in_ms_mom(self):
        """in_ms_mom"""
        return self._msmom
    #: m(in_ms_mom)

    def stdout_file(self):
        """stdout_file"""
        return self._stdout_file
    #: m(stdout_file)

    def stderr_file(self):
        """stderr_file"""
        return self._stderr_file
    #: m(stderr_file)

    def release_nodes(self, node_list=None, keep_select=None):
        """release_nodes"""
        if ((_pbs_v1.event().type & _pbs_v1.EXECJOB_PROLOGUE) == 0 and
                (_pbs_v1.event().type & _pbs_v1.EXECJOB_LAUNCH) == 0):
            return None
        tolerate_node_failures = None
        ajob = _pbs_v1.event().job
        if hasattr(ajob, "tolerate_node_failures"):
            tolerate_node_failures = getattr(ajob, "tolerate_node_failures")
            if tolerate_node_failures not in ["job_start", "all"]:
                msg = "no nodes released as job does not " \
                      "tolerate node failures"
                _pbs_v1.logmsg(_pbs_v1.LOG_DEBUG, "%s: %s" % (ajob.id, msg))
                return ajob
        return _pbs_v1.release_nodes(self, node_list, keep_select)
    #: m(release_nodes)


_job.id = PbsAttributeDescriptor(_job, 'id', "", (str,))
_job.failed_mom_list = PbsAttributeDescriptor(
    _job, 'failed_mom_list', {}, (list,))
_job.succeeded_mom_list = PbsAttributeDescriptor(
    _job, 'succeeded_mom_list', {}, (list,))
_job._connect_server = PbsAttributeDescriptor(
    _job, '_connect_server', {}, (str,))
#: C(job)

#:------------------------------------------------------------------------
#                       VNODE TYPE
#:-------------------------------------------------------------------------


class _vnode():
    """
    This represents a PBS vnode.
    """

    attributes = PbsReadOnlyDescriptor('attributes', {})
    _attributes_hook_set = {}

    def __new__(cls, value, connect_server=None):
        return object.__new__(cls)

    def __init__(self, name, connect_server=None):
        """__init__"""

        self.name = name
        self._readonly = False
        self._connect_server = connect_server
    #: m(__init__)

    def __str__(self):
        """String representation of the object"""

        return str(self.name)
    #: m(__str__)

    def __setattr__(self, name, value):
        if name == "_readonly":
            if _pbs_v1.in_python_mode() and \
                    hasattr(self, "_readonly") and not value:
                raise BadAttributeValueError(
                    "_readonly can only be set to True!")
        elif name not in _vnode.attributes:
            raise UnsetAttributeNameError(
                "vnode attribute '%s' not found" % (name,))
        super(_vnode, self).__setattr__(name, value)

        # attributes that are set in python mode will be reflected in
        # _attributes_hook_set dictionary.
        # For example,
        # _attributes_hook_set[<vnode object>]=['Priority', 'comment']
        # if 'comment' or 'Priority' has been assigned a value within the hook
        # script, or been unset.

        if _pbs_v1.in_python_mode() and (name != "_connect_server"):
            if self not in self._attributes_hook_set:
                self._attributes_hook_set[self] = {}
            # using a dictionary value as easier to search for keys
            self._attributes_hook_set[self].update({name: None})
            _pbs_v1.mark_vnode_set(self.name, name, str(value))

    #: m(__seattr__)
    
    def extract_state_strs(self):
        """returns the string values from the state bits."""
        lst = []
        if self.state == _pbs_v1.ND_STATE_FREE:
            lst.append('ND_STATE_FREE')
        else:
            lst = [val for (mask, val) in sorted(_pbs_v1.REVERSE_NODE_STATE.items()) if self.state & mask]
        return lst

    def extract_state_ints(self):
        """returns the integer values from the state bits."""
        lst = []
        if self.state == _pbs_v1.ND_STATE_FREE:
            lst.append(_pbs_v1.ND_STATE_FREE)
        else:
            lst = [mask for (mask, val) in sorted(_pbs_v1.REVERSE_NODE_STATE.items()) if self.state & mask]
        return lst

_vnode.name = PbsAttributeDescriptor(_vnode, 'name', "", (str,))
_vnode._connect_server = PbsAttributeDescriptor(
    _vnode, '_connect_server', "", (str,))
#: C(vnode)

# This exposes pbs.vnode() to be callable in a hook script
vnode = _vnode

#:-------------------------------------------------------------------------
#                       RESERVATION TYPE
#:-------------------------------------------------------------------------


class _resv():
    """
    This represents a PBS reservation entity.
    """

    attributes = PbsReadOnlyDescriptor('attributes', {})
    _attributes_hook_set = {}
    attributes_readonly = PbsReadOnlyDescriptor('attributes_readonly',
                                                [])

    def __new__(cls, value, connect_server=None):
        return object.__new__(cls)

    def __init__(self, resvid, connect_server=None):
        """__init__"""

        self.resvid = resvid
        self._readonly = False
        self._connect_server = connect_server
    #: m(__init__)

    def __str__(self):
        """String representation of the object"""

        return str(self.resvid)
    #: m(__str__)

    def __setattr__(self, name, value):
        if (name == "_readonly"):
            if _pbs_v1.in_python_mode() and \
                    hasattr(self, "_readonly") and not value:
                raise BadAttributeValueError(
                    "_readonly can only be set to True!")
        elif name not in _resv.attributes:
            raise UnsetAttributeNameError(
                "resv attribute '%s' not found" % (name,))
        elif name in _resv.attributes_readonly and \
                _pbs_v1.in_python_mode() and \
                _pbs_v1.in_site_hook():
            # readonly under a SITE hook
            raise BadAttributeValueError(
                "resv attribute '%s' is readonly" % (name,))
        super(_resv, self).__setattr__(name, value)
        #super(_resv, self).__setattr__(name, value)

        # attributes that are set in python mode will be reflected in
        # _attributes_hook_set dictionary.
        # For example,
        # _attributes_hook_set[<resv object>]=['reserve_start', 'reserve_end']
        # if 'reserve_start' or 'reserve_end' has been assigned a value within
        # the hook script, or been unset.

        if _pbs_v1.in_python_mode():
            if self not in self._attributes_hook_set:
                self._attributes_hook_set[self] = {}
            # using a dictionary value as easier to search for keys
            self._attributes_hook_set[self].update({name: None})
    #: m(__setattr__)


#: C(resv)
_resv.resvid = PbsAttributeDescriptor(_resv, 'resvid', "", (str,))
_resv._connect_server = PbsAttributeDescriptor(
    _resv, '_connect_server', "", (str,))
#: End (resv) setting class attributes

#:-------------------------------------------------------------------------
#                       QUEUE TYPE
#:-------------------------------------------------------------------------


class _queue():
    """
    This represents a PBS queue.
    """

    attributes = PbsReadOnlyDescriptor('attributes', {})
    #name = PbsAttributeDescriptor(queue, 'name', "", (str,))

    def __init__(self, name, connect_server=None):
        """__init__"""
        #: ok, descriptor is set.
        self.name = name
        self._readonly = False
        self._connect_server = connect_server
    #: m(__init__)

    def __str__(self):
        """String representation of the object"""

        return str(self.name)
    #: m(__str__)

    def __setattr__(self, name, value):
        if (name == "_readonly"):
            if _pbs_v1.in_python_mode() and \
                    hasattr(self, "_readonly") and not value:
                raise BadAttributeValueError(
                    "_readonly can only be set to True!")
        elif name not in _queue.attributes:
            raise UnsetAttributeNameError(
                "queue attribute '%s' not found" % (name,))
        super(_queue, self).__setattr__(name, value)
    #: m(__setattr__)

    def job(self, jobid):
        """Return a job object representing jobid that belongs to queue"""

        if jobid.find(".") == -1:
            jobid = jobid + "." + _pbs_v1.get_pbs_server_name()

        if _pbs_v1.get_python_daemon_name() == "pbs_python":

            if _pbs_v1.use_static_data():
                if self._connect_server is None:
                    sn = ""
                else:
                    sn = self._connect_server

                if self.name is None:
                    qn = ""
                else:
                    qn = self.name
                return _pbs_v1.get_job_static(jobid, sn, qn)

            return pbs_statobj("job", jobid, self._connect_server,
                               self.name)
        else:
            return _pbs_v1.get_job(jobid, self.name)
    #: m(job)

    def jobs(self):
        """
            Returns an iterator that loops over the list of jobs on this queue.
        """
        return pbs_iter("jobs", "",  self.name, self._connect_server)
    #: m(jobs)

#: C(_queue)


_queue.name = PbsAttributeDescriptor(_queue, 'name', "", (str,))
_queue._connect_server = PbsAttributeDescriptor(
    _queue, '_connect_server', "", (str,))

#: End (queue) setting class attributes

#:-------------------------------------------------------------------------
#                       Server TYPE
#:-------------------------------------------------------------------------


class _server():
    """
    This represents the PBS server entity.
    """

    attributes = PbsReadOnlyDescriptor('attributes', {})

    def __init__(self, name, connect_server=None):
        """__init__"""

        self.name = name
        self._readonly = False
        self._connect_server = connect_server
    #: m(__init__)

    def __str__(self):
        """String representation of the object"""

        return str(self.name)
    #: m(__str__)

    def queue(self, qname):
        """
        queue(strQname)
            strQname -  name of a PBS queue (without the @host part) to query.

          Returns a queue object representing the queue <queue name> that is
          managed by server s.
        """
        if qname.find("@") != -1:
            raise AssertionError(
                "Got '%s', please specify a queue name only (no @)" % (qname,))

        if _pbs_v1.get_python_daemon_name() == "pbs_python":
            if _pbs_v1.use_static_data():
                if self._connect_server is None:
                    sn = ""
                else:
                    sn = self._connect_server
                return _pbs_v1.get_queue_static(qname, sn)

            return pbs_statobj("queue", qname, self._connect_server)
        else:
            return _pbs_v1.get_queue(qname)
    #: m(queue)

    def job(self, jobid):
        """
        job(strJobid)
            strJobid - PBS jobid to query.
          Returns a job object representing jobid
        """
        if jobid.find(".") == -1:
            jobid = jobid + "." + _pbs_v1.get_pbs_server_name()

        if _pbs_v1.get_python_daemon_name() == "pbs_python":
            if _pbs_v1.use_static_data():
                if self._connect_server is None:
                    sn = ""
                else:
                    sn = self._connect_server
                return _pbs_v1.get_job_static(jobid, sn, "")

            return pbs_statobj("job", jobid, self._connect_server)
        else:
            return _pbs_v1.get_job(jobid)
    #: m(job)

    def vnode(self, vname):
        """
        vnode(strVname)
            strVname - PBS vnode name to query.
          Returns a vnode object representing vname
        """
        if _pbs_v1.get_python_daemon_name() == "pbs_python":
            if _pbs_v1.use_static_data():
                if self._connect_server is None:
                    sn = ""
                else:
                    sn = self._connect_server
                return _pbs_v1.get_vnode_static(vname, sn)

            return pbs_statobj("vnode", vname, self._connect_server)
        else:
            return _pbs_v1.get_vnode(vname)
    #: m(vnode)

    def resv(self, resvid):
        """Return a resv object representing resvid"""

        if _pbs_v1.get_python_daemon_name() == "pbs_python":
            if _pbs_v1.use_static_data():
                if self._connect_server is None:
                    sn = ""
                else:
                    sn = self._connect_server
                return _pbs_v1.get_resv_static(resvid, sn)

            return pbs_statobj("resv", resvid, self._connect_server)
        else:
            return _pbs_v1.get_resv(resvid)
    #: m(resv)

    # NAS localmod 014
    if NAS_mod != None and NAS_mod != 0:
        def jobs(self, ignore_fin=None, qname=None, username=None):
            """
            Returns an iterator that loops over the list of jobs
            on this server.
            Jobs can be filtered in 3 ways:
            - if ignore_fin is an integer != 0, finished jobs are ignored
            - qname returns jobs from that queue
            - username returns jobs with that euser
            """

            return pbs_iter("jobs", "",  qname, self._connect_server,
                            ignore_fin, username)
        #: m(jobs_nas)
    else:
        def jobs(self):
            """
            Returns an iterator that loops over the list of jobs
            on this server.
            """

            return pbs_iter("jobs", "",  "", self._connect_server)
        #: m(jobs)

    def vnodes(self):
        """
        Returns an iterator that loops over the list of vnodes
        on this server.
        """

        return pbs_iter("vnodes", "",  "", self._connect_server)
    #: m(vnodes)

    def queues(self):
        """
        Returns an iterator that loops over the list of queues on this server.
        """
        return pbs_iter("queues", "",  "", self._connect_server)
    #: m(queues)

    def resvs(self):
        """
        Returns an iterator that loops over the list of reservations on this
        server.
        """
        return pbs_iter("resvs", "", "", self._connect_server)
    #: m(resvs)

    def scheduler_restart_cycle(self):
        """
        Flags the server to tell the scheduler to restart scheduling cycle
        """
        if self._connect_server == None:
            _pbs_v1.scheduler_restart_cycle(_pbs_v1.get_pbs_server_name())
        else:
            _pbs_v1.scheduler_restart_cycle(self._connect_server)

    def __setattr__(self, name, value):
        if (name == "_readonly"):
            if _pbs_v1.in_python_mode() and \
                    hasattr(self, "_readonly") and not value:
                raise BadAttributeValueError(
                    "_readonly can only be set to True!")
        elif name not in _server.attributes:
            raise UnsetAttributeNameError(
                "server attribute '%s' not found" % (name,))
        super(_server, self).__setattr__(name, value)
    #: m(__setattr__)


#: C(server)
_server.name = PbsAttributeDescriptor(_server, 'name', "", (str,))
_server._connect_server = PbsAttributeDescriptor(
    _server, '_connect_server', "", (str,))
#: End (server) setting class attributes


#
# server: this now gets invoked when pbs.server() is called.
#        if in "pbs_python" mode, would use _pbs_ifl/pbs_ifl wrapped calls for
#        querying the server for data; otherwise, use the builtin server()
#        function in a server hook.
#
def server():

    if _pbs_v1.get_python_daemon_name() == "pbs_python":

        if _pbs_v1.use_static_data():
            return _pbs_v1.get_server_static()
        connect_server = _pbs_v1.get_pbs_server_name()
        return pbs_statobj("server", None, connect_server)
    else:
        return _pbs_v1.server()
#
# reboot: this flags PBS to reboot the local host, and if reboot_cmd is
#        given, to have PBS  use 'reboot_cmd' as the reboot command to
#        execute.
#        This immediately terminates the hook script.
#


def reboot(reboot_cmd=""):

    ev_type = _pbs_v1.event().type
    if ((ev_type & _pbs_v1.MOM_EVENTS) == 0):
        raise NotImplementedError("reboot(): only for mom hooks")
    _pbs_v1.reboot(reboot_cmd)
    raise SystemExit

#:-------------------------------------------------------------------------
#                       Event TYPE
#:-------------------------------------------------------------------------


class _event():
    """
    This represents the event that the current hook is responding to.
    """
    #: the below is used for attribute type acess
    attributes = PbsReadOnlyDescriptor('attributes', {})

    def __init__(self, type, rq_user, rq_host):
        """__init__"""
        self.type = type
        self.requestor = rq_user
        self.requestor_host = rq_host
        self._readonly = False
    #: m(__init__)

    def accept(self, ecode=0):
        """
        accept([ecode])
           Terminates hook execution and causes PBS to perform the
           associated event request action. If [ecode] argument is given,
           it will be used as the value for the SystemExit exception, else
           a value of 0 is used.

           This terminates hook execution by throwing a SystemExit exception.
        """
        _event_accept()
        _event_param_mod_disallow()
        raise SystemExit(str(ecode))
    #: m(__accept__)

    def reject(self, emsg="", ecode=255):
        """
        reject([msg])
           Terminates hook execution and instructs PBS to not perform the
           associated event request action. If [msg] argument is given, it
                will be shown in the appropriate PBS daemon log, and the STDERR
           of the PBS command that caused this event to take place.
           If [ecode] argument is given, if will be used as the value for
           the SystemExit exception, else a value of 255 is used.

           This terminates hook execution by throwing a SystemExit exception.
        """
        _event_reject(emsg)
        _event_param_mod_disallow()
        raise SystemExit(str(ecode))
    #: m(__reject__)

    def __getattr__(self, key):
        if self._param.__contains__(key):
            return self._param[key]
        # did not find <key>
        raise EventIncompatibleError
    #: m(__getattr__)

    def __setattr__(self, name, value):
        if (name == "_readonly"):
            if _pbs_v1.in_python_mode() and \
                    hasattr(self, "_readonly") and not value:
                raise BadAttributeValueError(
                    "_readonly can only be set to True!")
        elif _pbs_v1.in_python_mode() and self._param.__contains__(name):
            if name == "progname" or name == "argv" or name == "env":
                self._param[name] = value
                return
            else:
                raise BadAttributeValueError(
                    "event attribute '%s' is readonly" % (name,))
        elif name not in _event.attributes:
            raise UnsetAttributeNameError(
                "event attribute '%s' not found" % (name,))
        super().__setattr__(name, value)
    #: m(__setattr__)


#: C(event)
_event.type = PbsAttributeDescriptor(_event, 'type', None, (int,))
_event.hook_name = PbsAttributeDescriptor(_event, 'hook_name', "", (str,))
_event.hook_type = PbsAttributeDescriptor(_event, 'hook_type', "", (str,))
_event.requestor = PbsAttributeDescriptor(_event, 'requestor', "", (str,))
_event.requestor_host = PbsAttributeDescriptor(
    _event, 'requestor_host', "", (str,))
_event._param = PbsAttributeDescriptor(_event, '_param', {}, (dict,))
_event.freq = PbsAttributeDescriptor(_event, 'freq', None, (int,))
#: End (event) setting class attributes

#:-------------------------------------------------------------------------
#                       PBS Iterator Type
#:-------------------------------------------------------------------------


class pbs_iter():
    """
    This represents an iterator for looping over a list of PBS objects.
    Pbs_obj_name can be: queues, jobs, resvs, vnodes.
    Pbs_filter1 is usually the <server_name> where the queues,
                 jobs, resvs, vnodes reside. A <server_name> of ""
                means the local server host.
    Pbs_filter2 can be any string that can further restrict the list
                being referenced. For example, this can be set to
                some <queue_name>, to have the iterator represents
                a list of jobs on <queue_name>@<server_name>

    connect_server Name of the pbs server to get various stats.
    """
    # NAS localmod 014
    if NAS_mod != None and NAS_mod != 0:
        """
        We add the following args:

        Pbs_ignore_fin can be used to ignore finished jobs.
        Pbs_username   tells the iterator to return only jobs with this euser.
        """

        def __init__(self, pbs_obj_name, pbs_filter1, pbs_filter2,
                     connect_server=None, pbs_ignore_fin=None,
                     pbs_username=None):

            self._caller = _pbs_v1.get_python_daemon_name()
            if self._caller == "pbs_python":

                if(connect_server == None):
                    self._connect_server = "localhost"
                    sn = ""
                else:
                    self._connect_server = connect_server
                    sn = connect_server

                self.type = pbs_obj_name
                if _pbs_v1.use_static_data():
                    if(self.type == "jobs"):
                        self.bs = iter(_pbs_v1.get_job_static("", sn, ""))
                    elif(self.type == "queues"):
                        self.bs = iter(_pbs_v1.get_queue_static("", sn))
                    elif(self.type == "vnodes"):
                        self.bs = iter(_pbs_v1.get_vnode_static("", sn))
                    elif(self.type == "resvs"):
                        self.bs = iter(_pbs_v1.get_resv_static("", sn))
                    else:
                        _pbs_v1.logmsg(_pbs_v1.LOG_DEBUG,
                                       "pbs_iter/init: Bad object iterator"
                                       " type %s"
                                       % (self.type))
                        return None
                    return

                self.con = pbs_connect(self._connect_server)
                if self.con < 0:
                    _pbs_v1.logmsg(_pbs_v1.LOG_DEBUG,
                                   "pbs_iter: Unable to connect to server %s"
                                   % (connect_server))
                    return None

                if(self.type == "jobs"):
                    _pbs_v1.logmsg(_pbs_v1.LOG_DEBUG,
                                   "pbs_iter: pbs_python mode not"
                                   " supported by NAS local mod")
                    pbs_disconnect(self.con)
                    self.con = -1
                    return None
                elif(self.type == "queues"):
                    self.bs = pbs_statque(self.con, None, None, None)
                elif(self.type == "vnodes"):
                    self.bs = pbs_statvnode(self.con, None, None, None)
                elif(self.type == "resvs"):
                    self.bs = pbs_statresv(self.con, None, None, None)
                else:
                    _pbs_v1.logmsg(_pbs_v1.LOG_DEBUG,
                                   "pbs_iter/init: Bad object iterator type %s"
                                   % (self.type))
                    pbs_disconnect(self.con)
                    self.con = -1
                    return None

            else:

                self.obj_name = pbs_obj_name
                self.filter1 = pbs_filter1
                self.filter2 = ""
                self.ignore_fin = 0
                self.filter_user = ""

                if pbs_filter2 != None:
                    self.filter2 = pbs_filter2

                if pbs_ignore_fin != None:
                    self.ignore_fin = pbs_ignore_fin

                if pbs_username != None:
                    self.filter_user = pbs_username

                # argument 1 below tells C function were inside __init__
                _pbs_v1.iter_nextfunc(
                    self, 1, pbs_obj_name, pbs_filter1, self.filter2,
                    self.ignore_fin, self.filter_user)
    else:
        def __init__(self, pbs_obj_name, pbs_filter1,
                     pbs_filter2, connect_server=None):

            self._caller = _pbs_v1.get_python_daemon_name()
            if self._caller == "pbs_python":

                if(connect_server == None):
                    self._connect_server = "localhost"
                    sn = ""
                else:
                    self._connect_server = connect_server
                    sn = connect_server

                self.type = pbs_obj_name
                if _pbs_v1.use_static_data():
                    if(self.type == "jobs"):
                        self.bs = iter(_pbs_v1.get_job_static("", sn, ""))
                    elif(self.type == "queues"):
                        self.bs = iter(_pbs_v1.get_queue_static("", sn))
                    elif(self.type == "vnodes"):
                        self.bs = iter(_pbs_v1.get_vnode_static("", sn))
                    elif(self.type == "resvs"):
                        self.bs = iter(_pbs_v1.get_resv_static("", sn))
                    else:
                        _pbs_v1.logmsg(_pbs_v1.LOG_DEBUG,
                                       "pbs_iter/init: Bad object "
                                       "iterator type %s"
                                       % (self.type))
                        return None
                    return

                self.con = pbs_connect(self._connect_server)
                if self.con < 0:
                    _pbs_v1.logmsg(_pbs_v1.LOG_DEBUG,
                                   "pbs_iter: Unable to connect to server %s"
                                   % (connect_server))
                    return None

                if(self.type == "jobs"):
                    self.bs = pbs_statjob(self.con, pbs_filter2, None, None)
                elif(self.type == "queues"):
                    self.bs = pbs_statque(self.con, None, None, None)
                elif(self.type == "vnodes"):
                    self.bs = pbs_statvnode(self.con, None, None, None)
                elif(self.type == "resvs"):
                    self.bs = pbs_statresv(self.con, None, None, None)
                else:
                    _pbs_v1.logmsg(_pbs_v1.LOG_DEBUG,
                                   "pbs_iter/init: Bad object iterator type %s"
                                   % (self.type))
                    pbs_disconnect(self.con)
                    self.con = -1
                    return None

            else:

                self.obj_name = pbs_obj_name
                self.filter1 = pbs_filter1
                self.filter2 = pbs_filter2
                # argument 1 below tells C function we're inside __init__
                _pbs_v1.iter_nextfunc(
                    self, 1, pbs_obj_name, pbs_filter1, pbs_filter2)

    def __iter__(self):
        return self

    # NAS localmod 014
    if NAS_mod != None and NAS_mod != 0:
        def __next__(self):
            if self._caller == "pbs_python":
                if not hasattr(self, "bs") or self.bs == None:
                    if not _pbs_v1.use_static_data():
                        pbs_disconnect(self.con)
                        self.con = -1
                    raise StopIteration

                if _pbs_v1.use_static_data():
                    if(self.type == "jobs"):
                        return _pbs_v1.get_job_static(next(self.bs),
                                                      self._connect_server, "")
                    elif(self.type == "queues"):
                        return _pbs_v1.get_queue_static(next(self.bs),
                                                        self._connect_server)
                    elif(self.type == "resvs"):
                        return _pbs_v1.get_resv_static(next(self.bs),
                                                       self._connect_server)
                    elif(self.type == "vnodes"):
                        return _pbs_v1.get_vnode_static(next(self.bs),
                                                        self._connect_server)
                    else:
                        _pbs_v1.logmsg(_pbs_v1.LOG_DEBUG,
                                       "pbs_iter/next: Bad object"
                                       " iterator type %s"
                                       % (self.type))
                        raise StopIteration
                    return

                b = self.bs
                job = None

                _pbs_v1.set_c_mode()
                server_data_fp = get_server_data_fp()
                if(b):
                    if(self.type == "jobs"):
                        _pbs_v1.logmsg(_pbs_v1.LOG_DEBUG,
                                       "pbs_iter/next: pbs_python mode not"
                                       " supported by NAS local mod")
                        pbs_disconnect(self.con)
                        if server_data_fp:
                            server_data_fp.close()
                        self.con = -1
                        _pbs_v1.set_python_mode()
                        raise StopIteration
                    elif(self.type == "queues"):
                        obj = _queue(b.name, self._connect_server)
                        header_str = "pbs.server().queue(%s)" % (b.name,)
                    elif(self.type == "resvs"):
                        obj = _resv(b.name, self._connect_server)
                        header_str = "pbs.server().resv(%s)" % (b.name,)
                    elif(self.type == "vnodes"):
                        obj = _vnode(b.name, self._connect_server)
                        header_str = "pbs.server().vnode(%s)" % (b.name,)
                    else:
                        _pbs_v1.logmsg(_pbs_v1.LOG_DEBUG,
                                       "pbs_iter/next: Bad object iterator "
                                       "type %s"
                                       % (self.type))
                        pbs_disconnect(self.con)
                        if server_data_fp:
                            server_data_fp.close()
                        self.con = -1
                        _pbs_v1.set_python_mode()
                        raise StopIteration

                    a = b.attribs

                    while(a):
                        n = a.name
                        r = a.resource
                        v = a.value

                        if(self.type == "vnodes"):
                            if(n == ATTR_NODE_state):
                                v = _pbs_v1.str_to_vnode_state(v)
                            elif(n == ATTR_NODE_ntype):
                                v = _pbs_v1.str_to_vnode_ntype(v)
                            elif(n == ATTR_NODE_Sharing):
                                v = _pbs_v1.str_to_vnode_sharing(v)

                        if(self.type == "jobs"):
                            if n == ATTR_inter or n == ATTR_block or \
                                    n == ATTR_X11_port:
                                v = int(pbs_bool(v))

                        if(r):
                            pr = getattr(obj, n)

                            # if resource list does not exist, then set it
                            if(pr == None):
                                setattr(obj, n)

                            pr = getattr(obj, n)
                            if (pr == None):
                                _pbs_v1.logmsg(_pbs_v1.LOG_DEBUG,
                                               "pbs_statobj: missing %s" % (n))
                                a = a.next
                                continue

                            vo = getattr(pr, r)
                            if(vo == None):
                                setattr(pr, r, v)
                                if server_data_fp:
                                    server_data_fp.write(
                                        "%s.%s[%s]=%s\n" % (header_str, n,
                                                            r, v))
                            else:
                                # append value:
                                # example: "select=1:ncpus=1,ncpus=1,nodect=1,
                                # place=pack"
                                vl = [vo, v]
                                setattr(pr, r, ",".join(vl))
                                if server_data_fp:
                                    server_data_fp.write("%s.%s[%s]=%s\n" % (
                                        header_str, n, r, ",".join(vl)))

                        else:
                            vo = getattr(obj, n)

                            if(vo == None):
                                setattr(obj, n, v)
                                if server_data_fp:
                                    server_data_fp.write(
                                        "%s.%s=%s\n" % (header_str, n, v))
                            else:
                                vl = [vo, v]
                                setattr(obj, n, ",".join(vl))
                                if server_data_fp:
                                    server_data_fp.write("%s.%s=%s\n" % (
                                        header_str, n, ",".join(vl)))

                        a = a.next

                self.bs = b.next

                if server_data_fp:
                    server_data_fp.close()
                _pbs_v1.set_python_mode()
                return obj
            else:
                # argument 0 below tells C function we're inside next
                return _pbs_v1.iter_nextfunc(self, 0, self.obj_name,
                                             self.filter1,
                                             self.filter2, self.ignore_fin,
                                             self.filter_user)
    else:
        def __next__(self):
            if self._caller == "pbs_python":
                if not hasattr(self, "bs") or self.bs == None:
                    if not _pbs_v1.use_static_data():
                        pbs_disconnect(self.con)
                        self.con = -1
                    raise StopIteration

                if _pbs_v1.use_static_data():
                    if(self.type == "jobs"):
                        return _pbs_v1.get_job_static(next(self.bs),
                                                      self._connect_server,
                                                      "")
                    elif(self.type == "queues"):
                        return _pbs_v1.get_queue_static(next(self.bs),
                                                        self._connect_server)
                    elif(self.type == "resvs"):
                        return _pbs_v1.get_resv_static(next(self.bs),
                                                       self._connect_server)
                    elif(self.type == "vnodes"):
                        return _pbs_v1.get_vnode_static(next(self.bs),
                                                        self._connect_server)
                    else:
                        _pbs_v1.logmsg(_pbs_v1.LOG_DEBUG,
                                       "pbs_iter/next: Bad object"
                                       " iterator type %s"
                                       % (self.type))
                        raise StopIteration
                    return

                b = self.bs
                job = None

                _pbs_v1.set_c_mode()

                server_data_fp = get_server_data_fp()
                if(b):
                    if(self.type == "jobs"):
                        obj = _job(b.name, self._connect_server)
                        header_str = "pbs.server().job(%s)" % (b.name,)
                    elif(self.type == "queues"):
                        obj = _queue(b.name, self._connect_server)
                        header_str = "pbs.server().queue(%s)" % (b.name,)
                    elif(self.type == "resvs"):
                        obj = _resv(b.name, self._connect_server)
                        header_str = "pbs.server().resv(%s)" % (b.name,)
                    elif(self.type == "vnodes"):
                        obj = _vnode(b.name, self._connect_server)
                        header_str = "pbs.server().vnode(%s)" % (b.name,)
                    else:
                        _pbs_v1.logmsg(_pbs_v1.LOG_DEBUG,
                                       "pbs_iter/next: Bad object"
                                       " iterator type %s"
                                       % (self.type))
                        pbs_disconnect(self.con)
                        if server_data_fp:
                            server_data_fp.close()
                        self.con = -1
                        _pbs_v1.set_python_mode()
                        raise StopIteration

                    a = b.attribs

                    while(a):
                        n = a.name
                        r = a.resource
                        v = a.value

                        if(self.type == "vnodes"):
                            if(n == ATTR_NODE_state):
                                v = _pbs_v1.str_to_vnode_state(v)
                            elif(n == ATTR_NODE_ntype):
                                v = _pbs_v1.str_to_vnode_ntype(v)
                            elif(n == ATTR_NODE_Sharing):
                                v = _pbs_v1.str_to_vnode_sharing(v)

                        if(self.type == "jobs"):
                            if n == ATTR_inter or n == ATTR_block or \
                                    n == ATTR_X11_port:
                                v = int(pbs_bool(v))

                        if(r):
                            pr = getattr(obj, n)

                            # if resource list does not exist, then set it
                            if(pr == None):
                                setattr(obj, n)

                            pr = getattr(obj, n)
                            if (pr == None):
                                _pbs_v1.logmsg(_pbs_v1.LOG_DEBUG,
                                               "pbs_statobj: missing %s" % (n))
                                a = a.__next__
                                continue

                            vo = getattr(pr, r)
                            if(vo == None):
                                setattr(pr, r, v)
                                if server_data_fp:
                                    server_data_fp.write(
                                        "%s.%s[%s]=%s\n" % (header_str, n, r,
                                                            v))
                            else:
                                # append value:
                                # example: "select=1:ncpus=1,ncpus=1,nodect=1,
                                # place=pack"
                                vl = [vo, v]
                                setattr(pr, r, ",".join(vl))
                                if server_data_fp:
                                    server_data_fp.write("%s.%s[%s]=%s\n" % (
                                        header_str, n, r, ",".join(vl)))

                        else:
                            vo = getattr(obj, n)

                            if(vo == None):
                                setattr(obj, n, v)
                                if server_data_fp:
                                    server_data_fp.write(
                                        "%s.%s=%s\n" % (header_str, n, v))
                            else:
                                vl = [vo, v]
                                setattr(obj, n, ",".join(vl))
                                if server_data_fp:
                                    server_data_fp.write("%s.%s=%s\n" % (
                                        header_str, n, ",".join(vl)))

                        a = a.next

                self.bs = b.next

                _pbs_v1.set_python_mode()
                if server_data_fp:
                    server_data_fp.close()
                return obj
            else:
                # argument 0 below tells C function we're inside next
                return _pbs_v1.iter_nextfunc(self, 0, self.obj_name,
                                             self.filter1, self.filter2)
#: C(pbs_iter)

#:------------------------------------------------------------------------
#                  SERVER ATTRIBUTE TYPE
#:-------------------------------------------------------------------------
class _server_attribute:
    """
    This represents a external form of attributes..
    """
    attributes = PbsReadOnlyDescriptor('attributes', {})
    _attributes_hook_set = {}
    def __init__(self, name, resource, value, op, flags):
        self.name = name
        self.resource = resource
        self.value = value
        self.op = op
        self.flags = flags
        self.sisters = []
    #: m(__init__)

    def __str__(self):
        return "name=%s:resource=%s:value=%s:op=%s:flags=%s:sisters=%s" % self.tup()
    #: m(__str__)

    def __setattr__(self, name, value):
        if _pbs_v1.in_python_mode():
            raise BadAttributeValueError(
                "'%s' attribute in the server_attribute object is readonly" % (name,))
        super().__setattr__(name, value)
    #: m(__setattr__)

    def extract_flags_str(self):
        """returns the string values from the attribute flags."""
        lst = []
        for mask, value in _pbs_v1.REVERSE_ATR_VFLAGS.items():
            if self.flags & mask:
                lst.append(value)
        return lst
    #: m(extract_flags_str)

    def extract_flags_int(self):
        """returns the integer values from the attribute flags."""
        lst = []
        for mask, value in _pbs_v1.REVERSE_ATR_VFLAGS.items():
            if self.flags & mask:
                lst.append(mask)
        return lst
    #: m(extract_flags_int)

    def tup(self):
        return self.name, self.resource, self.value, self.op, self.flags, self.sisters
    #: m(tup)

_server_attribute._connect_server = PbsAttributeDescriptor(
    _server_attribute, '_connect_server', "", (str,))
#: C(_server_attribute)

# This exposes pbs.server_attribute() to be callable in a hook script
server_attribute = _server_attribute

#:------------------------------------------------------------------------
#                  MANAGEMENT TYPE
#:-------------------------------------------------------------------------
class _management:
    """
    This represents a management operation.
    """
    attributes = PbsReadOnlyDescriptor('attributes', {})
    _attributes_hook_set = {}

    def __init__(self, cmd, objtype, objname, request_time, reply_code,
        reply_auxcode, reply_choice, reply_text,
        attribs, connect_server=None):
        """__init__"""
        self.cmd = cmd
        self.objtype = objtype
        self.objname = objname
        self.request_time = request_time
        self.reply_code = reply_code
        self.reply_auxcode = reply_auxcode
        self.reply_choice = reply_choice
        self.reply_text = reply_text
        self.attribs = attribs
        self._readonly = True
        self._connect_server = connect_server
    #: m(__init__)

    def __str__(self):
        """String representation of the object"""
        return "%s:%s:%s" % (
            _pbs_v1.REVERSE_MGR_CMDS.get(self.cmd, self.cmd),
            _pbs_v1.REVERSE_MGR_OBJS.get(self.objtype, self.objtype),
            self.objname
            )
    #: m(__str__)

    def __setattr__(self, name, value):
        if _pbs_v1.in_python_mode():
            raise BadAttributeValueError(
                "'%s' attribute in the management object is readonly" % (name,))
        super().__setattr__(name, value)
    #: m(__setattr__)

_management.cmd = PbsAttributeDescriptor(_management, 'cmd', None, (int,))
_management.objtype = PbsAttributeDescriptor(_management, 'objtype', None, (int,))
_management.objname = PbsAttributeDescriptor(_management, 'objname', "", (str,))
_management._connect_server = PbsAttributeDescriptor(
    _management, '_connect_server', "", (str,))
#: C(_management)

# This exposes pbs.management() to be callable in a hook script
management = _management


#:------------------------------------------------------------------------
#                  Reverse Lookup for _pv1mod_insert_int_constants
#:-------------------------------------------------------------------------
_pbs_v1.REVERSE_MGR_CMDS = {}
_pbs_v1.REVERSE_MGR_OBJS = {}
_pbs_v1.REVERSE_BRP_CHOICES = {}
_pbs_v1.REVERSE_BATCH_OPS = {}
_pbs_v1.REVERSE_ATR_VFLAGS = {}
_pbs_v1.REVERSE_NODE_STATE = {}
_pbs_v1.REVERSE_JOB_STATE = {}
_pbs_v1.REVERSE_JOB_SUBSTATE = {}
    
for key, value in _pbs_v1.__dict__.items():
    if key.startswith("MGR_CMD_"):
        _pbs_v1.REVERSE_MGR_CMDS[value] = key
    elif key.startswith("MGR_OBJ_"):
        _pbs_v1.REVERSE_MGR_OBJS[value] = key
    elif key.startswith("BRP_CHOICE_"):
        _pbs_v1.REVERSE_BRP_CHOICES[value] = key
    elif key.startswith("BATCH_OP_"):
        _pbs_v1.REVERSE_BATCH_OPS[value] = key
    elif key.startswith("ATR_VFLAG_"):
        _pbs_v1.REVERSE_ATR_VFLAGS[value] = key
    elif key.startswith("ND_STATE_"):
        _pbs_v1.REVERSE_NODE_STATE[value] = key
    elif key.startswith("JOB_STATE_"):
        _pbs_v1.REVERSE_JOB_STATE[value] = key
    elif key.startswith("JOB_SUBSTATE_"):
        _pbs_v1.REVERSE_JOB_SUBSTATE[value] = key

_pbs_v1.REVERSE_HOOK_EVENT = {
	_pbs_v1.__dict__['QUEUEJOB']: "QUEUEJOB",
	_pbs_v1.__dict__['MODIFYJOB']: "MODIFYJOB",
	_pbs_v1.__dict__['RESVSUB']: "RESVSUB",
	_pbs_v1.__dict__['MOVEJOB']: "MOVEJOB",
	_pbs_v1.__dict__['RUNJOB']: "RUNJOB",
	_pbs_v1.__dict__['ENDJOB']: "ENDJOB",
	_pbs_v1.__dict__['MANAGEMENT']: "MANAGEMENT",
	_pbs_v1.__dict__['MODIFYVNODE']: "MODIFYVNODE",
	_pbs_v1.__dict__['PROVISION']: "PROVISION",
	_pbs_v1.__dict__['RESV_END']: "RESV_END",
	_pbs_v1.__dict__['EXECJOB_BEGIN']: "EXECJOB_BEGIN",
	_pbs_v1.__dict__['EXECJOB_PROLOGUE']: "EXECJOB_PROLOGUE",
	_pbs_v1.__dict__['EXECJOB_EPILOGUE']: "EXECJOB_EPILOGUE",
	_pbs_v1.__dict__['EXECJOB_PRETERM']: "EXECJOB_PRETERM",
	_pbs_v1.__dict__['EXECJOB_END']: "EXECJOB_END",
	_pbs_v1.__dict__['EXECJOB_LAUNCH']: "EXECJOB_LAUNCH",
	_pbs_v1.__dict__['EXECHOST_PERIODIC']: "EXECHOST_PERIODIC",
	_pbs_v1.__dict__['EXECHOST_STARTUP']: "EXECHOST_STARTUP",
	_pbs_v1.__dict__['EXECJOB_ATTACH']: "EXECJOB_ATTACH",
	_pbs_v1.__dict__['EXECJOB_RESIZE']: "EXECJOB_RESIZE",
	_pbs_v1.__dict__['EXECJOB_ABORT']: "EXECJOB_ABORT",
	_pbs_v1.__dict__['EXECJOB_POSTSUSPEND']: "EXECJOB_POSTSUSPEND",
	_pbs_v1.__dict__['EXECJOB_PRERESUME']: "EXECJOB_PRERESUME",
	_pbs_v1.__dict__['MOM_EVENTS']: "MOM_EVENTS",
	_pbs_v1.__dict__['PERIODIC']: "PERIODIC",
}
