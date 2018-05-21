/* *  jmd.c interface
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
#include <linux/blkdev.h>
#include <linux/kthread.h>
#include <linux/random.h>
#include <linux/kobject.h>
#include <linux/sysfs.h>
#include <asm/checksum.h>
#include <linux/kdev_t.h>
#include <linux/fs.h>
#include <linux/version.h>

#include "jmd.h"
#include "jmd_k.h"
#include "jmd_u.h"

#define MAX_NAME_LEN 56
#define MAX_READAHEAD	31
#define MAX_SECTORS 254

static LIST_HEAD(all_jraid_disks);
static LIST_HEAD(device_names);
static LIST_HEAD(all_jmddevs);

int jmd_size[MAX_MD_DEVS];

unsigned short JMD_MAJOR = 0;
EXPORT_SYMBOL_GPL(JMD_MAJOR);

struct kobject *kobj_base;
static struct jmd_thread *jmd_recovery_thread = NULL;

static struct jmd_personality *pers[MAX_PERSONALITY];

struct hd_struct jmd_hd_struct[MAX_MD_DEVS];
static int jmd_blocksizes[MAX_MD_DEVS];
static int jmd_hardsect_sizes[MAX_MD_DEVS];
static int jmd_maxreadahead[MAX_MD_DEVS];

struct jmddev *g_jmddev;
EXPORT_SYMBOL_GPL(g_jmddev);
static int add_new_disk(struct jmddev *jmddev, struct jmd_disk_info *info);

static sector_t calc_dev_sboffset(dev_t dev, struct jmddev *jmddev,
						int persistent);
static unsigned int calc_sb_csum(struct jmd_superblock *sb);
static int do_jmd_run(struct jmddev *jmddev);
static int set_array_info(struct jmddev *jmddev, struct jmd_array_info *info);

static void array_info_init(struct jmd_array_info *array_info)
{
        array_info->state = 0; 
        array_info->level = 5; 
        array_info->size = 0; 
        array_info->chunk_size = 32*1024;

        array_info->md_minor = 0;
        array_info->nr_disks = 3;
        array_info->raid_disks = 3;
        array_info->active_disks = 0;
        array_info->working_disks = 0;
        array_info->failed_disks = 0;
        array_info->spare_disks = 0;

        array_info->layout = 0; //ALGORITHM_LEFT_ASYMMETRIC
        array_info->not_persistent = 0;
}

struct define_tmp_device {
        struct block_device *bdev;
};

static sector_t
jmddev_size(struct jmddev *jmddev, int jraid_disks)
{
        sector_t sectors;

        if(!jmddev->sb)
                return 0;
        sectors = jmddev->sb->size / 2;
        sectors &= ~((sector_t)jmddev->chunk_sectors -1);

        return sectors * (jraid_disks - 1);
}

static ssize_t
add_store(struct kobject *kobj, struct kobj_attribute *attr,
				const char *page, size_t cnt)
{
	char filename[MAX_NAME_LEN];
        int total_num,jrdev_num;
	struct jmddev *jmddev = g_jmddev;
        struct jmd_array_info *array_info; 
        struct jmd_disk_info *info; 
        struct define_tmp_device *dtdev;
        sector_t sectors;

        if(!jmddev)
                return -ENOMEM;

        dtdev = (struct define_tmp_device *)kzalloc(sizeof(struct define_tmp_device), GFP_KERNEL);
        if(!dtdev)
                return -ENOMEM;

	sscanf(page, "%s %d %d",filename, &total_num, &jrdev_num);

        dtdev->bdev = blkdev_get_by_path(filename, FMODE_WRITE | FMODE_READ | FMODE_EXCL,
                                dtdev);
	array_info = (struct jmd_array_info *)kzalloc(sizeof(struct jmd_array_info), GFP_KERNEL);
        if(!array_info)
                goto err_array_info;

        array_info_init(array_info);
        array_info->nr_disks = total_num;
        array_info->raid_disks = total_num;

	info = (struct jmd_disk_info *)kzalloc(sizeof(struct jmd_disk_info), GFP_KERNEL);
        if(!info)
                goto err_info;

        info->major = dtdev->bdev->bd_disk->major;
        info->minor = dtdev->bdev->bd_disk->first_minor;
        info->raid_disk = jrdev_num;
        info->number = jrdev_num;
        printk(KERN_INFO "\njdrev major=%d minor=%d\n", info->major, info->minor);

        if (jmddev->nb_dev == 0){
                printk("Set array info %d ...\n", __LINE__);
                set_array_info(jmddev, array_info);
        }
	blkdev_put(dtdev->bdev, FMODE_READ|FMODE_WRITE|FMODE_EXCL);

        add_new_disk(jmddev, info);
        if (jmddev->nb_dev == total_num){
                printk("\nRun array %d ...\n", __LINE__);
                sectors = jmddev_size(jmddev, total_num+1);
                jmddev->array_sectors = sectors;
                //for simple IO test
                //jmddev->array_sectors = sectors / total_num;

                set_capacity(jmddev->gendisk, jmddev->array_sectors);
                printk("array capacity: %lu ...\n", jmddev->array_sectors);
                do_jmd_run(jmddev);
        }

        kfree(info);
        kfree(array_info);
        kfree(dtdev);
        return (size_t)cnt;

err_info:
        kfree(array_info);

err_array_info:
        kfree(dtdev);
        return -ENOMEM;
}

struct jmd_rdev *jrdev_get_by_name(const char *filename)
{
        return NULL;
}

int jrdev_delete(struct jmd_rdev *jrdev)
{
        return 0;
}

static ssize_t
del_store(struct kobject *kobj, struct kobj_attribute *attr,
				const char *page, size_t cnt)
{
        struct jmd_rdev *jrdev;
	char filename[MAX_NAME_LEN];
	sscanf(page, "%s",filename);
        jrdev = jrdev_get_by_name(filename);
        if(jrdev){
                jrdev_delete(jrdev);
                return (size_t)cnt;
        }

        return -EINVAL; 
}

static ssize_t
mngt_show(struct kobject *kobj, struct kobj_attribute *attr, 
				char *page)
{
	ssize_t ret = 0;

	ret += snprintf(page, PAGE_SIZE, 
			"echo filename total_num jrdev_num > \
                        /sys/block/jmd[num]/jmd/add\n");
	ret += snprintf(page + ret, PAGE_SIZE, 
			"echo filename total_num jrdev_num > \
                        /sys/block/jmd[num]/jmd/del\n");

	ret += snprintf(page + ret, PAGE_SIZE, 
			"echo /dev/vg0/lv0 4 0 > \
                        /sys/block/jmd[num]/jmd/add\n");
	return ret;
}

static struct kobj_attribute jmds_kobj_attr[] = {
	__ATTR(mngt, S_IRUGO, mngt_show, NULL),
	__ATTR(add, S_IWUSR, NULL, add_store),
	__ATTR(del, S_IWUSR, NULL, del_store),
};

static const struct attribute *jmds_attrs[] = {
	&jmds_kobj_attr[0].attr,
	&jmds_kobj_attr[1].attr,
	&jmds_kobj_attr[2].attr,
	NULL,
};


static struct jmd_sysfs_entry define_load = 
__ATTR(add, S_IWUSR, NULL, add_store);


static struct attribute *jmd_define_attrs[] = {
        &define_load.attr,
        NULL
};

struct attribute_group jmd_define_group = {
        .name = "define",
        .attrs = jmd_define_attrs,

}; 

static int alloc_array_sb(struct jmddev* jmddev)
{
	if (jmddev->sb) {
		MD_BUG();
		return 0;
	}

	jmddev->sb = (struct jmd_superblock *)kzalloc(sizeof(struct jmd_superblock), 
                                                        GFP_KERNEL);
	if (!jmddev->sb)
		return -ENOMEM;

	return 0;
}

static int set_array_info(struct jmddev *jmddev, struct jmd_array_info *info)
{
	if (alloc_array_sb(jmddev))
	        return -ENOMEM;

        jmddev->sb->size= info->size;
	jmddev->sb->chunk_size = 32*1024;
        jmddev->chunk_sectors = 32*1024/2;
        jmddev->sb->level = info->level;
	jmddev->sb->layout = info->layout;

        jmddev->sb->nr_disks = info->nr_disks;
        jmddev->sb->raid_disks = info->raid_disks;
        jmddev->sb->md_minor= info->md_minor;
        jmddev->sb->not_persistent = info->not_persistent;

	jmddev->sb->md_magic = MD_SB_MAGIC;
	jmddev->sb->ctime = get_seconds();

	jmddev->sb->state = info->state;
	jmddev->sb->active_disks = info->active_disks;
	jmddev->sb->working_disks = info->working_disks;
	jmddev->sb->failed_disks = info->failed_disks;
	jmddev->sb->spare_disks = info->spare_disks;

	jmddev->sb->major_version = MD_MAJOR_VERSION;
	jmddev->sb->patch_version = MD_PATCHLEVEL_VERSION;

	/*
	 * Generate a 128 bit UUID
	 */
	get_random_bytes(&jmddev->sb->set_uuid0, 4);
	get_random_bytes(&jmddev->sb->set_uuid1, 4);
	get_random_bytes(&jmddev->sb->set_uuid2, 4);
	get_random_bytes(&jmddev->sb->set_uuid3, 4);

	return 0;
}

