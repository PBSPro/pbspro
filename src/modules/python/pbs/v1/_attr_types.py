# coding: utf-8
"""

/* 
#  Copyright (C) 1994-2016 Altair Engineering, Inc.
#  For more information, contact Altair at www.altair.com.
#   
#  This file is part of the PBS Professional ("PBS Pro") software.
#  
#  Open Source License Information:
#   
#  PBS Pro is free software. You can redistribute it and/or modify it under the
#  terms of the GNU Affero General Public License as published by the Free 
#  Software Foundation, either version 3 of the License, or (at your option) any 
#  later version.
#   
#  PBS Pro is distributed in the hope that it will be useful, but WITHOUT ANY 
#  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
#  PARTICULAR PURPOSE.  See the GNU Affero General Public License for more details.
#   
#  You should have received a copy of the GNU Affero General Public License along 
#  with this program.  If not, see <http://www.gnu.org/licenses/>.
#   
#  Commercial License Information: 
#  
#  The PBS Pro software is licensed under the terms of the GNU Affero General 
#  Public License agreement ("AGPL"), except where a separate commercial license 
#  agreement for PBS Pro version 14 or later has been executed in writing with Altair.
#   
#  Altair’s dual-license business model allows companies, individuals, and 
#  organizations to create proprietary derivative works of PBS Pro and distribute 
#  them - whether embedded or bundled with other software - under a commercial 
#  license agreement.
#  
#  Use of Altair’s trademarks, including but not limited to "PBS™", 
#  "PBS Professional®", and "PBS Pro™" and Altair’s logos is subject to Altair's 
#  trademark licensing policies.
 *
 */
"""
__doc__ = """
All Python representation of internal PBS attribute structures.
"""
# All types available when doing a 
__all__ = (
           'acl_group_enable',
           'acl_groups',
           'acl_host_enable',
           'hosts',
           'acl_user_enable',
           'enabled',
           'from_route_only',
           'max_array_size',
           'max_queuable',
           'max_running'
           )

#from _pbs_v1 import _attr
_attr = object
#                            (BEGIN) QUEUE  ATTRIBUTES 

class acl_group_enable(_attr):
    pass

class acl_groups(_attr):
    pass

class acl_host_enable(_attr):
    pass

class hosts(_attr):
    pass

class acl_user_enable(_attr):
    pass

class acl_users(_attr):
    pass

class enabled(_attr):
    pass

class from_route_only(_attr):
    pass

class max_array_size(_attr):
    pass

class max_queuable(_attr):
    pass

class max_running(_attr):
    pass

class node_group_key(_attr):
    pass

class Priority(_attr):
    pass





#                            (END) QUEUE  ATTRIBUTES 

