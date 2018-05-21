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

#ifndef __PDP_MAIN_H
#define __PDP_MAIN_H

#include "GC_func.h"

struct _FUNC_TO_PDP_T
{
        /*GC to PDP*/
        int (*pfnGCPauseWork)(void); 
        struct CFG_FILE_T *(* pfnGCGetCfg)(void);
        int (* pfnGCRecoveryCfg)(struct CFG_FILE_T *cfg_file);

        /*CM to PDP*/
        int (* pfnCMPauseWork)(void); 
        struct CM_SLICE_T * (* pfnCMGetSlice)(u_int value);
        spinlock_t (* pfnCMGetQueSpinlock)(void);
        struct list_head *(* pfnCMGetQueue)(void); 
        int (* pfnCMRecoveryOneStripe)(struct STRIPE_T *stripe);
        int (* pfnCMRecoveryOneStCMRinfo)(struct ST_CM_RINFO_T *st_cm_rinfo);
};

struct _FUNC_FROM_PDP_T
{
        int ( *pfnSetFunc)(struct _FUNC_TO_PDP_T *pfunc_to_pdp);
        int ( *pfnCheckStatus)(void);
};

extern struct _FUNC_FROM_PDP_T *pfunc_from_pdp;
extern struct _FUNC_TO_PDP_T *pfunc_to_pdp;


#endif
