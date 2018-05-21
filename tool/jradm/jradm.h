/* *  jradm.h interface
*
*  Copyright(C)  2018 
*  Contact: JeCortex@yahoo.com
*
*  This program is distributed in the hope that it will be useful,
*  but WITHOUT ANY WARRANTY; without even the implied warranty of
*  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
*  GNU General Public License for more details.
*/

#ifndef __JADM_H__
#define __JADM_H__

#define INVALID_SECTORS 1
extern const char Name[];

#ifdef DEBUG
#define pr_err(fmt, args...) fprintf(stderr, "%s: %s: "fmt, Name, __func__, ##args)
#else
#define pr_err(fmt, args...) fprintf(stderr, "%s: "fmt, Name, ##args)
#endif

 enum special_options {                                                          
        AssumeClean = 300,                                                      
        BitmapChunk,                                                            
	WriteBehind,
	ReAdd,
	NoDegraded,
	Sparc22,
	BackupFile,
	HomeHost,
	AutoHomeHost,
	Symlinks,
	AutoDetect,
	Waitclean,
	DetailPlatform,
	KillSubarray,
	UpdateSubarray,
	IncrementalPath,
	NoSharing,
        HelpOptions = 63,
	Brief,
 };

/* structures read from config file */
/* List of mddevice names and identifiers
 * Identifiers can be:
 *    uuid=128-hex-uuid
 *    super-minor=decimal-minor-number-from-superblock
 *    devices=comma,separated,list,of,device,names,with,wildcards
 *
 * If multiple fields are present, the intersection of all matching
 * devices is considered
 */

enum flag_mode {
	FlagDefault, FlagSet, FlagClear,
};

#define UnSet (0xfffe)
struct jmddev_ident {
	char	*devname;

	int	uuid_set;
	int	uuid[4];
	char	name[33];

	int super_minor;

	char	*devices;	/* comma separated list of device
				 * names with wild cards
				 */
	int	level;
	int raid_disks;
	int spare_disks;
	struct supertype *st;
	char	*spare_group;

	struct jmddev_ident *next;
};

struct context {
	int	readonly;
	int	runstop;
	int	verbose;
	int	brief;
	int	force;
	char	*homehost;
	int	require_homehost;
	char	*prefer;
	int	export;
	int	test;
	char	*subarray;
	char	*update;
	int	scan;
	int	SparcAdjust;
	int	autof;
	int	delay;
	int	freeze_reshape;
	char	*backup_file;
	int	invalid_backup;
	char	*action;
	int	nodes;
	char	*homecluster;
};

struct shape {
	int	raiddisks;
	int	sparedisks;
	int	journaldisks;
	int	level;
	int	layout;
	char	*layout_str;
	int	chunk;
	int	bitmap_chunk;
	char	*bitmap_file;
	int	assume_clean;
	int	write_behind;
	unsigned long long size;
	int	consistency_policy;
};


/* List of device names - wildcards expanded */
struct jmddev_dev {
	char *devname;
	int disposition;	/* 'a' for add, 'r' for remove, 'f' for fail,
				 * 'A' for re_add.
				 * Not set for names read from .config
				 */
	enum flag_mode writemostly;
	enum flag_mode failfast;
	int used;		/* set when used */
	long long data_offset;
	struct jmddev_dev *next;
};

struct supertype {
	struct superswitch *ss;
	int minor_version;
	int max_devs;
	char container_devnm[32];    /* devnm of container */
	void *sb;
	void *info;
	void *other; /* Hack used to convert v0.90 to v1.0 */
	unsigned long long devsize;
	unsigned long long data_offset; /* used by v1.x only */
	int ignore_hw_compat; /* used to inform metadata handlers that it should ignore
				 HW/firmware related incompatability to load metadata.
				 Used when examining metadata to display content of disk
				 when user has no hw/firmare compatible system.
			      */
	struct metadata_update *updates;
	struct metadata_update **update_tail;

	/* extra stuff used by mdmon */
	struct active_array *arrays;
	int sock; /* listen to external programs */
	char devnm[32]; /* e.g. md0.  This appears in metadata_version:
			 *  external:/md0/12
			 */
	int devcnt;
	int retry_soon;
	int nodes;
	char *cluster_name;

	struct mdinfo *devs;

};

extern int open_jmddev(char *dev, int report_errors);
extern int JCreate(struct supertype *st, char *jmddev,
	   char *name, int *uuid,
	   int subdevs, struct jmddev_dev *devlist,
	   struct shape *s,
	   struct context *c, unsigned long long data_offset);
extern int JManage_run(char *devname, int fd, struct context *c);
#endif
