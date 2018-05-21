/* *  jradm.c interface
*
*  Copyright(C)  2018 
*  Contact: JeCortex@yahoo.com
*
*  This program is distributed in the hope that it will be useful,
*  but WITHOUT ANY WARRANTY; without even the implied warranty of
*  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
*  GNU General Public License for more details.
*/

#include <stdio.h>
#include <ctype.h>
#include <getopt.h>
#include "jradm.h"

#define Version "1.0\n"

const char Name[] = "jradm";

char short_options[]="-ABCDEFGIQhVXYWZ:vqbc:i:l:p:m:n:x:u:c:d:z:U:N:sarfRSow1tye:k:";

struct option long_options[] = {
    {"create", 0, 0, 0},
    {"help-options",0,0, HelpOptions},
    {0, 0, 0, 0},
};

char Help[] =
"jradm is used for managing\n"
"Linux jmd devices (JRAID array)\n"
"Usage: jradm --create device options...\n"
"            Create a new array from unused devices.\n"
"\n"                                
;

char Help_create[] =
"Usage: jradm --create device --chunk=X --level=Y --jraid-devices=Z devices\n"
"This usage will initialise a new jmd array.\n"
"\n"                                
;

char OptionHelp[] =                                                             
"Any parameter that does not start with '-' is treated as a device name\n"      
"or, for a file name.\n"                                      
"The first such name is often the name of an jmd device.  Subsequent\n"          
"names are often names of component devices.\n"                                 
"\n"                                
;

enum mode {
	CREATE,
        MANAGE,
	mode_count
};

char *mode_help[mode_count] = {
	[0]		= Help,
	[1]		= Help_create,
};

int main(int argc, char *argv[])
{
        int opt, rv;
	int option_index;
	char *shortopt = short_options;
        int mode = 0;
        char *help_text;
        struct jmddev_ident ident;
        int jmdfd;
	struct supertype *ss = NULL;
	int devs_found = 0;

        int print_help = 0;
	unsigned long long data_offset = INVALID_SECTORS;

	struct jmddev_dev *devlist = NULL;
	struct jmddev_dev **devlistend = & devlist;
	struct jmddev_dev *dv;

	ident.uuid_set = 0;
	ident.level = UnSet;
	ident.raid_disks = UnSet;
	ident.super_minor = UnSet;
	ident.devices = 0;
	ident.spare_group = NULL;
	ident.st = NULL;
	ident.name[0] = 0;

	struct context c = {
		.require_homehost = 1,
	};

	struct shape s = {
		.level		= UnSet,
		.layout		= UnSet,
	};

	while ((option_index = -1),
	       (opt = getopt_long(argc, argv, shortopt, long_options,
				  &option_index)) != -1) {
                //printf("opt %u...\n", opt);
                switch(opt) { //133
                case HelpOptions:
                        print_help = 2;
                        printf("In HelpOptions ...\n");
                        continue;
		case 'h':
			print_help = 1;
			continue;
		case 'C':
                        mode = CREATE;
                        printf("In CREATE ...\n");
			continue;
		case 'V':
			fputs(Version, stderr);
                        return 0;
                }
/*
                switch(opt) {  //191
                }
                switch(opt) {  //327
                }
                switch(opt) {  //374
                        switch(opt) {  //538
                        }
                }
*/
        }

        if(print_help) {
                if(print_help == 2)
                        help_text = OptionHelp; 
                if(print_help == 1)
                        help_text = mode_help[mode];
                fputs(help_text, stdout);
                return 0;
        }
	//jmdfd = open_jmddev(devlist->devname, 1);
	//switch(mode) {
	//case MANAGE:
	//	rv = JManage_run(devlist->devname, jmdfd, &c);
        //        break;
	//case CREATE:
	//	rv = JCreate(ss, devlist->devname,
	//		    ident.name, ident.uuid_set ? ident.uuid : NULL,
        //                    devs_found-1, devlist->next,
	//		    &s, &c, data_offset);
        //        break;
        //}
	//if (jmdfd > 0)
	//	close(jmdfd);
        return 0;
} 

