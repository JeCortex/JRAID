/*
*  jmd.c interface
*
*  Copyright(C)  2017 
*  Contact: JeCortex@yahoo.com
*
*  This program is distributed in the hope that it will be useful,
*  but WITHOUT ANY WARRANTY; without even the implied warranty of
*  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
*  GNU General Public License for more details.
*/

#include <linux/init.h>
#include <linux/module.h>

#include <linux/slab.h>
#include <linux/bio.h>
#include <linux/buffer_head.h>
#include <linux/wait.h>
#include <linux/sched.h>
#include <linux/blkdev.h>

#include <linux/sched.h>

#include "jraid5.h"

#define IO_THRESHOLD 1
#define STRIPE_SIZE PAGE_SIZE
#define STRIPE_SECTORS		(STRIPE_SIZE>>9)

#define	SECTOR_SHIFT	9
#define	PAGE_SECTORS_SHIFT	(PAGE_SHIFT - SECTOR_SHIFT)
#define PAGE_SECTORS		(1 << PAGE_SECTORS_SHIFT)

#define NR_STRIPES		1024
#define HASH_PAGES		1
#define HASH_PAGES_ORDER	0
#define NR_HASH			(HASH_PAGES * PAGE_SIZE / sizeof(struct stripe_head *))
#define HASH_MASK		(NR_HASH - 1)
#define stripe_hash(conf, sect)	((conf)->stripe_hashtbl[((sect) / ((conf)->buffer_size >> 9)) & HASH_MASK])

#define r5_next_bio(bio, sect) ( ( (bio)->bi_sector + ((bio)->bi_size>>9) < sect + STRIPE_SECTORS) ? (bio)->bi_next : NULL)

#if JDEBUG
#define PRINTK(x...) printk(x)
#else
#define PRINTK(x...) do { } while (0)
#endif



static struct jmd_personality jraid5_personality;

static inline void release_stripe__(struct jraid5_conf *conf, struct stripe_head *sh)
{
	if (atomic_dec_and_test(&sh->count)) {
		if (!list_empty(&sh->lru))
			BUG();
		if (atomic_read(&conf->active_stripes)==0)
			BUG();
		list_add_tail(&sh->lru, &conf->inactive_list);
		atomic_dec(&conf->active_stripes);
		wake_up(&conf->wait_for_stripe);
	}
}

static inline void __release_stripe(struct jraid5_conf *conf, struct stripe_head *sh)
{
	if (atomic_dec_and_test(&sh->count)) {
                if (!list_empty(&sh->lru)){
                        printk("[%s]: bug for empty sh->lru\n", __func__);
			//BUG();
                }
                if (atomic_read(&conf->active_stripes)==0){
                        printk("[%s]: bug for conf->active_stripes = 0\n", __func__);
			//BUG();
                }
		if (test_bit(STRIPE_HANDLE, &sh->state)) {
			list_add_tail(&sh->lru, &conf->handle_list);
			jmd_wakeup_thread(conf->thread);
		}
		else {
			list_add_tail(&sh->lru, &conf->inactive_list);
			atomic_dec(&conf->active_stripes);
			wake_up(&conf->wait_for_stripe);
		}
	}
}

static void release_stripe(struct stripe_head *sh)
{
	struct jraid5_conf *conf = sh->raid_conf;

	spin_lock_irq(&conf->device_lock);
	__release_stripe(conf, sh);
	spin_unlock_irq(&conf->device_lock);
}

static unsigned long jraid5_compute_sector(unsigned long r_sector, unsigned int raid_disks,
			unsigned int data_disks, unsigned int * dd_idx,
			unsigned int * pd_idx, struct jraid5_conf *conf)
{
	unsigned long stripe;
	unsigned long chunk_number;
	unsigned int chunk_offset;
	unsigned long new_sector;
	int sectors_per_chunk = conf->chunk_size >> 9;

	chunk_number = r_sector / sectors_per_chunk;
	chunk_offset = r_sector % sectors_per_chunk;

	stripe = chunk_number / data_disks;

	*dd_idx = chunk_number % data_disks;

	if (conf->level == 4)
		*pd_idx = data_disks;
	else switch (conf->algorithm) {
		case ALGORITHM_LEFT_ASYMMETRIC:
			*pd_idx = data_disks - stripe % raid_disks;
			if (*dd_idx >= *pd_idx)
				(*dd_idx)++;
			break;
		case ALGORITHM_RIGHT_ASYMMETRIC:
			*pd_idx = stripe % raid_disks;
			if (*dd_idx >= *pd_idx)
				(*dd_idx)++;
			break;
		case ALGORITHM_LEFT_SYMMETRIC:
			*pd_idx = data_disks - stripe % raid_disks;
			*dd_idx = (*pd_idx + 1 + *dd_idx) % raid_disks;
			break;
		case ALGORITHM_RIGHT_SYMMETRIC:
			*pd_idx = stripe % raid_disks;
			*dd_idx = (*pd_idx + 1 + *dd_idx) % raid_disks;
			break;
		default:
			printk ("raid5: unsupported algorithm %d\n", conf->algorithm);
	}

	new_sector = stripe * sectors_per_chunk + chunk_offset;
	return new_sector;
}

static void remove_hash(struct stripe_head *sh)
{
	PRINTK("remove_hash(), stripe %lu\n", sh->sector);

	if (sh->hash_pprev) {
		if (sh->hash_next)
			sh->hash_next->hash_pprev = sh->hash_pprev;
		*sh->hash_pprev = sh->hash_next;
		sh->hash_pprev = NULL;
	}
}

static void shrink_stripe_cache(struct jraid5_conf *conf)
{
	int i;
	if (atomic_read(&conf->active_stripes))
		BUG();
	for (i=0; i < NR_HASH; i++) {
		struct stripe_head *sh;
		while ((sh = conf->stripe_hashtbl[i])) 
			remove_hash(sh);
	}
}

static struct stripe_head *__find_stripe(struct jraid5_conf *conf, unsigned long sector)
{
	struct stripe_head *sh;

	PRINTK("__find_stripe, sector %lu\n", sector);
	for (sh = stripe_hash(conf, sector); sh; sh = sh->hash_next)
		if (sh->sector == sector)
			return sh;
	PRINTK("__stripe %lu not in cache\n", sector);
	return NULL;
}

static struct stripe_head *get_free_stripe(struct jraid5_conf *conf)
{
	struct stripe_head *sh = NULL;
	struct list_head *first;

        if (list_empty(&conf->inactive_list)){
		goto out;
        }
	first = conf->inactive_list.next;
	sh = list_entry(first, struct stripe_head, lru);
	list_del_init(first);
	remove_hash(sh);
	atomic_inc(&conf->active_stripes);
out:
	return sh;
}

