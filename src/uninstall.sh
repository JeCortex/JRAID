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

rmmod raid456.ko
rmmod raid0
rmmod raid1
rmmod raid10
rmmod md-mod.ko

rmmod jraid456.ko
rmmod jmd-mod.ko