static struct jmd_rdev *find_jrdev_all(dev_t dev)
{
	struct list_head *tmp;
	struct jmd_rdev *jrdev;

	tmp = all_jraid_disks.next;
	while (tmp != &all_jraid_disks) {
		jrdev = list_entry(tmp, struct jmd_rdev, all);
		if (jrdev->dev == dev)
			return jrdev;
		tmp = tmp->next;
	}
	return NULL;
}

struct gendisk *find_gendisk(dev_t dev)
{
	struct gendisk *tmp = NULL;

	while (tmp != NULL) {
		if (tmp->major == MAJOR(dev))
			return tmp;
	}
	return NULL;
}

struct jmd_disk *find_jrdev(struct jmddev *jmddev, dev_t dev)
{
	struct list_head *tmp;
	struct jmd_rdev *jrdev;

	ITERATE_RDEV(jmddev,jrdev,tmp) {
		if (jrdev->dev == dev)
			return jrdev;
	}
	return NULL;
}

static int alloc_disk_sb(struct jmd_rdev *jrdev)
{
	if (jrdev->sb_page)
		MD_BUG();

	jrdev->sb_page = alloc_page(GFP_KERNEL);
	if (!jrdev->sb_page) {
		printk(KERN_ALERT "md: out of memory.\n");
		return -ENOMEM;
	}

	return 0;
}

static void free_disk_sb(struct jmd_rdev *jrdev)
{
	if (jrdev->sb_page) {
		put_page(jrdev->sb_page);
	        jrdev->sb = NULL;
		jrdev->sb_page = NULL;
		jrdev->sb_offset = 0;
		jrdev->size = 0;
	} else {
		if (!jrdev->faulty)
			MD_BUG();
	}
}

/*
 * prevent the device from being mounted, repartitioned or
 * otherwise reused by a RAID array (or any other kernel
 * subsystem), by bd_claiming the device.
 */
static int lock_jrdev(struct jmd_rdev *jrdev, dev_t dev)
{
	int err = 0;
	struct block_device *bdev;
	char b[BDEVNAME_SIZE];

	bdev = blkdev_get_by_dev(dev, FMODE_READ|FMODE_WRITE|FMODE_EXCL, jrdev);
	if (IS_ERR(bdev)) {
		printk(KERN_ERR "md: could not open %s.\n",
			__bdevname(dev, b));
		return PTR_ERR(bdev);
	}
	jrdev->bdev = bdev;
	return err;
}

static void unlock_jrdev(struct jmd_rdev *jrdev)
{
	struct block_device *bdev = jrdev->bdev;
	jrdev->bdev = NULL;
	if (!bdev)
		MD_BUG();
	blkdev_put(bdev, FMODE_READ|FMODE_WRITE|FMODE_EXCL);
}

int sync_page_io(struct jmd_rdev *jrdev, sector_t sector, int size,
		 struct page *page, int rw, bool metadata_op)
{
	struct bio *bio = bio_kmalloc(GFP_KERNEL, 1);
	int ret;

	bio->bi_bdev = metadata_op ? jrdev->bdev : NULL;
	bio->bi_sector = sector + jrdev->sb_offset;

	bio_add_page(bio, page, size, 0);
	submit_bio_wait(rw, bio);

#if LINUX_VERSION_CODE < KERNEL_VERSION(3,18,12)
	ret = test_bit(BIO_UPTODATE, &bio->bi_flags);
#else
        ret = !bio->bi_error;
#endif
	bio_put(bio);
	return ret;
}
EXPORT_SYMBOL_GPL(sync_page_io);

static int read_disk_sb(struct jmd_rdev *jrdev, int size)
{
        printk("In ... %s\n", __func__);
	char b[BDEVNAME_SIZE];
	if (!jrdev->sb_page) {
		MD_BUG();
		return -EINVAL;
	}
	if (jrdev->sb_loaded)
		return 0;


	if (!sync_page_io(jrdev, 0, size, jrdev->sb_page, READ, true))
		goto fail;
	jrdev->sb_loaded = 1;
	return 0;

fail:
	printk(KERN_WARNING "md: disabled device %s, could not read superblock.\n",
		bdevname(jrdev->bdev,b));
	return -EINVAL;
}

/*
 * Check one RAID superblock for generic plausibility
 */
static int check_disk_sb(struct jmd_rdev *jrdev)
{
	struct jmd_superblock *sb;
	int ret = -EINVAL;

	sb = jrdev->sb;
	if (!sb) {
		MD_BUG();
		goto abort;
	}

	if (sb->md_magic != MD_SB_MAGIC) {
		printk ("BAD_MAGIC\n");
		goto abort;
	}

	if (sb->md_minor >= MAX_MD_DEVS) {
		printk ("BAD_MINOR\n");
		goto abort;
	}

	if (calc_sb_csum(sb) != sb->sb_csum)
		printk("BAD_CSUM\n");
	ret = 0;
abort:
	return ret;
}

/*
 * Import a device. If 'on_disk', then sanity check the superblock
 *
 * mark the device faulty if:
 *
 *   - the device is nonexistent (zero size)
 *   - the device has no valid superblock
 *
 * a faulty rdev _never_ has rdev->sb set.
 */
