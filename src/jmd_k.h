#ifndef __JMD_K_H__
#define __JMD_K_H__

#define THREAD_WAKEUP  0

#include "jmd_p.h"
#define MD_RESERVED 0UL;
#define LINEAR            1UL
#define STRIPED           2UL
#define RAID0             STRIPED
#define RAID1             3UL
#define RAID5             4UL
#define TRANSLUCENT       5UL
#define HSM               6UL
#define MAX_PERSONALITY   7UL

/*
 * options passed in raidrun:
 */

#define MAX_CHUNK_SIZE (4096*1024)

/*
 * default readahead
 */
#define MD_READAHEAD	MAX_READAHEAD

extern unsigned short JMD_MAJOR;


extern inline int disk_faulty(struct jmd_disk * d)
{
	return d->state & (1 << MD_DISK_FAULTY);
}

extern inline int disk_active(struct jmd_disk * d)
{
	return d->state & (1 << MD_DISK_ACTIVE);
}

extern inline int disk_sync(struct jmd_disk * d)
{
	return d->state & (1 << MD_DISK_SYNC);
}

extern inline int disk_spare(struct jmd_disk * d)
{
	return !disk_sync(d) && !disk_active(d) && !disk_faulty(d);
}

extern inline int disk_removed(struct jmd_disk * d)
{
	return d->state & (1 << MD_DISK_REMOVED);
}

extern inline void mark_disk_faulty(struct jmd_disk * d)
{
	d->state |= (1 << MD_DISK_FAULTY);
}

extern inline void mark_disk_active(struct jmd_disk * d)
{
	d->state |= (1 << MD_DISK_ACTIVE);
}

extern inline void mark_disk_sync(struct jmd_disk * d)
{
	d->state |= (1 << MD_DISK_SYNC);
}

extern inline void mark_disk_spare(struct jmd_disk * d)
{
	d->state = 0;
}

extern inline void mark_disk_removed(struct jmd_disk * d)
{
	d->state = (1 << MD_DISK_FAULTY) | (1 << MD_DISK_REMOVED);
}

extern inline void mark_disk_inactive(struct jmd_disk * d)
{
	d->state &= ~(1 << MD_DISK_ACTIVE);
}

extern inline void mark_disk_nonsync(struct jmd_disk * d)
{
	d->state &= ~(1 << MD_DISK_SYNC);
}

extern inline int pers_to_level(int pers)
{
	switch (pers) {
		case HSM:		return -3;
		case TRANSLUCENT:	return -2;
		case LINEAR:		return -1;
		case RAID0:		return 0;
		case RAID1:		return 1;
		case RAID5:		return 5;
	}
	panic("pers_to_level()");
}

extern inline int level_to_pers(int level)
{
	switch (level) {
		case -3: return HSM;
		case -2: return TRANSLUCENT;
		case -1: return LINEAR;
		case 0: return RAID0;
		case 1: return RAID1;
		case 4:
		case 5: return RAID5;
	}
	return MD_RESERVED;
}

struct jmddev {
	void				*private;
	struct jmd_personality		*pers;
	int				__minor;
	struct jmd_superblock		*sb;
	atomic_t			pending_writes;	/* number of active superblock writes */
	int				nb_dev;
	struct list_head 		disks;
	int				sb_dirty;
	struct jmd_param		param;
	int				ro;
	unsigned long			curr_resync;	/* blocks scheduled */
	unsigned long			resync_mark;	/* a recent timestamp */
	unsigned long			resync_mark_cnt;/* blocks written at resync_mark */
	char				*name;
	int				recovery_running;
	struct semaphore		reconfig_sem;
	struct semaphore		recovery_sem;
	struct semaphore		resync_sem;
	atomic_t			active;

	atomic_t			recovery_active; /* blocks scheduled, but not written */
	wait_queue_head_t		recovery_wait;
        struct request_queue		*queue;	/* for plugging ... */

	struct gendisk			*gendisk;
	struct kobject			kobj;

	struct list_head		all_jmddevs;

        /*define: */
	atomic_t			openers;
        sector_t                        dev_sectors; /* used size of component devices */
        sector_t                        array_sectors; /* used size of component devices */

        int                             chunk_sectors; /* used size of component devices */

        /*define: */
#define	MD_RECOVERY_RUNNING	0
#define	MD_RECOVERY_SYNC	1
#define	MD_RECOVERY_ERR		2
#define	MD_RECOVERY_INTR	3
#define	MD_RECOVERY_DONE	4
#define	MD_RECOVERY_NEEDED	5
        struct jmd_thread               *thread;
        struct jmd_thread               *sync_thread;
	unsigned long			recovery;
        
        // for test
        wait_queue_head_t       twait;
        int tvalue;
};


