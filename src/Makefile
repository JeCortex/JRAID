# Makefile for the kernel software JRAID
#
# Copyright(C)  2017 
# Contact: JeCortex@yahoo.com
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
# GNU General Public License for more details.

jmd-mod-y 	+= jmd.o
jraid456-y 	+= jraid5.o

ifdef DEBUG
ccflags-y += -DJDEBUG
endif

ifdef JRAID
obj-$(CONFIG_BLK_DEV_MD)	+= jmd.o
obj-$(CONFIG_MD_RAID456)	+= jraid456.o
endif

all:
	$(MAKE) JRAID=1 DEBUG=1 -C /lib/modules/$(shell uname -r)/build M=`pwd` modules
clean:
	$(MAKE) JRAID=1 DEBUG=1 -C /lib/modules/$(shell uname -r)/build M=`pwd` clean