static int jmd_import_device(dev_t newdev, int on_disk)
{
	int err;
	struct jmd_rdev *jrdev;
	//unsigned int size;

	if (find_jrdev_all(newdev))
		return -EEXIST;

	jrdev = (struct jmd_rdev *)kmalloc(sizeof(*jrdev), GFP_KERNEL);
	if (!jrdev) {
		printk("could not alloc mem for s!\n");
                printk("[%s-%d]: could not alloc mem jrdev\n", __func__, __LINE__);
		return -ENOMEM;
	}
	memset(jrdev, 0, sizeof(*jrdev));

	if (get_super(newdev)) {
		printk("md: can not import s, has active inodes!\n");
		err = -EBUSY;
		goto abort_free;
	}

	if ((err = alloc_disk_sb(jrdev)))
		goto abort_free;

	if (lock_jrdev(jrdev, newdev)) {
		printk("md: could not lock s, zero-size? Marking faulty.\n");
		err = -EINVAL;
		goto abort_free;
	}
	jrdev->dev = newdev;
	jrdev->desc_nr = -1;
	jrdev->faulty = 0;

        jrdev->sb = page_address(jrdev->sb_page);

	if (on_disk) {
		if ((err = read_disk_sb(jrdev, 4096))) {
			printk("md: could not read s's sb, not importing!\n");
			goto abort_free;
		}
#if 1
                char *buf;
                int i;
                buf = page_address(jrdev->sb_page);
                printk(KERN_INFO "%s-%d: print date\n", __func__, __LINE__);
                for (i = 0; i < 3; i++)
                        printk(KERN_INFO "%x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x", 
                                        *buf++, *buf++,*buf++,*buf++,*buf++,*buf++,*buf++,*buf++,
                                        *buf++,*buf++,*buf++,*buf++,*buf++,*buf++,*buf++,*buf++);
                printk(KERN_INFO "%s-%d: print end\n", __func__, __LINE__);
#endif
		if (err = check_disk_sb(jrdev)) {
			printk("md: s has invalid sb, not importing!\n");
			goto abort_free;
		}

		jrdev->old_dev = MKDEV(jrdev->sb->this_disk.major,
					jrdev->sb->this_disk.minor);
		jrdev->desc_nr = jrdev->sb->this_disk.number;
	}
	list_add(&jrdev->all, &all_jraid_disks);
	INIT_LIST_HEAD(&jrdev->pending);

	if (jrdev->faulty && jrdev->sb)
		free_disk_sb(jrdev);
	return 0;

abort_free:
	if (jrdev->sb) {
		if (jrdev->bdev)
		        unlock_jrdev(jrdev);
		free_disk_sb(jrdev);
	}
	kfree(jrdev);
	return err;
}

static int uuid_equal(struct jmd_rdev *jrdev1, struct jmd_rdev *jrdev2)
{
	if (	(jrdev1->sb->set_uuid0 == jrdev2->sb->set_uuid0) &&
		(jrdev1->sb->set_uuid1 == jrdev2->sb->set_uuid1) &&
		(jrdev1->sb->set_uuid2 == jrdev2->sb->set_uuid2) &&
		(jrdev1->sb->set_uuid3 == jrdev2->sb->set_uuid3))

		return 1;

	return 1;
}

static void export_jrdev(struct jmd_rdev *jrdev)
{
	printk("export_jrdev(s)\n");
	if (jrdev->jmddev)
		MD_BUG();
	unlock_jrdev(jrdev);
	free_disk_sb(jrdev);
	list_del(&jrdev->all);
	INIT_LIST_HEAD(&jrdev->all);
	if (jrdev->pending.next != &jrdev->pending) {
		printk("(s was pending)\n");
		list_del(&jrdev->pending);
		INIT_LIST_HEAD(&jrdev->pending);
	}
	jrdev->dev = 0;
	jrdev->faulty = 0;
	kfree(jrdev);
}

static struct jmd_rdev *match_dev_unit(struct jmddev *jmddev, dev_t dev)
{
	struct list_head *tmp;
	struct jmd_rdev *jrdev;

	ITERATE_RDEV(jmddev,jrdev,tmp)
                ;

	return NULL;
}

static void bind_rdev_to_array (struct jmd_rdev *jrdev, struct jmddev *jmddev)
{
	struct jmd_rdev *same_pdev;

	if (jrdev->jmddev) {
		MD_BUG();
		return;
	}
	same_pdev = match_dev_unit(jmddev, jrdev->dev);
	if (same_pdev)
		printk( KERN_WARNING
"md%d: WARNING: s appears to be on the same physical disk as s. True\n"
"     protection against single-disk failure might be compromised.\n",
 			jmdidx(jmddev));
		
	list_add(&jrdev->same_set, &jmddev->disks);
	jrdev->jmddev = jmddev;
	jmddev->nb_dev++;
	printk("bind<s,%d>\n", jmddev->nb_dev);
	printk("bind<%d,%d>\n", jrdev->dev, jmddev->nb_dev);
}

static sector_t calc_dev_sboffset(dev_t dev, struct jmddev *jmddev, int persistent)
{
	sector_t size = 0;
        struct jmd_rdev *jrdev=NULL;
        
        printk("superblock version [0.9], MAJOR(dev) = %u\n", MAJOR(dev));
        jrdev = find_jrdev(jmddev, dev);
        if(!jrdev){
                printk("Error find jrdev\n");
                return -EINVAL;
        }
        size = i_size_read(jrdev->bdev->bd_inode)/512;
        //printk(" 1->size = %u\t", size);

        if (persistent)
		size = MD_NEW_SIZE_BLOCKS(size);
        //printk("2->size = %u\n", size);

	return size;
}

static sector_t calc_dev_size(dev_t dev, struct jmddev *jmddev, int persistent)
{
	sector_t size;

	size = calc_dev_sboffset(dev, jmddev, persistent);
	if (!jmddev->sb) {
		MD_BUG();
		return size;
	}
	if (jmddev->sb->chunk_size)
		size &= ~(jmddev->sb->chunk_size/512 - 1);
	return size;
}

static void set_this_disk(struct jmddev *jmddev, struct jmd_rdev *jrdev)
{
	int i, ok = 0;
	struct jmd_disk *desc;

	for (i = 0; i < MD_SB_DISKS; i++) {
		desc = jmddev->sb->disks + i;
		if (MKDEV(desc->major,desc->minor) == jrdev->dev) {
			jrdev->sb->this_disk = *desc;
			jrdev->desc_nr = desc->number;
			ok = 1;
			break;
		}
	}

	if (!ok) {
		MD_BUG();
	}
}

static unsigned int calc_sb_csum(struct jmd_superblock *sb)
{
	unsigned int disk_csum, csum;

	disk_csum = sb->sb_csum;
	sb->sb_csum = 0;
	csum = csum_partial((void *)sb, MD_SB_BYTES, 0);
	sb->sb_csum = disk_csum;
	return csum;
}

static int sync_sbs(struct jmddev *jmddev)
{
	struct jmd_rdev *jrdev;
	struct jmd_superblock *sb;
	struct list_head *tmp;

	ITERATE_RDEV(jmddev,jrdev,tmp) {
		if (jrdev->faulty)
			continue;
		sb = jrdev->sb;
		*sb = *jmddev->sb;
		set_this_disk(jmddev, jrdev);
		sb->sb_csum = calc_sb_csum(sb);
	}
	return 0;
}

void jmd_print_devices (void)
{
	struct list_head *tmp, *tmp2;
	struct jmd_rdev *jrdev;
	struct jmddev *jmddev;

	printk("\n");
	printk("	**********************************\n");
	printk("	* <COMPLETE RAID STATE PRINTOUT> *\n");
	printk("	**********************************\n");
	printk("	**********************************\n");
	ITERATE_MDDEV(jmddev,tmp) {
		printk("jmd%d: ", jmdidx(jmddev));

		ITERATE_RDEV(jmddev,jrdev,tmp2)
                        ;

		if (jmddev->sb) {
			printk(" array superblock:\n");
		} else
			printk(" no array superblock.\n");

		ITERATE_RDEV(jmddev,jrdev,tmp2)
                        ;
        }	
	printk("	**********************************\n");
	printk("\n");
}
EXPORT_SYMBOL_GPL(jmd_print_devices);