/*
 * MD's 'extended' device
 */
struct jmd_rdev {
	struct list_head same_set;	/* RAID devices within the same set */
	struct list_head all;	/* all RAID devices */
	struct list_head pending;	/* undetected RAID devices */

	dev_t dev;			/* Device number */
	dev_t old_dev;			/*  "" when it was last imported */
	unsigned long size;		/* Device size (in blocks) */
	struct jmddev *jmddev;			/* RAID array if running */
	unsigned long last_events;	/* IO event timestamp */

	struct block_device *bdev;	/* block device handle */
        struct page *sb_page;
        int sb_loaded;

	struct jmd_superblock *sb;
	unsigned long sb_offset;

	int faulty;			/* if faulty do not issue IO requests */
	int desc_nr;			/* descriptor index in the superblock */
        /*define: */
        int in_sync;
        atomic_t        nr_pending;
        sector_t        data_offset;

        //for non stripe op
        struct bio *bio;
};

static inline void rdev_dec_pending(struct jmd_rdev *jrdev, struct jmddev *jmddev)
{
	int faulty = jrdev->faulty;
	if (atomic_dec_and_test(&jrdev->nr_pending) && faulty)
		set_bit(MD_RECOVERY_NEEDED, &jmddev->recovery);
}

static inline void jmd_sync_acct(struct block_device *bdev, unsigned long nr_sectors)
{
        atomic_add(nr_sectors, &bdev->bd_contains->bd_disk->sync_io);
}

/*
 * Currently we index md_array directly, based on the minor
 * number. This will have to change to dynamic allocation
 * once we start supporting partitioning of md devices.
 */
extern inline int jmdidx(struct jmddev *jmddev)
{
	return jmddev->__minor;
}

extern inline dev_t jmddev_to_dev(struct jmddev *jmddev)
{
	return MKDEV(JMD_MAJOR, jmdidx(jmddev));
}

struct jmd_personality {
	char *name;
	int (*make_request)(struct jmddev *jmddev, struct bio * bi);
	int (*run)(struct jmddev *jmddev);
	int (*stop)(struct jmddev *jmddev);
	int (*status)(char *page, struct jmddev *jmddev);
	int (*error_handler)(struct jmddev *jmddev, dev_t dev);

/*
 * Some personalities (RAID-1, RAID-5) can have disks hot-added and
 * hot-removed. Hot removal is different from failure. (failure marks
 * a disk inactive, but the disk is still part of the array) The interface
 * to such operations is the 'pers->diskop()' function, can be NULL.
 *
 * the diskop function can change the pointer pointing to the incoming
 * descriptor, but must do so very carefully. (currently only
 * SPARE_ACTIVE expects such a change)
 */
	int (*diskop) (struct jmddev *jmddev, struct jmd_disk **descriptor, int state);

	int (*stop_resync)(struct jmddev *jmddev);
	int (*restart_resync)(struct jmddev *jmddev);
	int (*sync_request)(struct jmddev *jmddev, unsigned long block_nr);
};

struct jmd_thread {
	void			(*run) (struct jmd_thread *thread);
	struct jmddev           *jmddev;
	wait_queue_head_t	wqueue;
	unsigned long           flags;
	struct semaphore	*sem;
	struct task_struct	*tsk;
	const char		*name;
        /*JDEBUG*/
        unsigned long          timeout;
};

#define MAX_DISKNAME_LEN 64
struct dev_name {
	struct list_head list;
	dev_t dev;
	char namebuf [MAX_DISKNAME_LEN];
	char *name;
};

/*
 * iterates through some jrdev ringlist. It's safe to remove the
 * current 'jrdev'. Dont touch 'tmp' though.
 */
#define ITERATE_RDEV_GENERIC(head,field,jrdev,tmp)			\
									\
	for (tmp = head.next;						\
		jrdev = list_entry(tmp, struct jmd_rdev, field),		\
			tmp = tmp->next, tmp->prev != &head		\
		; )

/*
 * iterates through the 'same array disks' ringlist
 */
#define ITERATE_RDEV(jmddev,jrdev,tmp)					\
	ITERATE_RDEV_GENERIC((jmddev)->disks,same_set,jrdev,tmp)


/*
 * iterates through all used mddevs in the system.
 */
#define ITERATE_MDDEV(jmddev,tmp)					\
	for (tmp = all_jmddevs.next;					\
		jmddev = list_entry(tmp, struct jmddev, all_jmddevs),	\
			tmp = tmp->next, tmp->prev != &all_jmddevs	\
		; )

extern void jmd_print_devices (void);

#define MD_BUG(x...) { printk("md: bug in file %s, line %d\n", __FILE__, __LINE__); jmd_print_devices(); }


#endif
