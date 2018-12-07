#
# Makefile
#
# Contact: JeCortex@yahoo.com
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
# GNU General Public License for more details.
#

obj-m += Libceph.o
Libceph-objs += libceph_client.o

all:
	$(MAKE) -C /lib/modules/$(shell uname -r)/build M=`pwd` modules

clean:
	$(MAKE) -C /lib/modules/$(shell uname -r)/build M=`pwd` clean