static void __shrink_buffers(struct stripe_head *sh, int num)
{
	struct buffer_head *bh;
	int i;

	for (i=0; i<num ; i++) {
		bh = sh->bh_cache[i];
		if (!bh)
			return;
		sh->bh_cache[i] = NULL;
		free_page((unsigned long) bh->b_data);
		kfree(bh);
	}
}

static void shrink_buffers(struct stripe_head *sh, int num)
{
        struct page *p;
	int i;

	for (i=0; i<num ; i++) {
                p = sh->dev[i].page;
                if(!p)
                        continue;
		sh->dev[i].page = NULL;
                page_cache_release(p);
	}
}

static int __grow_buffers(struct stripe_head *sh, int num, int b_size, int priority)
{
	struct buffer_head *bh;
	int i;

	for (i=0; i<num; i++) {
		struct page *page;
		bh = kmalloc(sizeof(struct buffer_head), priority);
		if (!bh)
			return 1;
		memset(bh, 0, sizeof (struct buffer_head));
		page = alloc_page(priority);
		bh->b_data = page_address(page);
		if (!bh->b_data) {
			kfree(bh);
			return 1;
		}
		atomic_set(&bh->b_count, 0);
		bh->b_page = page;
		sh->bh_cache[i] = bh;

	}
	return 0;
}

static int grow_buffers(struct stripe_head *sh, int num)
{
	int i;

	for (i=0; i<num; i++) {
		struct page *page;
		if(!(page = alloc_page(GFP_KERNEL))){
			return 1;
		}
                sh->dev[i].page = page;
	}
	return 0;
}

static int grow_stripes(struct jraid5_conf *conf, int num)
{
	struct stripe_head *sh;
        struct kmem_cache *sc;
        int devs = conf->raid_disks;

        sc = kmem_cache_create("raid5 cache", sizeof(struct stripe_head)+(devs-1)*
                              sizeof(struct r5dev), 0, 0, NULL);
        if(!sc)
                return 1;
        conf->slab_cache = sc;

	while (num--) {
		sh = kmem_cache_alloc(sc, GFP_KERNEL);
		if (!sh)
			return 1;
		memset(sh, 0, sizeof(*sh) + (devs-1)*sizeof(struct r5dev));
		sh->raid_conf = conf;
                spin_lock_init(&sh->lock);

		if (grow_buffers(sh, conf->raid_disks)) {
			shrink_buffers(sh, conf->raid_disks);
			kfree(sh);
			return 1;
		}
		/* we just created an active stripe so... */
		atomic_set(&sh->count, 1);
		atomic_inc(&conf->active_stripes);
		INIT_LIST_HEAD(&sh->lru);
		release_stripe(sh);
	}
	return 0;
}

static void shrink_stripes(struct jraid5_conf *conf, int num)
{
	struct stripe_head *sh;

	while (num--) {
		spin_lock_irq(&conf->device_lock);
		sh = get_free_stripe(conf);
		spin_unlock_irq(&conf->device_lock);
		if (!sh)
			break;
                if (atomic_read(&sh->count)){
                        printk("[%s]: bug for no zero sh->count.\n", __func__);
			//BUG();
                }
		shrink_buffers(sh, conf->jraid_disks);
		kfree(sh);
		atomic_dec(&conf->active_stripes);
	}
}

static int raid5_end_read_request (struct bio * bi, unsigned int bytes_done,
				   int error)
{
        printk("...raid5_end_read_request...\n");
 	struct stripe_head *sh = bi->bi_private;
	struct jraid5_conf *conf = sh->raid_conf;

        //conf->jmddev->tvalue = 0;
        //wake_up(&conf->jmddev->twait);

        release_stripe(sh);

        //spin_lock_irq(&conf->device_lock);
        //if(--bi->bi_phys_segments == 0){
        //        bio_endio(bi, 0);
        //}
        //spin_unlock_irq(&conf->device_lock);
	return 0;
}

static int raid5_end_write_request (struct bio *bi, unsigned int bytes_done,
				    int error)
{
        printk("...raid5_end_write_request...\n");
 	struct stripe_head *sh = bi->bi_private;
	struct jraid5_conf *conf = sh->raid_conf;
	//release_stripe__(conf, sh);
	//set_bit(STRIPE_HANDLE, &sh->state);
        //conf->jmddev->tvalue = 0;
        //wake_up(&conf->jmddev->twait);
	__release_stripe(conf, sh);

        //spin_lock_irq(&conf->device_lock);
        //printk("bi->bi_phys_segments = %u\n", bi->bi_phys_segments);
        //if(--bi->bi_phys_segments == 0){
        //      bio_endio(bi, 0);
        //}
        //spin_unlock_irq(&conf->device_lock);
	return 0;
}

static void __raid5_end_write_request (struct buffer_head *bh, int uptodate)
{
 	struct stripe_head *sh = bh->b_private;
	struct jraid5_conf *conf = sh->raid_conf;
	int disks = conf->raid_disks, i;
	unsigned long flags;

	for (i=0 ; i<disks; i++)
		if (bh == sh->bh_cache[i])
			break;

	PRINTK("end_write_request %lu/%d, count %d, uptodate: %d.\n", sh->sector, i, 
                                atomic_read(&sh->count), uptodate);
	if (i == disks) {
		BUG();
		return;
	}

	spin_lock_irqsave(&conf->device_lock, flags);
	if (!uptodate)
		jmd_error(jmddev_to_dev(conf->jmddev), bh->b_bdev);
	clear_bit(BH_Lock, &bh->b_state);
	set_bit(STRIPE_HANDLE, &sh->state);
	release_stripe__(conf, sh);
	spin_unlock_irqrestore(&conf->device_lock, flags);
}

