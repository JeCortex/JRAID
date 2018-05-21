/*
*  RCP_pro init and exit
*
*  Copyright(C)  2017 GPL
*  Contact: JeCortex@yahoo.com
*
*  This program is distributed in the hope that it will be useful,
*  but WITHOUT ANY WARRANTY; without even the implied warranty of
*  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
*  GNU General Public License for more details.
*/

#include <linux/module.h>
#include <linux/init.h>
#include "RK_func.h"

int __init RK_main_init(void)
{
        printk(" In RK ... \n");
        return 0;
}

void __exit RK_main_exit(void)
{
        return;
}

module_init(RK_main_init);
module_exit(RK_main_exit);
MODULE_LICENSE("GPL");