static int add_new_disk(struct jmddev *jmddev, struct jmd_disk_info *info)
{
	int err, size, persistent;
	struct jmd_rdev *jrdev;
	unsigned int nr;
	dev_t dev;
	dev = MKDEV(info->major,info->minor);

	if (find_jrdev_all(dev)) {
		printk("device s already used in a JRAID array!\n");
		return -EBUSY;
	}
	if (!jmddev->sb) {
		/* expecting a device which has a superblock */
		err = jmd_import_device(dev, 1);
		if (err) {
			printk("md error, jmd_import_device returned %d\n", err);
			return -EINVAL;
		}
		jrdev = find_jrdev_all(dev);
		if (!jrdev) {
			MD_BUG();
			return -EINVAL;
		}
		if (jmddev->nb_dev) {
			struct jmd_rdev *jrdev0 = list_entry(jmddev->disks.next,
							  struct jmd_rdev, same_set);
			if (!uuid_equal(jrdev0, jrdev)) {
				printk("md: a has different UUID to b\n");
				export_jrdev(jrdev);
				return -EINVAL;
			}
		}
		bind_rdev_to_array(jrdev, jmddev);
		return 0;
	}

	nr = info->number;
	if (nr >= jmddev->sb->nr_disks)
		return -EINVAL;

        jmddev->sb->disks[nr].number = info->number;
        jmddev->sb->disks[nr].major= info->major;
        jmddev->sb->disks[nr].minor = info->minor;
        jmddev->sb->disks[nr].raid_disk = info->raid_disk;
        jmddev->sb->disks[nr].state= info->state;

	if ((info->state & (1<<MD_DISK_FAULTY))==0) {
		err = jmd_import_device(dev, 0);
		if (err) {
			printk("md: error, jmd_import_device() returned %d\n", err);
			return -EINVAL;
		}
		jrdev = find_jrdev_all(dev);
		if (!jrdev) {
			MD_BUG();
			return -EINVAL;
		}

		jrdev->old_dev = dev;
		jrdev->desc_nr = info->number;

		bind_rdev_to_array(jrdev, jmddev);

		persistent = !jmddev->sb->not_persistent;
		if (!persistent)
			printk("nonpersistent superblock ...\n");
		if (!jmddev->sb->chunk_size)
			printk("no chunksize?\n");

		size = calc_dev_size(dev, jmddev, persistent);
		jrdev->sb_offset = calc_dev_sboffset(dev, jmddev, persistent);

		if (!jmddev->sb->size || (jmddev->sb->size > size))
			jmddev->sb->size = size;
                printk("jmddev->sb->size = %u\n",jmddev->sb->size);
	}

	sync_sbs(jmddev);

	return 0;
}

static void remove_descriptor(struct jmd_disk *disk, struct jmd_superblock *sb)
{
	if (disk_active(disk)) {
		sb->working_disks--;
	} else {
		if (disk_spare(disk)) {
			sb->spare_disks--;
			sb->working_disks--;
		} else	{
			sb->failed_disks--;
		}
	}
	sb->nr_disks--;
	disk->major = 0;
	disk->minor = 0;
	mark_disk_removed(disk);
}

static unsigned int zoned_jraid_size(struct jmddev *jmddev)
{
	unsigned int mask;
	struct jmd_rdev *jrdev;
	struct list_head *tmp;

	if (!jmddev->sb) {
		MD_BUG();
		return -EINVAL;
	}
	mask = ~(jmddev->sb->chunk_size/1024 - 1);

	ITERATE_RDEV(jmddev,jrdev,tmp) {
		jrdev->size &= mask;
		jmd_size[jmdidx(jmddev)] += jrdev->size;
	}
	return 0;
}

#define INCONSISTENT KERN_ERR \
"md: fatal superblock inconsistency in %s -- removing from array\n"

#define OUT_OF_DATE KERN_ERR \
"md: superblock update time inconsistency -- using the most recent one\n"

#define OLD_VERSION KERN_ALERT \
"md: md%d: unsupported raid array version %d.%d.%d\n"

#define NOT_CLEAN_IGNORE KERN_ERR \
"md: md%d: raid array is not clean -- starting background reconstruction\n"

#define UNKNOWN_LEVEL KERN_ERR \
"md: md%d: unsupported raid level %d\n"

static int analyze_sbs(struct jmddev *jmddev)
{
	int out_of_date = 0, i;
	struct list_head *tmp, *tmp2;
	struct jmd_rdev *jrdev, *jrdev2, *freshest;
	struct jmd_superblock *sb;

	ITERATE_RDEV(jmddev,jrdev,tmp) {
		if (jrdev->faulty) {
			MD_BUG();
			goto abort;
		}
		if (!jrdev->sb) {
			MD_BUG();
			goto abort;
		}
		if (check_disk_sb(jrdev))
			goto abort;
	}

	sb = NULL;

	ITERATE_RDEV(jmddev,jrdev,tmp) {
		if (!sb) {
			sb = jrdev->sb;
			continue;
		}
	}
	if (!jmddev->sb)
		if (alloc_array_sb(jmddev))
		        goto abort;
	sb = jmddev->sb;
	freshest = NULL;

	ITERATE_RDEV(jmddev,jrdev,tmp) {
		__u64 ev1, ev2;
		
		if (calc_sb_csum(jrdev->sb) != jrdev->sb->sb_csum) {
		      if (jrdev->sb->events_lo || jrdev->sb->events_hi)
		      	if ((jrdev->sb->events_lo--)==0)
		      		jrdev->sb->events_hi--;
		}

		printk("s's event counter: %08lx\n", (unsigned long)jrdev->sb->events_lo);
		if (!freshest) {
			freshest = jrdev;
			continue;
		}
		ev1 = jmd_event(jrdev->sb);
		ev2 = jmd_event(freshest->sb);
		if (ev1 != ev2) {
			out_of_date = 1;
			if (ev1 > ev2)
				freshest = jrdev;
		}
	}
	if (out_of_date) {
		printk(OUT_OF_DATE);
		printk("freshest: s\n");
	}
	memcpy(sb, freshest->sb, sizeof(*sb));

	ITERATE_RDEV(jmddev,jrdev,tmp) {
		__u64 ev1, ev2;
		ev1 = jmd_event(jrdev->sb);
		ev2 = jmd_event(sb);
		++ev1;
		if (ev1 < ev2) {
			continue;
		}
	}

	ITERATE_RDEV(jmddev,jrdev,tmp) {
		__u64 ev1, ev2, ev3;
		if (jrdev->faulty) { /* REMOVEME */
			MD_BUG();
			goto abort;
		}
		ev1 = jmd_event(jrdev->sb);
		ev2 = jmd_event(sb);
		ev3 = ev2;
		--ev3;
		if ((jrdev->dev != jrdev->old_dev) &&
		    ((ev1 == ev2) || (ev1 == ev3))) {
                        struct jmd_disk *desc;

			if (jrdev->desc_nr == -1) {
				MD_BUG();
				goto abort;
			}
			desc = &sb->disks[jrdev->desc_nr];
			if (jrdev->old_dev != MKDEV(desc->major, desc->minor)) {
				MD_BUG();
				goto abort;
			}
			desc->major = MAJOR(jrdev->dev);
			desc->minor = MINOR(jrdev->dev);
			desc = &jrdev->sb->this_disk;
			desc->major = MAJOR(jrdev->dev);
			desc->minor = MINOR(jrdev->dev);
		}
	}

	for (i = 0; i < MD_SB_DISKS; i++) {
		int found;
		struct jmd_disk *desc;
		dev_t dev;

		desc = sb->disks + i;
		dev = MKDEV(desc->major, desc->minor);

		if (disk_faulty(desc)) {
			found = 0;
			ITERATE_RDEV(jmddev,jrdev,tmp) {
				if (jrdev->desc_nr != desc->number)
					continue;
				found = 1;
				break;
			}
			if (!found) {
				if (dev == MKDEV(0,0))
					continue;
			}
			remove_descriptor(desc, sb);
			continue;
                }	

		if (dev == MKDEV(0,0))
			continue;

		found = 0;
		ITERATE_RDEV(jmddev,jrdev,tmp) {
			if (jrdev->desc_nr == desc->number) {
				found = 1;
				break;
			}
		}
		if (found)
			continue;
		printk("md%d: former device s is unavailable, removing from array!\n", 
                                                                jmdidx(jmddev));
		remove_descriptor(desc, sb);
	}

	for (i = 0; i < MD_SB_DISKS; i++) {
		struct jmd_disk *desc;
		dev_t dev;

		desc = sb->disks + i;
		dev = MKDEV(desc->major, desc->minor);

		if (dev == MKDEV(0,0))
			continue;

		if (disk_faulty(desc)) {
			MD_BUG();
			goto abort;
		}

		jrdev = find_jrdev(jmddev, dev);
		if (!jrdev) {
			MD_BUG();
			goto abort;
		}
	}

	ITERATE_RDEV(jmddev,jrdev,tmp) {
		if (jrdev->desc_nr == -1) {
			MD_BUG();
			goto abort;
		}
		ITERATE_RDEV(jmddev,jrdev2,tmp2) {
			if ((jrdev2 != jrdev) && (jrdev2->desc_nr == jrdev->desc_nr)) {
				MD_BUG();
				goto abort;
			}
		}
		ITERATE_RDEV(jmddev,jrdev2,tmp2) {
			if ((jrdev2 != jrdev) &&
					(jrdev2->dev == jrdev->dev)) {
				MD_BUG();
				goto abort;
			}
		}
	}

	if (sb->major_version != MD_MAJOR_VERSION ||
			sb->minor_version > MD_MINOR_VERSION) {
		printk (OLD_VERSION, jmdidx(jmddev), sb->major_version,
				sb->minor_version, sb->patch_version);
		goto abort;
	}

	if ((sb->state != (1 << MD_SB_CLEAN)) && ((sb->level == 1) ||
			(sb->level == 4) || (sb->level == 5)))
		printk(NOT_CLEAN_IGNORE, jmdidx(jmddev));

	return 0;
abort:
	return 1;
}