static sector_t compute_blocknr(struct stripe_head *sh, int i)
{
	struct jraid5_conf *conf = sh->raid_conf;
	int raid_disks = conf->raid_disks;
        int data_disks = raid_disks - 1;
	sector_t new_sector = sh->sector, check;
	int sectors_per_chunk = conf->chunk_size >> 9;
	sector_t stripe;
	int chunk_offset;
	int chunk_number, dummy1, dummy2, dd_idx = i;
	sector_t r_sector;

	chunk_offset = sector_div(new_sector, sectors_per_chunk);
	stripe = new_sector;
	BUG_ON(new_sector != stripe);

	
	switch (conf->algorithm) {
		case ALGORITHM_LEFT_ASYMMETRIC:
		case ALGORITHM_RIGHT_ASYMMETRIC:
			if (i > sh->pd_idx)
				i--;
			break;
		case ALGORITHM_LEFT_SYMMETRIC:
		case ALGORITHM_RIGHT_SYMMETRIC:
			if (i < sh->pd_idx)
				i += raid_disks;
			i -= (sh->pd_idx + 1);
			break;
		default:
			printk("raid5: unsupported algorithm %d\n",
				conf->algorithm);
	}

	chunk_number = stripe * data_disks + i;
	r_sector = (sector_t)chunk_number * sectors_per_chunk + chunk_offset;
        
	check = jraid5_compute_sector (r_sector, raid_disks, data_disks, &dummy1, &dummy2, conf);
        printk("chunk_number=%d r_sector = %lu check=%lu sh->sector=%lu [dummy1=%d dd_idx=%d] \
                        [dummy2=%d sh->pd_idx=%d]\n",chunk_number, r_sector, check, 
                        sh->sector, dummy1, dd_idx, dummy2, sh->pd_idx);
	if (check != sh->sector || dummy1 != dd_idx || dummy2 != sh->pd_idx) {
		printk("compute_blocknr: map not correct\n");
		return 0;
	}
	return r_sector;
}
	
static void __jraid5_build_block (struct stripe_head *sh, int i)
{
	struct r5dev *dev = &sh->dev[i];

	bio_init(&dev->req);
	dev->req.bi_io_vec = &dev->vec;
	dev->req.bi_vcnt++;
	dev->req.bi_max_vecs++;
	dev->vec.bv_page = dev->page;
	dev->vec.bv_len = STRIPE_SIZE;
	dev->vec.bv_offset = 0;

	dev->req.bi_sector = sh->sector;
	dev->req.bi_private = sh;

	PRINTK("build block called -- [i=%d sh->pd_idx=%d] --\n", i, sh->pd_idx);
	dev->flags = 0;
	if (i != sh->pd_idx)
		dev->sector = compute_blocknr(sh, i);
}

static __inline__ void insert_hash(struct jraid5_conf *conf, struct stripe_head *sh)
{
	struct stripe_head **shp = &stripe_hash(conf, sh->sector);

	PRINTK("insert_hash(), stripe %lu\n",sh->sector);

	if ((sh->hash_next = *shp) != NULL)
		(*shp)->hash_pprev = &sh->hash_next;
	*shp = sh;
	sh->hash_pprev = shp;
}

static inline void __init_stripe(struct stripe_head *sh, sector_t sector, int pd_idx)
{
	struct jraid5_conf *conf = sh->raid_conf;
	int disks = conf->raid_disks, i;

	if (atomic_read(&sh->count) != 0)
		BUG();
	if (test_bit(STRIPE_HANDLE, &sh->state))
		BUG();
	
	PRINTK("init_stripe called, stripe %llu\n", 
		(unsigned long long)sh->sector);

	remove_hash(sh);
	
	sh->sector = sector;
	sh->pd_idx = pd_idx;
	sh->state = 0;

	for (i=disks; i--; ) {
		struct r5dev *dev = &sh->dev[i];

		if (dev->toread || dev->towrite || dev->written ||
		    test_bit(R5_LOCKED, &dev->flags)) {
			printk("sector=%llx i=%d %p %p %p %d\n",
			       (unsigned long long)sh->sector, i, dev->toread,
			       dev->towrite, dev->written,
			       test_bit(R5_LOCKED, &dev->flags));
			BUG();
		}
		dev->flags = 0;
		__jraid5_build_block(sh, i);
	}
	insert_hash(conf, sh);
}

static struct stripe_head *get_active_stripe(struct jraid5_conf *conf, unsigned long sector, 
                                        int size, int pd_idx, int noblock) 
{
	struct stripe_head *sh;

	PRINTK("get_stripe, sector %lu\n", sector);

	spin_lock_irq(&conf->device_lock);

	do {
		sh = __find_stripe(conf, sector);
		if (!sh) {
                        printk("xx 1 xx\n");
			sh = get_free_stripe(conf);
			if (noblock && sh == NULL)
				break;
			if (!sh) {
                                printk("xx 2 xx\n");
				wait_event_lock_irq(conf->wait_for_stripe,
						    !list_empty(&conf->inactive_list),
						    conf->device_lock);
                        } else{
                                printk("xx 3 xx\n");
				__init_stripe(sh, sector, pd_idx);
                        }
		} else {
			if (atomic_read(&sh->count)) {
                                if (!list_empty(&sh->lru)){
                                        printk("bug for empty sh->lru\n");
					//BUG();
                                }
			} else {
				if (!test_bit(STRIPE_HANDLE, &sh->state))
					atomic_inc(&conf->active_stripes);
				if (list_empty(&sh->lru))
					BUG();
				list_del_init(&sh->lru);
			}
		}
	} while (sh == NULL);

	if (sh)
		atomic_inc(&sh->count);

	spin_unlock_irq(&conf->device_lock);
	return sh;
}

static int __add_stripe_bio(struct stripe_head *sh, struct bio *bi, int dd_idx, int forwrite)
{
	struct bio **bip;
	struct jraid5_conf *conf = sh->raid_conf;

	PRINTK("adding bh b#%llu to stripe s#%llu\n",
		(unsigned long long)bi->bi_sector,
		(unsigned long long)sh->sector);


	spin_lock(&sh->lock);
	spin_lock_irq(&conf->device_lock);
	if (forwrite == WRITE)
		bip = &sh->dev[dd_idx].towrite;
	else
		bip = &sh->dev[dd_idx].toread;
	while (*bip && (*bip)->bi_sector < bi->bi_sector) {
		if ((*bip)->bi_sector + ((*bip)->bi_size >> 9) > bi->bi_sector)
			goto overlap;
		bip = & (*bip)->bi_next;
	}
	if (*bip && (*bip)->bi_sector < bi->bi_sector + ((bi->bi_size)>>9))
		goto overlap;

	if (*bip && bi->bi_next && (*bip) != bi->bi_next)
		BUG();
        PRINTK("bi=%x *bip = %x \n", bi, *bip);
        if (*bip){
		bi->bi_next = *bip;
                PRINTK("the are towrite bio\n");
        }
	*bip = bi;
	//bi->bi_phys_segments ++;
        printk("func %s bi_phys_segments = %u\n", __func__, bi->bi_phys_segments);
	spin_unlock_irq(&conf->device_lock);
	spin_unlock(&sh->lock);

	PRINTK("added bi b#%llu to stripe s#%llu, disk %d.\n",
		(unsigned long long)bi->bi_sector,
		(unsigned long long)sh->sector, dd_idx);

        if (forwrite == WRITE) {
                /* check if page is covered */
                sector_t sector = sh->dev[dd_idx].sector;
                PRINTK("sector = %u bi->sh->dec[dd_idx].towrite = %x\n", 
                                sector, sh->dev[dd_idx].towrite);
                for (bi=sh->dev[dd_idx].towrite;
                     sector < sh->dev[dd_idx].sector + STRIPE_SECTORS &&
                     bi && bi->bi_sector <= sector;
                     bi = r5_next_bio(bi, sh->dev[dd_idx].sector)) {
                             PRINTK("page covered ? bi =%x bi->next = %x -> bi->bi_sector = %u \
                                    sector = %u ...\n", bi, bi->bi_next, bi->bi_sector, sector);
                             if (bi->bi_sector + (bi->bi_size>>9) >= sector){
                                     sector = bi->bi_sector + (bi->bi_size>>9);
                             }
                }
                if (sector >= sh->dev[dd_idx].sector + STRIPE_SECTORS){
                        PRINTK("sector = %u sh->dev[dd_idx].sector = %u -> overwrite needed\n", 
                                sector, sh->dev[dd_idx].sector);
                        set_bit(R5_OVERWRITE, &sh->dev[dd_idx].flags);
                }
        }
	return 1;

 overlap:
	set_bit(R5_Overlap, &sh->dev[dd_idx].flags);
	spin_unlock_irq(&conf->device_lock);
	spin_unlock(&sh->lock);
	return 0;
}

