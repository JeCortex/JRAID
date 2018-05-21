/* *  JManage.c interface
*
*  Copyright(C)  2018 
*  Contact: JeCortex@yahoo.com
*
*  This program is distributed in the hope that it will be useful,
*  but WITHOUT ANY WARRANTY; without even the implied warranty of
*  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
*  GNU General Public License for more details.
*/
#include "jradm.h"
#include <stdio.h>

int JManage_run(char *devname, int fd, struct context *c)
{
	/* Run the array.  Array must already be configured
	 *  Requires >= 0.90.0
	 */
	//char nm[32], *nmp;

	//nmp = fd2devnm(fd);
	//if (!nmp) {
	//	pr_err("Cannot find %s in sysfs!!\n", devname);
	//	return 1;
	//}
	//strcpy(nm, nmp);
	//return IncrementalScan(c, nm);
        return 0;
}
