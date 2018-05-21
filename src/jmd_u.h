#ifndef __JMD_U_H__
#define __JMD_U_H__

struct jmd_array_info {
	/*
	 * Generic constant information
	 */
	int major_version;
	int minor_version;
	int patch_version;
	int ctime;
	int level;
	int size;
	int nr_disks;
	int raid_disks;
	int md_minor;
	int not_persistent;

	/*
	 * Generic state information
	 */
	int utime;		/*  0 Superblock update time		      */
	int state;		/*  1 State bits (clean, ...)		      */
	int active_disks;	/*  2 Number of currently active disks	      */
	int working_disks;	/*  3 Number of working disks		      */
	int failed_disks;	/*  4 Number of failed disks		      */
	int spare_disks;	/*  5 Number of spare disks		      */

	/*
	 * Personality information
	 */
	int layout;		/*  0 the array's physical layout	      */
	int chunk_size;	/*  1 chunk size in bytes		      */

};

struct jmd_disk_info {
	/*
	 * configuration/status of one particular disk
	 */
	int number;
	int major;
	int minor;
	int raid_disk;
	int state;

};

struct jmd_param
{
	int			personality;	/* 1,2,3,4 */
	int			chunk_size;	/* in bytes */
	int			max_fault;	/* unused for now */
};

#endif