static void copy_data(int frombio, struct bio *bio,
		     struct page *page,
		     sector_t sector)
{
	char *pa = page_address(page);
	struct bio_vec *bvl;
	int i;
	int page_offset;

	if (bio->bi_sector >= sector)
		page_offset = (signed)(bio->bi_sector - sector) * 512;
	else
		page_offset = (signed)(sector - bio->bi_sector) * -512;
	bio_for_each_segment(bvl, bio, i) {
		int len = bio_iovec_idx(bio,i)->bv_len;
		int clen;
		int b_offset = 0;

		if (page_offset < 0) {
			b_offset = -page_offset;
			page_offset += b_offset;
			len -= b_offset;
		}

		if (len > 0 && page_offset + len > STRIPE_SIZE)
			clen = STRIPE_SIZE - page_offset;
		else clen = len;
			
		if (clen > 0) {
			char *ba = __bio_kmap_atomic(bio, i, KM_USER0);
			if (frombio)
				memcpy(pa+page_offset, ba+b_offset, clen);
			else
				memcpy(ba+b_offset, pa+page_offset, clen);
			__bio_kunmap_atomic(ba, KM_USER0);
		}
		if (clen < len) /* hit end of page */
			break;
		page_offset +=  len;
        }
}

void print_bio_info(struct bio *bio)
{
        PRINTK("bio->bi_io_vec = %x\n", bio->bi_io_vec);
        PRINTK("bio->bi_vcnt = %u\n", bio->bi_vcnt);
        PRINTK("bio->bi_max_vecs= %u\n", bio->bi_max_vecs);
        PRINTK("bio->bi_sector = %u\n", bio->bi_sector);
        PRINTK("bio->bi_io_vec[0].bv_len = %u\n", bio->bi_io_vec[0].bv_len);
        PRINTK("bio->bi_io_vec[0].bv_offset = %u\n", bio->bi_io_vec[0].bv_offset);
        PRINTK("bio->bi_size = %u\n", bio->bi_size);
        PRINTK("bio->bi_next = %x\n", bio->bi_next);
        PRINTK("bio->bi_idx = %u\n", bio->bi_idx);
        PRINTK("bio->bi_rw = %u\n", bio->bi_rw);
        PRINTK("bio->bi_integrity = %p\n", bio->bi_integrity);
        return ;
}

