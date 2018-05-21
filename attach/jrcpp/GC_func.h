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

#ifndef  __GC_MAIN_H
#define  __GC_MAIN_H

#include <linux/list.h>

#define MAX_SLICE_NUM 27
#define MAX_RG_NUM 10
#define MAX_FRAME_NUM 12
#define MAX_SLOT_NUM_PER_FRAME 8
#define MAX_TEMP_NUM 8
#define MAX_DISK_NUM_PER_RG 8
#define MAX_SG_NUM 10

#define RDC_VERSION_LEN 120
#define RDC_MAC_LEN 8
#define RDC_MAE_LEN 128
#define RDC_FREESPACE_LEN 2048

#define STATISTIC_UNM 120

struct GLOBAL_INFO_T {
};

struct SLICE_T {
};

struct RG_T {
};

struct DISK_T {
};

struct DISK_PRIVATE_T {
};

struct FUNC_FROM_ST_T {
};

struct STATISTIC_U {
};

struct _SU_T {
};

struct _CM_STRIPE_T {
};

struct STRUCT_TYPE_E {
};

struct OWNER_E {
};

struct CFG_FILE_T {
        u_int64_t       uMagic;
        struct GLOBAL_INFO_T   stGlobalInfo;
        struct SLICE_T         a_stSlice[MAX_SLICE_NUM];
        struct RG_T            a_stRg[MAX_RG_NUM];
        struct DISK_T          a_stDisk[MAX_FRAME_NUM][MAX_SLOT_NUM_PER_FRAME];
        struct DISK_PRIVATE_T  stDiskPrivate;
        char            a_version[RDC_VERSION_LEN];
        u_int32_t       a_temp[MAX_TEMP_NUM];
        char            a_Mac[RDC_MAC_LEN];
        char            a_MacSign[MAX_FRAME_NUM][MAX_SLOT_NUM_PER_FRAME][RDC_MAE_LEN];
        struct STATISTIC_U     a_unStatistic[STATISTIC_UNM];
        char            no_use[RDC_FREESPACE_LEN-MAX_TEMP_NUM*4-MAX_FRAME_NUM*
                                MAX_SLOT_NUM_PER_FRAME*RDC_MAC_LEN-RDC_MAC_LEN];
};

struct STRIPE_T {
        u_int16_t       u16SliceId; 
        u_int32_t       uStripeId; 
        u_int32_t       uFirstSuId;
        u_int64_t       u64FirstSuOffsetLba;
        u_int           uAccessTime;
        u_int           uWriteTimes;
        struct _SU_T    *_pstSu[MAX_DISK_NUM_PER_RG];
        struct hlist_node       *pstHashNode;
        struct list_head        *pstFifoNodeDirty;
        struct list_head        *pstFifoNodeClean;
        struct list_head        *pstFifoNodeDtg;
        struct _CM_STRIPE_T     stCmStripe;
};

struct ST_CM_RINFO_T {
        struct STRUCT_TYPE_E    enStructType;
        u_int8_t                u8When; 
        struct OWNER_E          enOwner;
        u_int16_t       u16SliceId;
        u_int64_t       u64OffsetLba;
        u_int32_t       uLenLba;
        char            *a_pcBuf[MAX_SG_NUM];
        u_int8_t        u8PieceNum;
        unsigned        bIsRead :1;
        unsigned        bFailed :1;
        struct FUNC_FROM_ST_T *pstFuncFromSt;
        void            *pStRinfo;
        void            *pCmRinfo;
};

#endif 
