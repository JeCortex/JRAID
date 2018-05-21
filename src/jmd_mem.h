/*
 *  slab cache 
 *
 *  Copyright(C)  2018
 *  Contact: JeCortex@yahoo.com
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 */

#ifndef __JMD_CACHE_MEM__H
#define __JMD_CACHE_MEM__H

#include <linux/slab.h>
#include <linux/list.h>
#include <linux/wait.h>
#include <asm/atomic.h>
#include <asm/barrier.h>

struct ioblk {
	struct list_head list;
	struct bio *bio;

	unsigned long private;
	struct page *page;
	int len;
	int offset;
	sector_t sector;

	unsigned long flag;
	pgoff_t	index;	

	struct work_struct work;

	wait_queue_head_t wq;
	u8          mapped;
	u8          uptodate;
	u8          dirty;
	uint64_t bi_sector;
};

enum blk_state {
	CACHE_locked,
	CACHE_dirty,
	CACHE_uptodate,
	CACHE_writeerror,
	CACHE_error,
	CACHE_readerror,
	CACHE_readahead,
	CACHE_mappedtodisk,
	CACHE_reading,
	CACHE_release,
};

#define is_blk_locked(blk)			test_bit(CACHE_locked, &(blk)->flag)
#define is_blk_dirty(blk)			test_bit(CACHE_dirty, &(blk)->flag)
#define is_blk_uptodate(blk)			test_bit(CACHE_uptodate, &(blk)->flag)
#define is_blk_error(blk)			test_bit(CACHE_error, &(blk)->flag)
#define is_blk_writeerror(blk)			test_bit(CACHE_writeerror, &(blk)->flag)
#define is_blk_readerror(blk)			test_bit(CACHE_readerror, &(blk)->flag)
#define is_blk_readahead(blk)			test_bit(CACHE_readahead, &(blk)->flag)
#define is_blk_mappedtodisk(blk)		test_bit(CACHE_mappedtodisk, &(blk)->flag)
#define is_blk_reading(blk)	        	test_bit(CACHE_reading, &(blk)->flag)
#define is_blk_release(blk)		        test_bit(CACHE_release, &(blk)->flag)


#define TestSetBlkLocked(blk)		test_and_set_bit(CACHE_locked, &(blk)->flag)
#define TestSetBlkDitry(blk)		test_and_set_bit(CACHE_dirty, &(blk)->flag)
#define TestSetBlkMirror(blk)		test_and_set_bit(CACHE_mirror, &(blk)->flag)
#define TestClearBlkLocked(blk)		test_and_clear_bit(CACHE_locked, &(blk)->flag)
#define TestClearBlkDitry(blk)		test_and_clear_bit(CACHE_dirty, &(blk)->flag)
#define TestClearBlkWriteback(blk)	test_and_clear_bit(CACHE_writeback, &(blk)->flag)
#define TestClearBlkMirror(blk)		test_and_clear_bit(CACHE_mirror, &(blk)->flag)
#define TestClearBlkError(blk)		test_and_clear_bit(CACHE_error, &(blk)->flag)
#define TestClearBlkReadError(blk)		test_and_clear_bit(CACHE_readerror, &(blk)->flag)
#define TestClearBlkWriteError(blk)		test_and_clear_bit(CACHE_writeerror, &(blk)->flag)
#define TestClearBlkReading(blk)		test_and_clear_bit(CACHE_reading, &(blk)->flag)
#define TestClearBlkRemotread(blk)		test_and_clear_bit(CACHE_remoteread, &(blk)->flag)


#define SetBlkLocked(blk)			set_bit(CACHE_locked, &(blk)->flag)
#define SetBlkDitry(blk)			set_bit(CACHE_dirty, &(blk)->flag)
#define SetBlkWriteback(blk)		set_bit(CACHE_writeback, &(blk)->flag)
#define SetBlkPrivate(blk)			set_bit(CACHE_private, &(blk)->flag)
#define SetBlkUptodate(blk)			set_bit(CACHE_uptodate, &(blk)->flag)
#define SetBlkError(blk)			set_bit(CACHE_error, &(blk)->flag)
#define SetBlkWriteError(blk)		set_bit(CACHE_writeback, &(blk)->flag)
#define SetBlkReadError(blk)		set_bit(CACHE_readerror, &(blk)->flag)
#define SetBlkReadAhead(blk)		set_bit(CACHE_readahead, &(blk)->flag)
#define SetBlkMirror(blk)			set_bit(CACHE_mirror, &(blk)->flag)
#define SetBlkMappedtodisk(blk)		set_bit(CACHE_mappedtodisk, &(blk)->flag)
#define SetBlkReading(blk)		    set_bit(CACHE_reading, &(blk)->flag)
#define SetBlkRemoteread(blk)		set_bit(CACHE_remoteread, &(blk)->flag)
#define SetBlkRelease(blk)		set_bit(CACHE_release, &(blk)->flag)
#define SetBlkSending(blk)		set_bit(CACHE_sending, &(blk)->flag)
#define SetBlkRecving(blk)		set_bit(CACHE_recving, &(blk)->flag)


#define ClearBlkUptodate(blk)		clear_bit(CACHE_uptodate, &(blk)->flag)
#define ClearBlkReadahead(blk)		clear_bit(CACHE_readahead, &(blk)->flag)
#define ClearBlkMappedToDisk(blk)	clear_bit(CACHE_mappedtodisk, &(blk)->flag)
#define ClearBlkMirror(blk)			clear_bit(CACHE_mirror, &(blk)->flag)
#define ClearBlkDirty(blk)			clear_bit(CACHE_dirty, &(blk)->flag)
#define ClearBlkLocked(blk)			clear_bit(CACHE_locked, &(blk)->flag)
#define ClearBlkReading(blk)		clear_bit(CACHE_reading, &(blk)->flag)
#define ClearBlkRemoteread(blk)		clear_bit(CACHE_remoteread, &(blk)->flag)
#define ClearBlkRelease(blk)		clear_bit(CACHE_release, &(blk)->flag)
#define ClearBlkSending(blk)		clear_bit(CACHE_sending, &(blk)->flag)
#define ClearBlkRecving(blk)		clear_bit(CACHE_recving, &(blk)->flag)

static inline wait_queue_head_t *blk_waitqueue(struct ioblk *blk)
{
	return &blk->wq;
}

static inline void wake_up_blk(struct ioblk *blk, int bit)
{
	__wake_up_bit(blk_waitqueue(blk), &blk->flag, bit);
}

static inline int sync_blk(void *arg)
{
	io_schedule();
	return 0;
}

static inline void __lock_blk(struct ioblk *blk)
{
	wait_on_bit_lock(&blk->flag, CACHE_locked, sync_blk,
			TASK_UNINTERRUPTIBLE);
}

static inline int trylock_blk(struct ioblk *blk)
{
	return likely(!test_and_set_bit_lock(CACHE_locked, &blk->flag));
}

static inline void lock_blk(struct ioblk *blk)
{
	might_sleep();
	if (!trylock_blk(blk))
		__lock_blk(blk);
}

static inline void unlock_blk(struct ioblk *blk)
{
	clear_bit_unlock(CACHE_locked, &blk->flag);
	smp_mb__after_clear_bit();
	wake_up_bit(&blk->flag, CACHE_locked);
}

int jmd_cache_mem_init(void);
void jmd_cache_mem_exit(void);

#endif