static void handle_stripe(struct stripe_head *sh)
{
	struct jraid5_conf *conf = sh->raid_conf;
	int disks = conf->raid_disks;
	struct bio *return_bi= NULL;
	struct bio *bi;
	int i;
	int syncing;
	int locked=0, uptodate=0, to_read=0, to_write=0, failed=0, written=0;
	int non_overwrite = 0;
	int failed_num=0;
	struct r5dev *dev;

	PRINTK("handling stripe %llu, cnt=%d, pd_idx=%d\n",
		(unsigned long long)sh->sector, atomic_read(&sh->count),
                sh->pd_idx);

	for (i=disks; i--; ) {
		struct jmd_rdev *jrdev;
		dev = &sh->dev[i];

		PRINTK("check %d: state 0x%lx read %p write %p written %p\n",
			i, dev->flags, dev->toread, dev->towrite, dev->written);
/*
		if (test_bit(R5_UPTODATE, &dev->flags) && dev->toread) {
			struct bio *rbi, *rbi2;
			PRINTK("Return read for disc %d\n", i);
			spin_lock_irq(&conf->device_lock);
			rbi = dev->toread;
			dev->toread = NULL;

			spin_unlock_irq(&conf->device_lock);
			while (rbi && rbi->bi_sector < dev->sector + STRIPE_SECTORS) {
				copy_data(0, rbi, dev->page, dev->sector);
				rbi2 = r5_next_bio(rbi, dev->sector);
				spin_lock_irq(&conf->device_lock);
				if (--rbi->bi_phys_segments == 1) {
					rbi->bi_next = return_bi;
					return_bi = rbi;
                                        printk("fuc %s rbi->bi_phys_segments=%u \n", 
                                                rbi->bi_phys_segments);
				}
				spin_unlock_irq(&conf->device_lock);
				rbi = rbi2;
			}
		}
*/

		if (test_bit(R5_UPTODATE, &dev->flags)) uptodate++;

                if (dev->toread) {
		        set_bit(R5_Wantread, &sh->dev[i].flags);
                        to_read++;
                }
		if (dev->towrite) {
			to_write++;
			if (!test_bit(R5_OVERWRITE, &dev->flags))
				non_overwrite++;
		        set_bit(R5_Wantwrite, &sh->dev[i].flags);
		}
		if (dev->written) written++;
	}

	dev = &sh->dev[sh->pd_idx];

        // for test
	set_bit(R5_Insync, &dev->flags);
	clear_bit(R5_LOCKED, &dev->flags);
	set_bit(R5_UPTODATE, &dev->flags);

        printk("for write: written=%u \n", written);
        written = 0;
        if ( written &&
            ( (test_bit(R5_Insync, &dev->flags) && !test_bit(R5_LOCKED, &dev->flags) &&
               test_bit(R5_UPTODATE, &dev->flags))
             || (failed == 1 && failed_num == sh->pd_idx))
           ) {
                for (i=disks; i--; ){
                printk("sh->dev[i].towrite = %p\n", sh->dev[i].towrite);
                sh->dev[i].written = sh->dev[i].towrite;
                if (sh->dev[i].written) {
                        dev = &sh->dev[i];
                        // for test
                        clear_bit(R5_LOCKED, &dev->flags);
                        set_bit(R5_UPTODATE, &dev->flags);

                        if (!test_bit(R5_LOCKED, &dev->flags) &&
                                test_bit(R5_UPTODATE, &dev->flags) ) {
                                /* We can return any write requests */
                                struct bio *wbi, *wbi2;
                                PRINTK("Return write for disc %d\n", i);
                                spin_lock_irq(&conf->device_lock);
                                wbi = dev->written;
                                dev->written = NULL;
                                while (wbi && wbi->bi_sector < dev->sector + STRIPE_SECTORS) {
                                        wbi2 = r5_next_bio(wbi, dev->sector);
                                        if (--wbi->bi_phys_segments == 1) {
                                                //jmd_write_end(conf->jmddev);
                                                wbi->bi_next = return_bi;
                                                return_bi = wbi;
                                                printk("fuc rbi->bi_phys_segments=%u \n", 
                                                                wbi->bi_phys_segments);
                                        }
                                                wbi = wbi2;
                                }
                                spin_unlock_irq(&conf->device_lock);
                                }
                        }
                }
        }
        printk("return_bi = %p\n", return_bi);
        while(bi=return_bi){
                printk("return bi ... \n");
                return_bi = bi->bi_next;
                bi->bi_next = NULL;
                bi->bi_size = 0;
                bio_endio(bi, 0);
        }

	for (i=disks; i-- ;) {
		int rw;
		struct bio *bi;
		struct jmd_rdev *jrdev;
		if (test_and_clear_bit(R5_Wantwrite, &sh->dev[i].flags))
			rw = 1;
		else if (test_and_clear_bit(R5_Wantread, &sh->dev[i].flags))
			rw = 0;
                else{
                        printk("skip i=%u ...\n", i);
			continue;
                }
		bi = &sh->dev[i].req;
		//bi->bi_rw= rw;	
 
		if (rw)
			bi->bi_end_io = raid5_end_write_request;
		else
			bi->bi_end_io = raid5_end_read_request;
 
		jrdev = conf->disks[i].jrdev;
                printk("...jrdev=%x bi=%x...\n", jrdev, bi);
 
		if (jrdev) {
                        //bio_reset(bi);
                        print_bio_info(bi);
			bi->bi_bdev = jrdev->bdev;
			PRINTK("for %llu schedule op %ld on disc %d\n",
				(unsigned long long)sh->sector, bi->bi_rw, i);
                        bi->bi_rw = rw;
			atomic_inc(&sh->count);
                        bi->bi_idx = 0;
			//bi->bi_sector = sh->sector + jrdev->data_offset;
			bi->bi_sector = sh->sector; //superblock 0.9  jrdev likely data_offset = 0;
                        bi->bi_flags |= 1 << BIO_UPTODATE;
                        bi->bi_private = sh;
			bi->bi_vcnt = 0;	
			bi->bi_max_vecs = 1;
			bi->bi_io_vec = &sh->dev[i].vec;
			bi->bi_io_vec[0].bv_len = STRIPE_SIZE;
			bi->bi_io_vec[0].bv_offset = 0;
			bi->bi_size = STRIPE_SIZE;
                        //bi->bi_rw |= REQ_FLUSH;
                        //bi->bi_rw |= REQ_SYNC;
			//bi->bi_next = NULL;
                        
                        //bio_add_page(bi, sh->dev[i].page, PAGE_SIZE, 0);
                        smp_mb();
                        printk("rw=%u ...generic_make_request...\n", rw);
                        print_bio_info(bi);
			generic_make_request(bi);
		} else {
			PRINTK("...skip op %ld on disc %d for sector %llu\n",
				bi->bi_rw, i, (unsigned long long)sh->sector);
			clear_bit(R5_LOCKED, &sh->dev[i].flags);
	 		set_bit(STRIPE_HANDLE, &sh->state);
		}
	}
}

#if 1
void readwrite_prepare(struct bio* bio)
{
	struct bio_vec *bvec;
	sector_t sector;
	unsigned int offset,copy;
	int nr, i, j;

	sector = bio->bi_sector;
        printk(KERN_INFO "000 bi_cnt = %u\n", atomic_read(&bio->bi_cnt));
	bio_for_each_segment(bvec, bio, i){
		bvec->bv_page->private = (unsigned long)bio;
		offset = (sector & (PAGE_SECTORS -1)) << SECTOR_SHIFT;
		copy = min_t(unsigned int, bvec->bv_len, PAGE_SIZE - offset);
		nr = (bvec->bv_len>(PAGE_SIZE-offset))?2:1;
		for(j=0;j<nr;j++){
			sector += copy >>SECTOR_SHIFT;
			copy = bvec->bv_len-copy;
			bio_get(bio);  
                        printk(KERN_INFO "111 bi_cnt = %u\n", atomic_read(&bio->bi_cnt));
		}
		sector += bvec->bv_len >> SECTOR_SHIFT;
	}
	bio_get(bio);   
        printk(KERN_INFO "222 bi_cnt = %u\n", atomic_read(&bio->bi_cnt));
}

void prepare_endio(struct bio *bio)
{
        printk(KERN_INFO "before bi_cnt = %u\n", atomic_read(&bio->bi_cnt));
	if (1 == atomic_dec_return(&bio->bi_cnt)) {
	        bio->bi_flags |= 1 << BIO_UPTODATE;
		bio_endio(bio,0);
	}
        printk(KERN_INFO "after bi_cnt = %u\n", atomic_read(&bio->bi_cnt));
}

static void io_end_bio(struct bio *bio, int err)
{
	struct jmd_rdev *jrdev = bio->bi_private;
        printk(KERN_INFO "in end bio\n");

	if (unlikely(!bio_flagged(bio,BIO_UPTODATE))) {
		if (err == 0) {
			printk(KERN_ERR "%s-%d: Not uptodata bio try again !%ld \n",
				 __func__, __LINE__, bio->bi_sector);
			return ;
		}
	}
        //prepare_endio(jrdev->bio);
	smp_mb();
	//__free_pages(page, 0);
	bio_put(bio);
}

struct submit_bio_ret {
                struct completion event;
                int error;

};

static void _submit_bio_wait_endio(struct bio *bio, int error)
{
        struct submit_bio_ret *ret = bio->bi_private;

        ret->error = error;
        complete(&ret->event);

}

int _submit_bio_wait(int rw, struct bio *bio)
{
        struct submit_bio_ret ret;

        rw |= REQ_SYNC;
        init_completion(&ret.event);
        bio->bi_private = &ret;
        bio->bi_end_io = _submit_bio_wait_endio;
        submit_bio(rw, bio);
        wait_for_completion(&ret.event);

        return ret.error;
}

