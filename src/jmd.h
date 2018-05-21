#ifndef __JMD_H__
#define __JMD_H__
/*
 * Different major versions are not compatible.
 * Different minor versions are only downward compatible.
 * Different patchlevel versions are downward and upward compatible.
 */
#include "jmd_p.h"
#include "jmd_u.h"
#include "jmd_k.h"

#define MD_MAJOR_VERSION                0
#define MD_MINOR_VERSION                90
#define MD_PATCHLEVEL_VERSION           0

#define MAX_MD_DEVS 256
extern struct jmddev *g_jmddev; 

extern char *partition_name(dev_t dev);
extern int jmd_error(dev_t dev, dev_t jrdev);

extern void jmd_interrupt_thread(struct jmd_thread *thread);
extern void jmd_unregister_thread(struct jmd_thread  *thread);
extern struct jmd_thread* jmd_register_thread (void (*run) (struct jmd_thread *thread),
				struct jmddev *jmddev, const char *name);
extern int jmd_update_sb (struct jmddev *jmddev);
extern void jmd_wakeup_thread(struct jmd_thread *thread);

extern int register_jmd_personality(int pnum, struct jmd_personality *p);
extern int unregister_jmd_personality(int pnum);
extern int jmd_do_sync(struct jmddev *jmddev, struct jmd_disk *spare);
extern void jmd_recover_arrays (void);

struct jmd_sysfs_entry {
        struct attribute attr;
        ssize_t (*show)(struct jmddev *, char *);
        ssize_t (*store)(struct jmddev *, const char *, size_t);
};
#endif
