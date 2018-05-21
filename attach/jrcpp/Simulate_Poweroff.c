#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h> 

#define POWERDOWN 0xCAC

extern int power_down_message(unsigned long val, void *v);
static int __init Simulate_Poweroff_init(void)
{
    power_down_message(POWERDOWN, "Simulate_Poweroff");

    return 0;
}

static void __exit Simulate_Poweroff_exit(void)
{
    printk(KERN_DEBUG "Goodbye\n");
}

module_init(Simulate_Poweroff_init);
module_exit(Simulate_Poweroff_exit);
MODULE_LICENSE("GPL");