int submit_readwrite(int rw,struct jmd_rdev *jrdev, struct page *page, sector_t sector)
{
	struct bio *bio;
	int ret = 0;
	
	bio = bio_kmalloc(GFP_KERNEL, 1);
	if (!bio) {
		printk(KERN_ERR "%s-%d: Cannot alloc bio\n", __func__, __LINE__);
		return -ENOMEM;
	}
	/* init bio */
	bio->bi_bdev = jrdev->bdev;
        PRINTK("bio->bi_sector = %u\n", sector);
	bio->bi_sector = (sector / PAGE_SECTORS)*PAGE_SECTORS;
        //fix it
        if(bio->bi_sector >= 20971520){
                bio->bi_sector = 0;
                goto out;
        }
        PRINTK("bio->bi_sector = %u\n", bio->bi_sector);
	bio->bi_private = jrdev;

	bio->bi_rw |= REQ_FAILFAST_DEV |
		REQ_FAILFAST_TRANSPORT |
		REQ_FAILFAST_DRIVER;

	bio_add_page(bio, page, PAGE_SIZE, 0);

	bio->bi_end_io = io_end_bio;

	submit_bio(rw, bio);
        //if(rw == WRITE){
	//        submit_bio(rw, bio);
	//        //ret = _submit_bio_wait(WRITE, bio);
        //        //if(ret)
        //        //        printk(KERN_ERR "failed to wait submit WRITE\n");
        //}else{
	//        ret = _submit_bio_wait(READ, bio);
        //        if(ret)
        //                printk(KERN_ERR "failed to wait submit READ\n");
        //        bio_put(bio);
        //}

out:
        //bio_put(bio);

	return 0;
}

static int non_stripe_write(struct jmd_rdev *jrdev, struct page *page, unsigned int len, 
                unsigned int off, sector_t sector, uint64_t bi_sector, uint32_t bi_size)
{
	unsigned int offset = (sector & (PAGE_SECTORS -1)) << SECTOR_SHIFT;
	unsigned int copy = min_t(unsigned int, len, PAGE_SIZE - offset);
        char sectors;
        int nr,i,index;

        void *dst = NULL, *src = NULL;
        src = page_address(page)+off;
        nr = (len>(PAGE_SIZE-offset))?2:1;

	for (i=0; i<nr; i++) {
                page = alloc_page(GFP_KERNEL);
                if (!page) {
                        printk(KERN_ERR "%s-%d: Cannot alloc page\n", __func__, __LINE__);
                        return -ENOMEM;
                }
                sectors = copy >> SECTOR_SHIFT;
                src += i*(len-copy);
                dst = page_address(page)+offset;
                memcpy(dst, src, copy);

                submit_readwrite(WRITE, jrdev, page, sector);

		sector += sectors;
		copy = len-copy;
		offset = 0;
                __free_pages(page, 0);
        }
        return 0;
}

static int non_stripe_read(struct jmd_rdev *jrdev, struct page *page, unsigned int len, 
                        unsigned int off, sector_t sector)
{
	unsigned int offset = (sector & (PAGE_SECTORS -1)) << SECTOR_SHIFT;
	unsigned int copy = min_t(unsigned int, len, PAGE_SIZE - offset);
	char sectors;
	int nr, i;
	void *dst = NULL, *src = NULL;
	dst = page_address(page)+off;
	nr = (len>(PAGE_SIZE-offset))?2:1;

        for(i=0;i<nr;i++){
                page = alloc_page(GFP_KERNEL);
                if (!page) {
                        printk(KERN_ERR "%s-%d: Cannot alloc page\n", __func__, __LINE__);
                        return -ENOMEM;
                }
                sectors = copy >>SECTOR_SHIFT;          
		dst += i*(len-copy);

	        src = page_address(page)+offset;
                submit_readwrite(READ, jrdev, page, sector);

	        memcpy(dst, src, copy);
		sector += sectors;
		copy = len-copy;
		offset = 0;
                __free_pages(page, 0);
        }
        return 0;
}

#endif


/*
 * for non stripe io request
 */
static int _jraid5_make_request(struct jmddev *jmddev, struct bio* bi)
{
        int rw, i, err;
        struct jmd_rdev *jrdev;
	struct list_head *tmp;
        struct bio_vec *bvec;
        sector_t sector = bi->bi_sector;

        ITERATE_RDEV(jmddev,jrdev,tmp) {
                PRINTK("the rdev = %u\n", jrdev->desc_nr);

                //just for simple IO test! 
                //bi->bi_bdev = jrdev->bdev;
                //generic_make_request(bi);
                //break;
                
                // for bio segment strace                
                //readwrite_prepare(bi);
                jrdev->bio = bi;
                bio_for_each_segment(bvec, bi, i) {
                        if(rw == WRITE){
                                err = non_stripe_write(jrdev, bvec->bv_page, bvec->bv_len,
                                                bvec->bv_offset, sector,
                                                bi->bi_sector, bi->bi_size);
                        }else{
                                err = non_stripe_read(jrdev, bvec->bv_page, bvec->bv_len,
                                                bvec->bv_offset, sector);
                        }
                        if (err) break;
                        sector += bvec->bv_len >> SECTOR_SHIFT;
                }
                break;
        }
        //prepare_endio(bi);
	bi->bi_flags |= 1 << BIO_UPTODATE;
        bio_endio(bi,0);

}

static int jraid5_make_request(struct jmddev *jmddev, struct bio* bi)
{
	struct jraid5_conf *conf = (struct jraid5_conf *)jmddev->private;
	const unsigned int jraid_disks = conf->raid_disks;
	const unsigned int data_disks = jraid_disks - 1;
	unsigned int dd_idx, pd_idx;
	unsigned long new_sector;
	int read_ahead = 0;
        int rw = bio_data_dir(bi);

	struct stripe_head *sh;

	if (rw == READA) {
		rw = READ;
		read_ahead=1;
	}
        bi->bi_next = NULL;
        bi->bi_phys_segments = 1;

	new_sector = jraid5_compute_sector(bi->bi_sector,
			jraid_disks, data_disks, &dd_idx, &pd_idx, conf);

	PRINTK("jraid5_make_request, sector %lu dd_idx = %d pd_idx = %d \n", 
                                new_sector, dd_idx, pd_idx);
	sh = get_active_stripe(conf, new_sector, bi->bi_size, pd_idx, read_ahead);
	if (sh) {
		//sh->pd_idx = pd_idx;

		__add_stripe_bio(sh, bi, dd_idx, rw);
		handle_stripe(sh);
		release_stripe(sh);
        } else{
	        PRINTK(" ... bio_endio ...\n");
                //bio_endio(bi, 0);
        }

        //if(jmddev->tvalue == 0){
        //        DEFINE_WAIT(_w);
        //        for(;;){
	//                PRINTK(" ... each wake print ...\n");
        //                prepare_to_wait(&jmddev->twait, &_w,                    
        //                                TASK_UNINTERRUPTIBLE); 
        //                if(!jmddev->tvalue)
        //                        break;
        //                schedule();
        //        }
        //        finish_wait(&jmddev->twait, &_w);
        //}
	//PRINTK(" ... wake up to bio end handle ...\n");

	//bi->bi_flags |= 1 << BIO_UPTODATE;
        //bio_endio(bi,0);
        spin_lock_irq(&conf->device_lock);
        if(--bi->bi_phys_segments == 0){
	        bi->bi_flags |= 1 << BIO_UPTODATE;
                bio_endio(bi, 0);
        } 
        printk("func %s bi_phys_segments = %u\n", __func__, bi->bi_phys_segments);
        spin_unlock_irq(&conf->device_lock);

        return 0;
} 

