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
#ifndef __JRAID_H__
#define __JRAID_H__
#include "jmd.h"

struct stripe_head {
	struct stripe_head	*hash_next, **hash_pprev; /* hash pointers */
	struct list_head	lru;			/* inactive_list or handle_list */
	struct jraid5_conf *raid_conf;
	struct buffer_head	*bh_cache[MD_SB_DISKS];	/* buffered copy */
	struct buffer_head	*bh_read[MD_SB_DISKS];	/* read request buffers of the MD device */
	struct buffer_head	*bh_write[MD_SB_DISKS];	/* write request buffers of the MD device */
	struct buffer_head	*bh_written[MD_SB_DISKS]; /* write request buffers of the MD device \
                                                                that have been scheduled for write */
	sector_t                sector;			/* sector of this row */

	int			size;			/* buffers size */
	int			pd_idx;			/* parity disk index */
	unsigned long		state;			/* state flags */
	atomic_t		count;			/* nr of active thread/requests */
	spinlock_t		lock;
	int			sync_redone;

        /*define: */
        struct r5dev {
                struct bio      req;
                struct bio_vec      vec;
                struct page     *page;
                struct bio      *toread, *towrite, *written;
                sector_t        sector;
                unsigned        flags;
        }dev[1];
};

/* Flags */                                                                     
#define R5_UPTODATE     0       /* page contains current data */                
#define R5_LOCKED       1       /* IO has been submitted on "req" */            
#define R5_OVERWRITE    2       /* towrite covers whole page */                 
/* and some that are internal to handle_stripe */                               
#define R5_Insync       3       /* rdev && rdev->in_sync at start */            
#define R5_Wantread     4       /* want to schedule a read */                   
#define R5_Wantwrite    5                                                       
#define R5_Syncio       6       /* this io need to be accounted as resync io */ 
#define R5_Overlap      7       /* There is a pending overlapping request on this block */


/*
 * Write method
 */
#define RECONSTRUCT_WRITE	1
#define READ_MODIFY_WRITE	2
/* not a write method, but a compute_parity mode */
#define	CHECK_PARITY		3

/*
 * Stripe state
 */
#define STRIPE_ERROR		1
#define STRIPE_HANDLE		2
#define	STRIPE_SYNCING		3
#define	STRIPE_INSYNC		4
#define	STRIPE_PREREAD_ACTIVE	5
#define	STRIPE_DELAYED		6

struct disk_info {
	dev_t	dev;
	int	operational;
	int	number;
	int	raid_disk;
	int	write_only;
	int	spare;
	int	used_slot;
        /*define: */
        struct jmd_rdev *jrdev;
};

struct jraid5_conf {
	struct stripe_head	**stripe_hashtbl;
	struct jmddev           *jmddev;
	struct jmd_thread	*thread, *resync_thread;
	struct disk_info	disks[MD_SB_DISKS];
	struct disk_info	*spare;
	int			buffer_size;
	int			chunk_size, level, algorithm;
	int			raid_disks, working_disks, failed_disks;
	int			resync_parity;
	int			max_nr_stripes;

	struct list_head	handle_list; /* stripes needing handling */
	/*
	 * Free stripes pool
	 */
	atomic_t		active_stripes;
	struct list_head	inactive_list;
	wait_queue_head_t	wait_for_stripe;
	wait_queue_head_t	wait_for_overlap;

	spinlock_t		device_lock;
        /*define: */
        unsigned int            jraid_disks;
        struct kmem_cache       *slab_cache;
        atomic_t                preread_active_stripes;
};

#define jmddev_to_conf(jmddev) ((struct jraid5_conf *)jmddev->private)

/*
 * Our supported algorithms
 */
#define ALGORITHM_LEFT_ASYMMETRIC	0
#define ALGORITHM_RIGHT_ASYMMETRIC	1
#define ALGORITHM_LEFT_SYMMETRIC	2
#define ALGORITHM_RIGHT_SYMMETRIC	3

#endif
