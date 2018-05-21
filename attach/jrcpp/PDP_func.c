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
#include <linux/slab.h>
#include <linux/notifier.h>
#include <linux/sched.h>
#include <linux/kthread.h>

#include "PDP_func.h" 
#include "CM_func.h" 

#define POWERDOWN_NOTIFIER 0xCAC

volatile int power_down=0;
struct task_struct *power_down_tsk;
wait_queue_head_t   power_down_wqh;

//extern struct _FUNC_FROM_PDP_T *pfunc_from_pdp;
//extern struct _FUNC_TO_PDP_T *pfunc_to_pdp;

/*GC to PDP*/
extern int _pfnGCPauseWork(void); 
extern struct CFG_FILE_T *_pfnGCGetCfg(void);
extern int _pfnGCRecoveryCfg(struct CFG_FILE_T *cfg_file);
extern int _pfnCMPauseWork(void);
extern struct CM_SLICE_T *_pfnCMGetSlice(u_int value);
extern spinlock_t _pfnCMGetQueSpinlock(void);
extern struct list_head *_pfnCMGetQueue(void); 
extern int _pfnCMRecoveryOneStripe(struct STRIPE_T *stripe); 
extern int _pfnCMRecoveryOneStCMRinfo(struct ST_CM_RINFO_T *st_cm_rinfo);

int shutdown_event(struct notifier_block *nb, unsigned long event, void *msg);
static struct notifier_block powerdown_init_notifier = {
    .notifier_call = shutdown_event,
};

static RAW_NOTIFIER_HEAD(powerdown_notifier);


int power_down_message(unsigned long val, void *msg)
{
    return raw_notifier_call_chain(&powerdown_notifier,val,msg);
}
EXPORT_SYMBOL(power_down_message);

int register_power_notifier(struct notifier_block *nb)
{
    return raw_notifier_chain_register(&powerdown_notifier,nb);
}

int unregister_power_notifier(struct notifier_block *nb)
{
    return raw_notifier_chain_unregister(&powerdown_notifier,nb);
}

int shutdown_event(struct notifier_block *nb, unsigned long event, void *msg)
{
        if(POWERDOWN_NOTIFIER == event){
                power_down = 1;
                wake_up(&power_down_wqh);
                printk("power_down_message\n");
        }
        return NOTIFY_DONE;
}

#if 1
int _pfnSetFunc(struct _FUNC_TO_PDP_T *pfunc_to_pdp) 
{ 
        /*GC to PDP*/ pfunc_to_pdp->pfnGCPauseWork = _pfnGCPauseWork;
        pfunc_to_pdp->pfnGCGetCfg = _pfnGCGetCfg;
        pfunc_to_pdp->pfnGCRecoveryCfg = _pfnGCRecoveryCfg;
        /*CM to PDP*/ 
        pfunc_to_pdp->pfnCMPauseWork = _pfnCMPauseWork;
        pfunc_to_pdp->pfnCMGetSlice = _pfnCMGetSlice;
        pfunc_to_pdp->pfnCMGetQueSpinlock = _pfnCMGetQueSpinlock;
        pfunc_to_pdp->pfnCMGetQueue = _pfnCMGetQueue;
        pfunc_to_pdp->pfnCMRecoveryOneStripe = _pfnCMRecoveryOneStripe;
        pfunc_to_pdp->pfnCMRecoveryOneStCMRinfo = _pfnCMRecoveryOneStCMRinfo;
        return 0;
}

int _pfnCheckStatus(void)
{
        return 0;
}

struct _FUNC_FROM_PDP_T func_from_pdp = {
        .pfnSetFunc = _pfnSetFunc,
        .pfnCheckStatus = _pfnCheckStatus,
};

#endif

int power_down_thread(void *arg)
{
        init_waitqueue_head(&power_down_wqh);
        allow_signal(SIGINT);
        while(!kthread_should_stop()){
                wait_event_interruptible(power_down_wqh,power_down);
                power_down = 0;
                if(!kthread_should_stop()){
                        printk(KERN_EMERG "I am here in thread !\n");
                        schedule();
                }                
        }
        return 0;
}

int __init PDP_main_init(void)
{
        int ret=0;
        printk("In PDP ...\n");
        power_down_tsk = kthread_run(power_down_thread, NULL, "power_down_thread");
        if(IS_ERR(power_down_tsk)){
                printk(KERN_ERR "Can not run powerdown thread\n");
                ret = -ENOMEM;
                goto err_kthread;
        }
        register_power_notifier(&powerdown_init_notifier);
        //raw_notifier_chain_register(&powerdown_notifier,&powerdown_init_notifier);
        pfunc_to_pdp = kzalloc(sizeof(*pfunc_to_pdp), GFP_KERNEL | GFP_NOIO);
        if (!pfunc_to_pdp) {
                ret = -ENOMEM;
                goto err_pfunc_to;
        }
        pfunc_from_pdp = &func_from_pdp;

        return 0;

err_pfunc_to:
        if(power_down_tsk) {
                send_sig(SIGINT, power_down_tsk, 1);
                kthread_stop(power_down_tsk);
                power_down_tsk= NULL;
        }
err_kthread:
        return ret;
}

void __exit PDP_main_exit(void)
{
        printk("Out PDP ...\n");
        unregister_power_notifier(&powerdown_init_notifier);
        //raw_notifier_chain_unregister(&powerdown_notifier,&powerdown_init_notifier);
        if(power_down_tsk) {
	        send_sig(SIGINT, power_down_tsk, 1);
                kthread_stop(power_down_tsk);
                power_down_tsk = NULL;
        }
        if(pfunc_to_pdp){
                kfree(pfunc_to_pdp);
                pfunc_to_pdp = NULL;
        }
        return;
}

module_init(PDP_main_init);
module_exit(PDP_main_exit);
MODULE_LICENSE("GPL");