static int jraid5_sync_request(struct jmddev *jmddev, unsigned long block_nr)
{
	struct jraid5_conf *conf = (struct jraid5_conf*)jmddev->private;
	struct stripe_head *sh;
	int sectors_per_chunk = conf->chunk_size >> 9;
	unsigned long stripe = (block_nr<<1)/sectors_per_chunk;
	int chunk_offset = (block_nr<<1) % sectors_per_chunk;
	int dd_idx, pd_idx;
	unsigned long first_sector;
	int raid_disks = conf->raid_disks;
	int data_disks = raid_disks-1;
	int redone = 0;
	int bufsize;

	//sh = get_active_stripe(conf, block_nr<<1, 0, 0);
	bufsize = sh->size;
	redone = block_nr-(sh->sector>>1);
	first_sector = jraid5_compute_sector(stripe*data_disks*sectors_per_chunk
		+ chunk_offset, raid_disks, data_disks, &dd_idx, &pd_idx, conf);
	sh->pd_idx = pd_idx;
	spin_lock(&sh->lock);	
	set_bit(STRIPE_SYNCING, &sh->state);
	clear_bit(STRIPE_INSYNC, &sh->state);
	sh->sync_redone = redone;
	spin_unlock(&sh->lock);

	handle_stripe(sh);
	release_stripe(sh);

	return (bufsize>>10)-redone;
}

static void jraid5d(struct jmd_thread *thread)
{
	struct stripe_head *sh;
	struct jmddev *jmddev = thread->jmddev;
	struct jraid5_conf *conf = jmddev->private;
	int handled;
	printk("+++ jraid5d active\n");

	handled = 0;

	if (jmddev->sb_dirty) {
                printk("\n.. 2 .. jmddev->sb_dirty =%d \n", jmddev->sb_dirty);
		jmddev->sb_dirty = 0;
		jmd_update_sb(jmddev);
	}
        
	spin_lock_irq(&conf->device_lock);
        while(!list_empty(&conf->handle_list)){
                printk(".....in !empty ..... \n");
                struct list_head *first = conf->handle_list.next;
                if(!first)
                        break;
                sh = list_entry(first, struct stripe_head, lru);

                list_del_init(first);
                atomic_inc(&sh->count);
                if (atomic_read(&sh->count)!= 1)
                        BUG();
                spin_unlock_irq(&conf->device_lock);

                handled++;
                handle_stripe(sh);
                printk("... to release stripe_head ...\n");
                release_stripe(sh);

                spin_lock_irq(&conf->device_lock);
        }	

	printk("%d stripes handled\n", handled);

	spin_unlock_irq(&conf->device_lock);

	printk("--- jraid5d inactive\n");
}

static void jraid5syncd(struct jmd_thread *thread)
{
	struct jmddev *jmddev = thread->jmddev;
	struct jraid5_conf *conf = jmddev->private;
        printk("\n In jraid5syncd ..\n");

	printk("raid5: resync finished.\n");
}

