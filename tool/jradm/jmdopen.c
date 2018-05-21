/* *  jmdopen.c interface
*
*  Copyright(C)  2018 
*  Contact: JeCortex@yahoo.com
*
*  This program is distributed in the hope that it will be useful,
*  but WITHOUT ANY WARRANTY; without even the implied warranty of
*  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
*  GNU General Public License for more details.
*/

/* Open this and check that it is an jmd device.
 * On success, return filedescriptor.
 * On failure, return -1 if it doesn't exist,
 */

#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>

#include "jradm.h"
int open_jmddev(char *dev, int report_errors)
{
	int jmdfd = open(dev, O_RDONLY);

	if (jmdfd < 0) {
		if (report_errors)
			pr_err("error opening %s: %s\n",
				dev, strerror(errno));
		return -1;
	}

	return jmdfd;
}
