##########################################################################
# If not stated otherwise in this file or this component's Licenses.txt
# file the following copyright and licenses apply:
#
# Copyright 2016 RDK Management
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
##########################################################################
AM_CFLAGS = -D_ANSC_LINUX
AM_CFLAGS += -D_ANSC_USER
AM_CFLAGS += -D_ANSC_LITTLE_ENDIAN_
AM_LDFLAGS = -lsysevent
AM_LDFLAGS += -lsyscfg
AM_CFLAGS +=  -I=/usr/include/ccsp

AM_CPPFLAGS = -Wall -Werror
ACLOCAL_AMFLAGS = -I m4
hardware_platform = i686-linux-gnu
bin_PROGRAMS = rdkbPowerMgr
rdkbPowerMgr_CPPFLAGS =  $(CPPFLAGS) -I$(srcdir)/include -I${PKG_CONFIG_SYSROOT_DIR}/$(includedir)/ruli/
rdkbPowerMgr_SOURCES = pwrMgr.c
rdkbPowerMgr_LDFLAGS = -lsysevent -lsyscfg -lccsp_common -lhal_mta -pthread -lsecure_wrapper