static int jraid5_run(struct jmddev *jmddev)
{
	struct jraid5_conf *conf;
	int i, j, raid_disk, memory;
	struct jmd_superblock *sb = jmddev->sb;
	struct jmd_disk *desc;
	struct jmd_rdev *jrdev;
	struct disk_info *disk;
	struct list_head *tmp;
	int start_recovery = 0;

	if (sb->level != 5 && sb->level != 4) {
		printk("raid5: md%d: raid level not set to 4/5 (%d)\n", 
                                        jmdidx(jmddev), sb->level);
		return -EIO;
	}

	jmddev->private = kmalloc(sizeof(struct jraid5_conf), GFP_KERNEL);
	if ((conf = jmddev->private) == NULL)
		goto abort;
	memset(conf, 0, sizeof(*conf));
	conf->jmddev = jmddev;
	conf->jraid_disks = jmddev->nb_dev;

	conf->stripe_hashtbl = (struct stripe_head **)__get_free_pages(GFP_ATOMIC, HASH_PAGES_ORDER);
	if (NULL == conf->stripe_hashtbl)
		goto abort;
	memset(conf->stripe_hashtbl, 0, HASH_PAGES * PAGE_SIZE);

        spin_lock_init(&conf->device_lock);
	init_waitqueue_head(&conf->wait_for_stripe);
	INIT_LIST_HEAD(&conf->handle_list);
	INIT_LIST_HEAD(&conf->inactive_list);
	atomic_set(&conf->active_stripes, 0);
	conf->buffer_size = PAGE_SIZE;

	PRINTK("jraid5_run(jmd%d) called.\n", jmdidx(jmddev));

	ITERATE_RDEV(jmddev,jrdev,tmp) {
		desc = sb->disks + jrdev->desc_nr;
		raid_disk = desc->raid_disk;
		disk = conf->disks + raid_disk;

                mark_disk_active(desc);
                mark_disk_sync(desc);

		if (disk_active(desc)) {
			if (!disk_sync(desc)) {
				printk(KERN_ERR "jraid5: disabled device s (not in sync)\n");
				MD_BUG();
				goto abort;
			}
			if (raid_disk > sb->raid_disks) {
				printk(KERN_ERR "jraid5: disabled device s (inconsistent descriptor)\n");
				continue;
			}
			if (disk->operational) {
				printk(KERN_ERR "jraid5: disabled device s (device %d already operational)\n",
                                                                                raid_disk);
				continue;
			}
			printk(KERN_INFO "jraid5: device s operational as raid disk %d\n", 
                                                                        raid_disk);
	
			disk->number = desc->number;
			disk->raid_disk = raid_disk;
			disk->dev = jrdev->dev;
			disk->operational = 1;
			disk->used_slot = 1;

                        disk->jrdev = jrdev;

			conf->working_disks++;
		} else {
			printk(KERN_INFO "raid5: spare disk s\n");
			disk->number = desc->number;
			disk->raid_disk = raid_disk;
			disk->dev = jrdev->dev;

			disk->operational = 0;
			disk->write_only = 0;
			disk->spare = 1;
			disk->used_slot = 1;
                        disk->jrdev = jrdev;
		}
	}

	for (i = 0; i < MD_SB_DISKS; i++) {
		desc = sb->disks + i;
		raid_disk = desc->raid_disk;
		disk = conf->disks + raid_disk;

		if (disk_faulty(desc) && (raid_disk < sb->raid_disks) &&
			!conf->disks[raid_disk].used_slot) {

			disk->number = desc->number;
			disk->raid_disk = raid_disk;
			disk->dev = MKDEV(0,0);

			disk->operational = 0;
			disk->write_only = 0;
			disk->spare = 0;
			disk->used_slot = 1;
		}
	}

	conf->raid_disks = sb->raid_disks;

        printk("conf->raid_disks=%d conf->working_disks=%d\n", 
                        conf->raid_disks, conf->working_disks);
	conf->failed_disks = conf->raid_disks - conf->working_disks;
	conf->jmddev = jmddev;
	conf->chunk_size = sb->chunk_size;
	conf->level = sb->level;
	conf->algorithm = sb->layout;
	conf->max_nr_stripes = NR_STRIPES;

	if (!conf->chunk_size || conf->chunk_size % 4) {
		printk(KERN_ERR "raid5: invalid chunk size %d for md%d\n", 
                                        conf->chunk_size, jmdidx(jmddev));
		goto abort;
	}
	if (conf->algorithm > ALGORITHM_RIGHT_SYMMETRIC) {
		printk(KERN_ERR "raid5: unsupported parity algorithm %d for md%d\n", 
                                                conf->algorithm, jmdidx(jmddev));
		goto abort;
	}

	if (conf->failed_disks > 1) {
		printk(KERN_ERR "raid5: not enough operational devices for md%d (%d/%d failed)\n", 
                                        jmdidx(jmddev), conf->failed_disks, conf->raid_disks);
		goto abort;
	}

	if (conf->working_disks != sb->raid_disks) {
		printk(KERN_ALERT "raid5: md%d, not all disks are operational \
                                -- trying to recover array\n", jmdidx(jmddev));
		start_recovery = 1;
	}

	if (!start_recovery && (sb->state & (1 << MD_SB_CLEAN))){
		printk(KERN_ERR "raid5: detected raid-5 superblock xor inconsistency \
                                                                -- running resync\n");
		sb->state &= ~(1 << MD_SB_CLEAN);
	}

        const char * name = "jraid5d";

        conf->thread = jmd_register_thread(jraid5d, jmddev, name);
        if (!conf->thread) {
                printk(KERN_ERR "raid5: couldn't allocate thread for md%d\n", 
                                                        jmdidx(jmddev));
                goto abort;
        }

	memory = conf->max_nr_stripes * (sizeof(struct stripe_head) +
		 conf->raid_disks * ((sizeof(struct buffer_head) + PAGE_SIZE))) / 1024;

	if (grow_stripes(conf, conf->max_nr_stripes)) {
	        printk(KERN_ERR "raid5: couldn't allocate %dkB for buffers\n", memory);
	        shrink_stripes(conf, conf->max_nr_stripes);
	        goto abort;
	} else
		printk(KERN_INFO "raid5: allocated %dkB for md%d\n", memory, jmdidx(jmddev));

	for (i = 0; i < MD_SB_DISKS ; i++) { 
                mark_disk_nonsync(sb->disks + i);
		for (j = 0; j < sb->raid_disks; j++) {
			if (!conf->disks[j].operational)
				continue;
			if (sb->disks[i].number == conf->disks[j].number)
				mark_disk_sync(sb->disks + i);
		}
	}
	sb->active_disks = conf->working_disks;

	if (sb->active_disks == sb->raid_disks)
		printk("raid5: raid level %d set md%d active with %d out of %d devices, algorithm %d\n", 
                        conf->level, jmdidx(jmddev), sb->active_disks, sb->raid_disks, conf->algorithm);
	else
		printk(KERN_ALERT "raid5: raid level %d set md%d active with %d out of %d devices, algorithm %d\n", 
                                conf->level, jmdidx(jmddev), sb->active_disks, sb->raid_disks, conf->algorithm);

	if (!start_recovery && !(sb->state & (1 << MD_SB_CLEAN))) {
		const char * name = "jraid5syncd";

		conf->resync_thread = jmd_register_thread(jraid5syncd, jmddev,name);
		if (!conf->resync_thread) {
			printk(KERN_ERR "raid5: couldn't allocate thread for md%d\n", 
                                                                jmdidx(jmddev));
			goto abort;
		}

		printk("raid5: raid set md%d not clean; reconstructing parity\n", 
                                                                jmdidx(jmddev));
		conf->resync_parity = 1;
		jmd_wakeup_thread(conf->resync_thread);
	}
	jmd_wakeup_thread(conf->thread);

	if (start_recovery)
		jmd_recover_arrays();

	return 0;
abort:
	if (conf) {
		if (conf->stripe_hashtbl)
			free_pages((unsigned long) conf->stripe_hashtbl,
							HASH_PAGES_ORDER);
		kfree(conf);
	}
	jmddev->private = NULL;
	printk(KERN_ALERT "raid5: failed to run raid set md%d\n", jmdidx(jmddev));
	return -EIO;
}

static struct jmd_personality jraid5_personality =
{
	.name     =  "jraid5",
	.make_request  = jraid5_make_request,
	.run      =  jraid5_run,
	.sync_request        = jraid5_sync_request,
};

static int __init jraid5_init(void)
{
	return register_jmd_personality(RAID5, &jraid5_personality);
}

static void __exit jraid5_exit(void)
{
        struct jmddev *jmddev = g_jmddev;
        struct jraid5_conf *conf = NULL;

        if(jmddev->private){
                conf = jmddev->private;

                if(conf->resync_thread)
                jmd_unregister_thread(conf->resync_thread);

                if(conf->thread)
                jmd_unregister_thread(conf->thread);

                shrink_stripes(conf, conf->max_nr_stripes);
		if (conf->stripe_hashtbl)
			free_pages((unsigned long) conf->stripe_hashtbl,
							HASH_PAGES_ORDER);
                kfree(conf);
        }
	unregister_jmd_personality(RAID5);
}

module_init(jraid5_init);
module_exit(jraid5_exit);
MODULE_LICENSE("GPL");
