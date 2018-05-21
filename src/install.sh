# install.sh 
#
# Copyright(C)  2017 
# Contact: JeCortex@yahoo.com
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
# GNU General Public License for more details.

#!/bin/sh

insmod jmd-mod.ko
insmod jraid456.ko

echo /dev/sdb 3 0 > /sys/block/jmd0/jmds/add
echo /dev/sdc 3 1 > /sys/block/jmd0/jmds/add
echo /dev/sdd 3 2 > /sys/block/jmd0/jmds/add

#hexdump -C -s 10737352704 -n 512 /dev/sdb
