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
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/kthread.h>

#include "GC_func.h"
#include "PDP_func.h"

LIST_HEAD(modules_list);
struct task_struct *GC_thread_tsk;
volatile int flag = 0;

#if 1
struct CFG_FILE_T *cfg_file = NULL;
struct STRIPE_T *stripe = NULL;
struct ST_CM_RINFO_T *st_cm_rinfo = NULL;

struct _FUNC_TO_PDP_T *pfunc_to_pdp = NULL;
EXPORT_SYMBOL(pfunc_to_pdp);

struct _FUNC_FROM_PDP_T *pfunc_from_pdp = NULL;
EXPORT_SYMBOL(pfunc_from_pdp);

int _pfnGCPauseWork(void)
{
        return 0;
}
EXPORT_SYMBOL(_pfnGCPauseWork);

struct CFG_FILE_T *_pfnGCGetCfg(void)
{
        return NULL;
}
EXPORT_SYMBOL(_pfnGCGetCfg);

int _pfnGCRecoveryCfg(struct CFG_FILE_T *cfg_file)
{
        return 0;
}
EXPORT_SYMBOL(_pfnGCRecoveryCfg);

void init_GC_info(void)
{
       return;
}
#endif

int GC_do_thread(void *arg)
{
        printk("In GC do thread ...\n");
        allow_signal(SIGINT);
        while(!kthread_should_stop()){
                if(flag != 1 || NULL == pfunc_from_pdp){
                        schedule();
                }
#if 1
                if(flag == 1)
                        pfunc_from_pdp->pfnSetFunc(pfunc_to_pdp);
#endif
        }
        return 0;
}

int __init GC_main_init(void)
{
        int ret=0;
        printk("In GC ...\n");
        GC_thread_tsk = kthread_run(GC_do_thread, NULL, "GC_do_thread");
        if(IS_ERR(GC_thread_tsk)){
                printk(KERN_ERR "Can not run GC thread\n");
                ret = -ENOMEM;
                goto err_kthread;
        }
#if 1

        /*base struct info alloc and init*/
        cfg_file = kzalloc(sizeof(*cfg_file), GFP_KERNEL | GFP_NOIO);
        if (!cfg_file) {
                ret = -ENOMEM;
                goto err_cfg;
        }
        stripe = kzalloc(sizeof(*stripe), GFP_KERNEL | GFP_NOIO);
        if (!stripe) {
                ret = -ENOMEM;
                goto err_stripe;
        }
        st_cm_rinfo = kzalloc(sizeof(*st_cm_rinfo), GFP_KERNEL | GFP_NOIO);
        if (!st_cm_rinfo) {
                ret = -ENOMEM;
                goto err_st;
        }
        init_GC_info();

        /*second, wait for other modules register*/

        /*third, start threads*/
        return 0;

err_st:
        kfree(stripe);
err_stripe:
        kfree(cfg_file);
err_cfg:
        if(GC_thread_tsk) {
                send_sig(SIGINT, GC_thread_tsk, 1);
                kthread_stop(GC_thread_tsk);
                GC_thread_tsk = NULL;
        }
#endif
err_kthread:

        return ret;
}

void __exit GC_main_exit(void)
{
        printk("Out GC ...\n");
        if(GC_thread_tsk) {
	        send_sig(SIGINT, GC_thread_tsk, 1);
                kthread_stop(GC_thread_tsk);
                GC_thread_tsk = NULL;
        }
#if 1
        if(st_cm_rinfo)
                kfree(st_cm_rinfo);
        if (stripe)
                kfree(stripe);
        if (cfg_file)
                kfree(cfg_file);
#endif
        return;
}

module_init(GC_main_init);
module_exit(GC_main_exit);
MODULE_LICENSE("GPL");