static int device_size_calculation(struct jmddev *jmddev)
{
	int data_disks = 0, persistent;
	unsigned int readahead;
	struct jmd_superblock *sb = jmddev->sb;
	struct list_head *tmp;
	struct jmd_rdev *jrdev;

	persistent = !jmddev->sb->not_persistent;
	ITERATE_RDEV(jmddev,jrdev,tmp) {
		if (jrdev->faulty)
			continue;
		if (jrdev->size) {
			MD_BUG();
			continue;
		}
		jrdev->size = calc_dev_size(jrdev->dev, jmddev, persistent);
		if (jrdev->size < sb->chunk_size / 512) {
			printk (KERN_WARNING
				"Dev s smaller than chunk_size: %ldk < %dk\n",
				jrdev->size, sb->chunk_size / 1024);
			return -EINVAL;
		}
	}

	switch (sb->level) {
		case -3:
			data_disks = 1;
			break;
		case -2:
			data_disks = 1;
			break;
		case -1:
			zoned_jraid_size(jmddev);
			data_disks = 1;
			break;
		case 0:
			zoned_jraid_size(jmddev);
			data_disks = sb->raid_disks;
			break;
		case 1:
			data_disks = 1;
			break;
		case 4:
		case 5:
			data_disks = sb->raid_disks - 1;
                        printk("in case 4/5 data_disks = %u\n", data_disks);
			break;
		default:
			printk (UNKNOWN_LEVEL, jmdidx(jmddev), sb->level);
			goto abort;
	}
	if (!jmd_size[jmdidx(jmddev)])
		jmd_size[jmdidx(jmddev)] = sb->size * data_disks;

	readahead = MD_READAHEAD;
        printk("readahead = %u\n", readahead);
	if ((sb->level == 0) || (sb->level == 4) || (sb->level == 5)) {
		readahead = (jmddev->sb->chunk_size>>PAGE_SHIFT) * 4 * data_disks;
		if (readahead < data_disks * (MAX_SECTORS>>(PAGE_SHIFT-9))*2)
			readahead = data_disks * (MAX_SECTORS>>(PAGE_SHIFT-9))*2;
	} else {
		if (sb->level == -3)
			readahead = 0;
	}
	jmd_maxreadahead[jmdidx(jmddev)] = readahead;

	printk(KERN_INFO "md%d: max total readahead window set to %ldk\n",
		jmdidx(jmddev), readahead*(PAGE_SIZE/1024));

	//printk(KERN_INFO
	//	"md%d: %d data-disks, max readahead per data-disk: %ldk\n",
	//		jmdidx(jmddev), data_disks, readahead/data_disks*(PAGE_SIZE/1024));
	return 0;
abort:
	return 1;
}

