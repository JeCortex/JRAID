#
# Makefile 
#
# Copyright(C)  2017 GPL
# Contact: JeCortex@yahoo.com
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
# GNU General Public License for more details.


obj-m += GC.o
GC-objs += GC_func.o

obj-m += ST.o
ST-objs += ST_func.o

obj-m += CM.o
CM-objs += CM_func.o

obj-m += PDP.o
PDP-objs += PDP_func.o

obj-m += RK.o
RK-objs += RK_func.o

obj-m += SI.o
SI-objs += SI_func.o

obj-m += Simulate_Poweroff.o

KVER := /lib/modules/`uname -r`/build
PWD := `pwd`

all:
	$(MAKE) -C $(KVER) M=$(PWD) modules
clean:
	$(MAKE) -C $(KVER) M=$(PWD) clean

.PHONY: clean all
