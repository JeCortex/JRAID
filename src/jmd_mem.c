/*
 *  slab info and cache men
 *
 *  Copyright(C)  2018
 *  Contact: JeCortex@yahoo.com
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 */

#include <linux/slab.h>
#include <linux/list.h>
#include <linux/wait.h>
#include <linux/sched.h>
#include <linux/kthread.h>

#include "jmd_mem.h"

struct kmem_cache *cache_blk;
static int cache_ratio = 1;

static int release_start_line;
static int release_end_line;

LIST_HEAD(lru_cache_list_cold);		/* linked all free pages */
LIST_HEAD(lru_cache_list_hot);		/* linked all used pages */
DEFINE_SPINLOCK(lru_list_lock);

wait_queue_head_t ioblk_waitQ;
atomic_t total_blks;
atomic_t free_blks;

struct task_struct *memory_management_tsk;
wait_queue_head_t   memory_management_wq;

static unsigned long calc_total_pages(void)
{
	int i;
	unsigned long pages = 0;
	for_each_online_node(i) {
		pages += node_present_pages(i);
	}
	return pages;
}

static int need_release(void)
{
	int ratio = 100 * atomic_read(&free_blks) / atomic_read(&total_blks);
	return (ratio < release_start_line) ? 1:0;
}

int memory_management_fn(void *arg)
{
	allow_signal(SIGINT);
        printk("In %s ... \n", __func__);
	
	while (!kthread_should_stop()) {
		if(signal_pending(current))
			flush_signals(current);

		wait_event_interruptible(memory_management_wq, need_release());
		pr_debug("(%s)-(%d)\n", __func__, __LINE__);
                if (!kthread_should_stop()){
                        ;
			//reclaim_clean_blks_from_tree();
                }
	}

	return 0;
}

int jmd_cache_mem_init(void)
{
	int ret;
	unsigned long pages, i;
	struct ioblk *pos, *n;
	unsigned long flags;

	init_waitqueue_head(&ioblk_waitQ); /* wait queue head */
	cache_blk = kmem_cache_create("ioblk", sizeof(struct ioblk),
			0, 0, NULL);
	if (!cache_blk) {
		ret = -ENOMEM;
		goto err_kmem_cache;
	}

	pages = calc_total_pages();
	
	pages = pages * cache_ratio / 100;

	atomic_set(&total_blks, 0);
	atomic_set(&free_blks, 0);
	for (i = 0; i < pages; i ++) {
		struct ioblk *blk = kmem_cache_zalloc(cache_blk, 
				GFP_KERNEL);
		if (!blk){
			ret = -ENOMEM;
			goto err_alloc;
		}
		blk->page = alloc_page(GFP_KERNEL);
		if (!blk->page) {
			kmem_cache_free(cache_blk, blk);
			ret = -ENOMEM;
			goto err_alloc;
		}

		spin_lock_irqsave(&lru_list_lock, flags);
		list_add(&blk->list, &lru_cache_list_cold);
		spin_unlock_irqrestore(&lru_list_lock, flags);
		atomic_inc(&total_blks);
		atomic_inc(&free_blks);
	}
	memory_management_tsk = kthread_run(memory_management_fn, NULL,
			"memory_management\n");
	if(IS_ERR(memory_management_tsk)){
		printk(KERN_ERR "%s-%d: Failed to crate memory_management task",
				__func__, __LINE__);
		goto err_memory_management;
	}
	init_waitqueue_head(&memory_management_wq);


	return 0;
err_memory_management:
err_alloc:
	list_for_each_entry_safe(pos, n, &lru_cache_list_cold, list) {
		spin_lock_irqsave(&lru_list_lock, flags);
		list_del(&pos->list);
		spin_unlock_irqrestore(&lru_list_lock, flags);
		__free_pages(pos->page, 0);
		atomic_dec(&total_blks);
		atomic_dec(&free_blks);
		kmem_cache_free(cache_blk, pos);
	}
	kmem_cache_destroy(cache_blk);

err_kmem_cache:
	return ret;
}

void jmd_cache_mem_exit(void)
{
	struct ioblk *pos, *n;
	pr_info("%s\n", __func__);

	if (!cache_blk)
		return;
	if(memory_management_tsk){
		send_sig(SIGINT, memory_management_tsk, 1);
		kthread_stop(memory_management_tsk);
	}
	list_for_each_entry_safe(pos, n, &lru_cache_list_cold, list) {
		list_del(&pos->list);
		__free_pages(pos->page, 0);
		kmem_cache_free(cache_blk, pos);
	}

	list_for_each_entry_safe(pos, n, &lru_cache_list_hot, list) {
		list_del(&pos->list);
		__free_pages(pos->page, 0);
		kmem_cache_free(cache_blk, pos);
	}

	kmem_cache_destroy(cache_blk);
}
