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

#ifndef __CM_MAIN_H
#define __CM_MAIN_H

#include "GC_func.h"

#define PFIFO_ROOT_T
#define PSTRIPE_T
#define PST_CM_RINFO_T
#define SPINLOCK_T

struct CM_SLICE_T {
        struct hlist_head stHashRoot;
        spinlock_t hash_lock; 
        struct list_head stFifoDirty;
        spinlock_t  dirty_lock; 
        struct list_head stFifoClean;
        spinlock_t clean_lock; 
};

#endif