static void super_written(struct bio *bio, int error)
{
	struct jmd_rdev *jrdev = bio->bi_private;
	struct jmddev *jmddev = jrdev->jmddev;

#if LINUX_VERSION_CODE < KERNEL_VERSION(3,18,12)
	if (error || !test_bit(BIO_UPTODATE, &bio->bi_flags)) {
		printk("md: super_written gets error=%d, uptodate=%d\n",
		       error, test_bit(BIO_UPTODATE, &bio->bi_flags));
		WARN_ON(test_bit(BIO_UPTODATE, &bio->bi_flags));
#else
        if (bio->bi_error) {
                printk("md: super_written gets error=%d\n", bio->bi_error);
#endif
	}

	if (atomic_dec_and_test(&jmddev->pending_writes))
                ;
	bio_put(bio);
}

static int write_disk_sb(struct jmd_rdev *jrdev)
{
	dev_t dev;
	sector_t sb_offset, size;
        struct jmd_superblock *sb;
	struct bio *bio = bio_kmalloc(GFP_KERNEL, 1);
        struct page *page;
	if (!bio) {
		return -ENOMEM;
	}
	page = alloc_page(GFP_KERNEL);
	if (!page) {
		bio_put(bio);
		return -ENOMEM;
	}
	if (!jrdev->sb) {
		MD_BUG();
		return -1;
	}
	if (jrdev->faulty) {
		MD_BUG();
		return -1;
	}
	if (jrdev->sb->md_magic != MD_SB_MAGIC) {
		MD_BUG();
		return -1;
	}

	dev = jrdev->dev;
	sb_offset = calc_dev_sboffset(dev, jrdev->jmddev, 1);
        printk("submit_bio sb_offset = %u ...\n", sb_offset);
	if (jrdev->sb_offset != sb_offset) {
		printk("s's sb offset has changed from %ld to %ld, skipping\n", 
                                                jrdev->sb_offset, sb_offset);
		goto skip;
	}
	size = calc_dev_size(dev, jrdev->jmddev, 1);
	if (size != jrdev->size) {
		printk("s's size has changed from %ld to %ld since import, skipping\n", 
                                                        jrdev->size, size);
		goto skip;
	}

	bio->bi_bdev = jrdev->bdev;
	bio->bi_sector = sb_offset;
	bio_add_page(bio, page, PAGE_SIZE, 0);
	bio->bi_private = jrdev;
	bio->bi_end_io = super_written;

        memcpy(page_address(page), jrdev->sb, sizeof(struct jmd_superblock));
        smp_mb();
        submit_bio(WRITE, bio);

	atomic_inc(&jrdev->jmddev->pending_writes);

        return 0;

skip:
	return 1;
}

int jmd_update_sb(struct jmddev *jmddev)
{
	int first, err, count = 100;
	struct list_head *tmp;
	struct jmd_rdev *jrdev;
        printk("\nIn update sb ...\n");

repeat:
	jmddev->sb->utime = get_seconds();
	if ((++jmddev->sb->events_lo)==0)
		++jmddev->sb->events_hi;

	if ((jmddev->sb->events_lo | jmddev->sb->events_hi)==0) {
		MD_BUG();
		jmddev->sb->events_lo = jmddev->sb->events_hi = 0xffffffff;
	}
	sync_sbs(jmddev);

	/*
	 * do not write anything to disk if using
	 * nonpersistent superblocks
	 */
	if (jmddev->sb->not_persistent)
		return 0;

	printk(KERN_INFO "md: updating md%d RAID superblock on device\n",
					jmdidx(jmddev));

	first = 1;
	err = 0;
	ITERATE_RDEV(jmddev,jrdev,tmp) {
		if (!first) {
			first = 0;
			printk(", ");
		}
		if (jrdev->faulty)
			printk("(skipping faulty ");
		printk("jrdev desc_nr %d\n", jrdev->desc_nr);
		if (!jrdev->faulty) {
			printk("[events: %08lx]\n",
				(unsigned long)jrdev->sb->events_lo);
			err += write_disk_sb(jrdev);
		} else
			printk(")\n");
	}
	printk(".\n");
	if (err) {
		printk("errors occured during superblock update, repeating\n");
		if (--count)
			goto repeat;
		printk("excessive errors occured during superblock update, exiting\n");
	}
	return 0;
}
EXPORT_SYMBOL_GPL(jmd_update_sb);

#define TOO_BIG_CHUNKSIZE KERN_ERR \
"too big chunk_size: %d > %d\n"

#define TOO_SMALL_CHUNKSIZE KERN_ERR \
"too small chunk_size: %d < %ld\n"

#define BAD_CHUNKSIZE KERN_ERR \
"no chunksize specified, see 'man raidtab'\n"

static int do_jmd_run(struct jmddev *jmddev)
{
	int pnum, err;
	int chunk_size;
	struct list_head *tmp;
	struct jmd_rdev *jrdev;

	if (!jmddev->nb_dev) {
		MD_BUG();
		return -EINVAL;
	}
	if (jmddev->pers)
		return -EBUSY;

	jmd_size[jmdidx(jmddev)] = 0;

	/*
	 * Analyze all RAID superblock(s)
	 */
	if (analyze_sbs(jmddev)) {
		MD_BUG();
		return -EINVAL;
	}

	chunk_size = jmddev->sb->chunk_size;
	pnum = level_to_pers(jmddev->sb->level);

	jmddev->param.chunk_size = chunk_size;
	jmddev->param.personality = pnum;

	if (chunk_size > MAX_CHUNK_SIZE) {
		printk(TOO_BIG_CHUNKSIZE, chunk_size, MAX_CHUNK_SIZE);
		return -EINVAL;
	}
	/*
	 * chunk-size has to be a power of 2 and multiples of PAGE_SIZE
	 */
	if ( (1 << ffz(~chunk_size)) != chunk_size) {
		MD_BUG();
		return -EINVAL;
	}
	if (chunk_size < PAGE_SIZE) {
		printk(TOO_SMALL_CHUNKSIZE, chunk_size, PAGE_SIZE);
		return -EINVAL;
	}

	if (pnum >= MAX_PERSONALITY) {
		MD_BUG();
		return -EINVAL;
	}

	if ((pnum != RAID1) && (pnum != LINEAR) && !chunk_size) {
		printk(BAD_CHUNKSIZE);
		return -EINVAL;
	}

	if (!pers[pnum])
	{
#ifdef CONFIG_KMOD
		char module_name[80];
		sprintf (module_name, "md-personality-%d", pnum);
		request_module(module_name);
		if (!pers[pnum])
#endif
			return -EINVAL;
	}

	if (device_size_calculation(jmddev))
		return -EINVAL;

	jmd_hardsect_sizes[jmdidx(jmddev)] = 512;
	ITERATE_RDEV(jmddev,jrdev,tmp) {
		if (jrdev->faulty)
			continue;
	}
	jmd_blocksizes[jmdidx(jmddev)] = 1024;
	if (jmd_blocksizes[jmdidx(jmddev)] < jmd_hardsect_sizes[jmdidx(jmddev)])
		jmd_blocksizes[jmdidx(jmddev)] = jmd_hardsect_sizes[jmdidx(jmddev)];

	jmddev->pers = pers[pnum];
        
	printk("Per->run ...\n");
	err = jmddev->pers->run(jmddev);
	if (err) {
		printk("pers->run() failed ...\n");
		jmddev->pers = NULL;
		return -EINVAL;
	}

	jmddev->sb->state &= ~(1 << MD_SB_CLEAN);
	jmd_update_sb(jmddev);

	jmd_hd_struct[jmdidx(jmddev)].start_sect = 0;
	jmd_hd_struct[jmdidx(jmddev)].nr_sects = jmd_size[jmdidx(jmddev)] << 1;

	return (0);
}
#undef TOO_BIG_CHUNKSIZE
#undef BAD_CHUNKSIZE

int jmd_thread(void *arg)
{
	struct jmd_thread *thread = arg;
#ifdef JDEBUG
        printk("In md_thread ...\n");
#endif
	allow_signal(SIGKILL);
	while (!kthread_should_stop()) {
		if (signal_pending(current))
			flush_signals(current);

		wait_event_interruptible_timeout
			(thread->wqueue,
			 test_bit(THREAD_WAKEUP, &thread->flags)
			 || kthread_should_stop(),
			 thread->timeout);

		clear_bit(THREAD_WAKEUP, &thread->flags);
		if (!kthread_should_stop())
			thread->run(thread);
	}

	return 0;
}

void jmd_wakeup_thread(struct jmd_thread *thread)
{
	set_bit(THREAD_WAKEUP, &thread->flags);
	wake_up(&thread->wqueue);
}
EXPORT_SYMBOL_GPL(jmd_wakeup_thread);

void jmd_done_sync(struct jmddev *jmddev, int blocks, int ok)
{
	/* another "blocks" (512byte) blocks have been synced */
	atomic_sub(blocks, &jmddev->recovery_active);
	wake_up(&jmddev->recovery_wait);
	if (!ok) {
		set_bit(MD_RECOVERY_ERR, &jmddev->recovery);
		jmd_wakeup_thread(jmddev->thread);
	}
}
EXPORT_SYMBOL_GPL(jmd_done_sync);

struct jmd_thread *jmd_register_thread(void (*run) (struct jmd_thread *),
				struct jmddev *jmddev, const char *name)
{
	struct jmd_thread *thread;
        	
	thread = (struct jmd_thread *) kmalloc
				(sizeof(struct jmd_thread), GFP_KERNEL);
	if (!thread)
		return NULL;
	
	memset(thread, 0, sizeof(struct jmd_thread));
	init_waitqueue_head(&thread->wqueue);
	
	thread->run = run;
	thread->jmddev = jmddev;
	thread->name = name;
	thread->timeout = MAX_SCHEDULE_TIMEOUT;
        printk("MAX_SCHEDULE_TIMEOUT = %ld\n", MAX_SCHEDULE_TIMEOUT);
	thread->tsk = kthread_run(jmd_thread, thread, "%s", name);
	if (IS_ERR(thread->tsk)) {
		kfree(thread);
		return NULL;
	}
#ifdef JDEBUG
	printk("thread->tsk->pid = %d ...\n", thread->tsk->pid);
#endif
	return thread;
}
EXPORT_SYMBOL_GPL(jmd_register_thread);

void jmd_interrupt_thread(struct jmd_thread *thread)
{
	if (!thread->tsk) {
		MD_BUG();
	        printk("No thread, do md_bug ...\n");
		return;
	}
	printk("interrupting MD-thread pid %d\n", thread->tsk->pid);
        kthread_stop(thread->tsk);
        kfree(thread);
        thread = NULL;
}

void jmd_unregister_thread(struct jmd_thread *thread)
{
	thread->run = NULL;
	thread->name = NULL;
	if (!thread->tsk) {
		MD_BUG();
	        printk("No thread, do md_bug ...\n");
		return;
	}
	jmd_interrupt_thread(thread);
}
EXPORT_SYMBOL_GPL(jmd_unregister_thread);

void jmd_recover_arrays (void)
{
	if (!jmd_recovery_thread) {
		MD_BUG();
		return;
	}
	jmd_wakeup_thread(jmd_recovery_thread);
}
EXPORT_SYMBOL_GPL(jmd_recover_arrays);

int jmd_error(dev_t dev, dev_t jrdev)
{
	struct jmddev *jmddev;
	int rc;

	if (!jmddev) {
		MD_BUG();
		return 0;
	}
	if (jmddev->pers->stop_resync)
		jmddev->pers->stop_resync(jmddev);
	if (jmddev->recovery_running)
		jmd_interrupt_thread(jmd_recovery_thread);
	if (jmddev->pers->error_handler) {
		rc = jmddev->pers->error_handler(jmddev, jrdev);
		jmd_recover_arrays();
		return rc;
	}
	return 0;
}
EXPORT_SYMBOL_GPL(jmd_error);

int register_jmd_personality(int pnum, struct jmd_personality *p)
{
	if (pnum >= MAX_PERSONALITY)
		return -EINVAL;

	if (pers[pnum])
		return -EBUSY;

	pers[pnum] = p;
	printk(KERN_INFO "%s personality registered\n", p->name);
	return 0;
}
EXPORT_SYMBOL_GPL(register_jmd_personality);

int unregister_jmd_personality(int pnum)
{
	if (pnum >= MAX_PERSONALITY)
		return -EINVAL;

	printk(KERN_INFO "%s personality unregistered\n", pers[pnum]->name);
	pers[pnum] = NULL;
	return 0;
}
EXPORT_SYMBOL_GPL(unregister_jmd_personality);

DECLARE_WAIT_QUEUE_HEAD(resync_wait);

#define SYNC_MARKS	10
#define	SYNC_MARK_STEP	(3*HZ)
int jmd_do_sync(struct jmddev *jmddev, struct jmd_disk *spare)
{
	unsigned int max_blocks, currspeed,
		j, window, err, serialize;
	dev_t read_disk = jmddev_to_dev(jmddev);
	unsigned long mark[SYNC_MARKS];
	unsigned long mark_cnt[SYNC_MARKS];	
	int last_mark,m;
	struct list_head *tmp;
	unsigned long last_check;
	struct jmddev *jmddev2;


	err = down_interruptible(&jmddev->resync_sem);
	if (err)
	        goto out_nolock;
recheck:
	//serialize = 0;
        //ITERATE_MDDEV(jmddev2, tmp){
		//if (jmddev2 == jmddev)
		//	continue;
		//if (jmddev2->curr_resync && match_mddev_units(mddev,mddev2)) {
		//      //printk(KERN_INFO "md: serializing resync, md%d shares one or \
                        //                        more physical units with md%d!\n", 
                        //                        jmdidx(jmddev), jmdidx(jmddev2));
		      //serialize = 1;
		      //break;
		//}
	//}

	if (serialize) {
		//interruptible_sleep_on(&resync_wait);
		//if (md_signal_pending(current)) {
			//md_flush_signals();
			//err = -EINTR;
			//goto out;
		//}
		goto recheck;
	}

	jmddev->curr_resync = 1;

	max_blocks = jmddev->sb->size;

	printk(KERN_INFO "md: syncing RAID array md%d\n", jmdidx(jmddev));
	//printk(KERN_INFO "md: minimum _guaranteed_ reconstruction speed: %d KB/sec/disc.\n",
//						sysctl_speed_limit_min);
	//printk(KERN_INFO "md: using maximum available idle IO bandwith \
        //                but not more than %d KB/sec) for reconstruction.\n", 
        //                                        sysctl_speed_limit_max);

	/*
	 * Resync has low priority.
//	 */
	//current->nice = 19;
	//is_mddev_idle(mddev); /* this also initializes IO event counters */
	for (m = 0; m < SYNC_MARKS; m++) {
		mark[m] = jiffies;
		mark_cnt[m] = 0;
	}
	last_mark = 0;
	//mddev->resync_mark = mark[last_mark];
	//mddev->resync_mark_cnt = mark_cnt[last_mark];

	/*
	 * Tune reconstruction:
	 */
	//window = MAX_READAHEAD*(PAGE_SIZE/1024);
	printk(KERN_INFO "md: using %dk window, over a total of %d blocks.\n",
                                                        window,max_blocks);

	atomic_set(&jmddev->recovery_active, 0);
	init_waitqueue_head(&jmddev->recovery_wait);
	last_check = 0;
	for (j = 0; j < max_blocks;){
		int blocks;

		blocks = jmddev->pers->sync_request(jmddev, j);

		if (blocks < 0) {
			err = blocks;
			goto out;
		}
		atomic_add(blocks, &jmddev->recovery_active);
		j += blocks;
		jmddev->curr_resync = j;

		if (last_check + window > j)
			continue;
		
		//run_task_queue(&tq_disk); //??

		if (jiffies >= mark[last_mark] + SYNC_MARK_STEP ) {
			/* step marks */
			int next = (last_mark+1) % SYNC_MARKS;
			
			jmddev->resync_mark = mark[next];
			jmddev->resync_mark_cnt = mark_cnt[next];
			mark[next] = jiffies;
			mark_cnt[next] = j - atomic_read(&jmddev->recovery_active);
			last_mark = next;
		}
			

		//if (md_signal_pending(current)) {
			/*
			 * got a signal, exit.
			 */
			jmddev->curr_resync = 0;
			printk("md_do_sync() got signal ... exiting\n");
			//md_flush_signals();
			err = -EINTR;
			goto out;
		//}

		/*
		 * this loop exits only if either when we are slower than
		 * the 'hard' speed limit, or the system was IO-idle for
		 * a jiffy.
		 * the system might be non-idle CPU-wise, but we only care
		 * about not overloading the IO subsystem. (things like an
		 * e2fsck being done on the RAID array should execute fast)
		 */
repeat:
		//if (md_need_resched(current))
		//	schedule();

		currspeed = (jmddev->resync_mark_cnt)/((jiffies-jmddev->resync_mark)/HZ +1) +1;

		//if (currspeed > sysctl_speed_limit_min) {
			//current->nice = 19;

			//if ((currspeed > sysctl_speed_limit_max) ||
			//		!is_mddev_idle(mddev)) {
			//	current->state = TASK_INTERRUPTIBLE;
			//	md_schedule_timeout(HZ/4);
			//	if (!md_signal_pending(current))
			//		goto repeat;
			//}
		//} else
			//current->nice = -20;
	}
	//fsync_dev(read_disk);
	printk(KERN_INFO "md: md%d: sync done.\n",jmdidx(jmddev));
	err = 0;
	/*
	 * this also signals 'finished resyncing' to md_stop
	 */
out:
	wait_event(jmddev->recovery_wait, atomic_read(&jmddev->recovery_active)==0);
	up(&jmddev->resync_sem);
out_nolock:
	jmddev->curr_resync = 0;
	wake_up(&resync_wait);
	return err;
}
EXPORT_SYMBOL_GPL(jmd_do_sync);

void jmd_do_recovery(struct jmd_thread  *thread)
{
#ifdef JDEBUG
        printk("In md_do_recovery ...\n");
#endif
        return;
}

void jmddev_init(struct jmddev *jmddev)
{
        jmddev->tvalue = 1;
        jmddev->nb_dev = 0;
        jmddev->private = NULL;
	INIT_LIST_HEAD(&jmddev->disks);

        atomic_set(&jmddev->openers, 0);
        init_waitqueue_head(&jmddev->twait);

        return ;
}


static struct jmddev * jmddev_find(dev_t unit)
{
        struct jmddev *jmddev = NULL;
	jmddev = kzalloc(sizeof(*jmddev), GFP_KERNEL);
	if (!jmddev)
		return NULL;

        jmddev_init(jmddev);
        return jmddev;
}

static int jmd_open(struct block_device *bdev, fmode_t mode)
{
	struct jmddev *jmddev = g_jmddev; 
	int err;
        printk("In open ...\n");

	if (!jmddev)
		return -ENODEV;

	if (jmddev->gendisk != bdev->bd_disk) {
		/* we are racing with mddev_put which is discarding this
		 * bd_disk.
		 */
		return -ERESTARTSYS;
	}
	BUG_ON(jmddev != bdev->bd_disk->private_data);

	err = 0;
	atomic_inc(&jmddev->openers);
	return err;
}

static void jmd_release(struct gendisk *disk, fmode_t mode)
{
	struct jmddev *jmddev = g_jmddev;
        printk("In close ...\n");

	BUG_ON(!jmddev);
	atomic_dec(&jmddev->openers);
}

static void jmd_make_request(struct request_queue *q, struct bio *bio)
{
        const int rw = bio_data_dir(bio);
        struct jmddev *jmddev = q->queuedata;
        //blk_queue_split(q, &bio, q->queuedata);
        printk("\n... rw = %d bio ptr = %x bio->bi_size = %u \
                        bio->bi_iter.bi_sector = %lu ... \n", 
                        rw, bio, bio->bi_size, bio->bi_sector);

        if(jmddev == NULL || jmddev->pers == NULL){
                bio_endio(bio,0);
                return ;
        }
                
        jmddev->pers->make_request(jmddev, bio);
        return ;
}

static const struct block_device_operations jmd_fops =
{
	.owner		= THIS_MODULE,
	.open		= jmd_open,
	.release	= jmd_release,
};


static int jmd_alloc(dev_t dev, char *name)
{
        struct jmddev *jmddev = jmddev_find(dev);
        struct gendisk *disk;
        int error;
        g_jmddev = jmddev;

        error = -EEXIST;
        if(jmddev->gendisk)
                goto abort;

	error = -ENOMEM;
	jmddev->queue = blk_alloc_queue(GFP_KERNEL);
	if (!jmddev->queue)
		goto abort;
	jmddev->queue->queuedata = jmddev;

	blk_queue_make_request(jmddev->queue, jmd_make_request);
	blk_set_stacking_limits(&jmddev->queue->limits);

	disk = alloc_disk(1);
	if (!disk) {
                printk("[%s-%d]: fix here!\n", __func__, __LINE__);
                goto abort;
        }
	disk->major = JMD_MAJOR;
	disk->first_minor = 0;

        if(!name)
                goto abort;
	strcpy(disk->disk_name, name);

	disk->fops = &jmd_fops;
	disk->private_data = jmddev;
	disk->queue = jmddev->queue;
	blk_queue_flush(jmddev->queue, REQ_FLUSH | REQ_FUA);

	/* Allow extended partitions.  This makes the
	 * 'mdp' device redundant, but we can't really
	 * remove it now.
	 */
	disk->flags |= GENHD_FL_EXT_DEVT;
        jmddev->gendisk = disk;

	add_disk(disk);

	kobj_base = kobject_create_and_add("jmds", &disk_to_dev(disk)->kobj);
	if (!kobj_base) {
		error = -ENOMEM;
		goto err_base;
	}
	error = sysfs_create_files(kobj_base, jmds_attrs);
	if (error)
		goto err_jmds_files;


        return 0;
abort:
err_base:
err_jmds_files:
	kobject_del(kobj_base);
	kobject_put(kobj_base);
        return error;
}

static int __init jmd_init(void)
{
        int ret = -ENOMEM;
	static char * name = "jmd_recoveryd";

	printk (KERN_INFO "md driver %d.%d.%d MAX_MD_DEVS=%d, MD_SB_DISKS=%d\n",
			        MD_MAJOR_VERSION, MD_MINOR_VERSION,
			        MD_PATCHLEVEL_VERSION, MAX_MD_DEVS, MD_SB_DISKS);
        if((ret = register_blkdev(0, "jmd")) < 0)
                goto err_jmd;
        JMD_MAJOR = ret;
        printk("JMD_MAJOR = %u\n",JMD_MAJOR);

	jmd_recovery_thread = jmd_register_thread(jmd_do_recovery, NULL, name);
        if (!jmd_recovery_thread){
		printk(KERN_ALERT "bug: couldn't allocate jmd_recovery_thread\n");
                goto err_thread;
        }
	jmd_alloc(JMD_MAJOR, "jmd0");

        
        return 0;
err_thread:
        unregister_blkdev(JMD_MAJOR, "jmd");
err_jmd:
       return ret; 
}

static void __exit jmd_exit(void)
{
        struct jmddev *jmddev=g_jmddev;
        struct jmd_rdev *jrdev;
        struct list_head *tmp;
        ITERATE_RDEV(jmddev,jrdev,tmp){
                unlock_jrdev(jrdev);
                free_disk_sb(jrdev);
        }
	sysfs_remove_files(kobj_base, jmds_attrs);

        if(jmddev->gendisk){
                del_gendisk(jmddev->gendisk);
                put_disk(jmddev->gendisk);
        }
        if(jmddev->queue){
                blk_cleanup_queue(jmddev->queue);
        }
        unregister_blkdev(JMD_MAJOR, "jmd");
        if(jmd_recovery_thread)
                jmd_unregister_thread(jmd_recovery_thread);

        kfree(jmddev);
        return;
}

subsys_initcall(jmd_init);
module_exit(jmd_exit);
MODULE_LICENSE("GPL");
