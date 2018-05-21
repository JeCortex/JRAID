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
#include "CM_func.h"

#if 1

int _pfnCMPauseWork(void)
{
        return 0;
}
EXPORT_SYMBOL(_pfnCMPauseWork);

struct CM_SLICE_T *_pfnCMGetSlice(u_int value)
{
        return NULL;
}
EXPORT_SYMBOL(_pfnCMGetSlice);

spinlock_t _pfnCMGetQueSpinlock(void)
{
        spinlock_t ret;
        return ret;
}
EXPORT_SYMBOL(_pfnCMGetQueSpinlock);

struct list_head *_pfnCMGetQueue(void)
{
        return NULL;
}
EXPORT_SYMBOL(_pfnCMGetQueue);

int _pfnCMRecoveryOneStripe (struct STRIPE_T *stripe)
{
        return 0;
}
EXPORT_SYMBOL(_pfnCMRecoveryOneStripe);

int _pfnCMRecoveryOneStCMRinfo(struct ST_CM_RINFO_T *st_cm_rinfo)
{
        return 0;
}
EXPORT_SYMBOL(_pfnCMRecoveryOneStCMRinfo);
#endif

int __init CM_main_init(void)
{
        printk(" In CM ... \n");
        return 0;
}

void __exit CM_main_exit(void)
{
        return;
}

module_init(CM_main_init);
module_exit(CM_main_exit);
MODULE_LICENSE("GPL");
