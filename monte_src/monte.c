/*------------------------------------------------------------ -*- C -*-
 *
 *  Two-kernel Monte for MIPS
 *  Copyright (C) 2003 MuscleNerd <musclenerd2000@hotmail.com>
 *
 *  Ported from the i386 version of Monte:
 *  Copyright (C) 2000 Erik Arjan Hendriks <hendriks@scyld.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 *
 *  monte.c - User-level code that invokes the kernel-level module
 *  $Revision: 1.2 $
 *--------------------------------------------------------------------*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <syscall.h>

#define MONTE_MAGIC_1 0xdeaddead
#define MONTE_MAGIC_2 0xdeaddead
struct monte_param_t {
    char *command_line;
    int   kernel_fd;
};

void usage(char *arg0) {
    fprintf(stderr,
	    "Usage: %s [options] imagefile [command line ....]\n"
	    "       This program loads a MIPS kernel image into memory and then\n"
	    "       restarts the machine using the new kernel.\n"
	    "\n"
	    "       imagefile is the kernel image file to load.  It must\n"
	    "       start with a \"makeppceval\" type of header that describes\n"
	    "       where the kernel ends and the embedded initrd begins.\n"
	    "\n"
	    "       All remaining command line args are concatenated and passed to\n"
	    "       the kernel as the kernel command line.\n"
	    "\n"
	    "       Options:\n"
	    "       -h          Display this message and exit.\n"
	    "       -v          Display the program version number and exit.\n"
	    , arg0);
}

int main(int argc, char *argv[]) {
    int i, len, c;
    struct monte_param_t param;
    
    param.kernel_fd = -1;
    
    while((c = getopt(argc, argv, "vh"))!=EOF) {
	switch(c) {
	case 'h':
	    usage(argv[0]);
	    exit(0);
	case 'v':
	    printf("%s: version %s\n", argv[0], PACKAGE_VERSION);
	    exit(0);
	default:
	    usage(argv[0]);
	    exit(1);
	}
    }
    
    if (argc - optind == 0) {
	usage(argv[0]);
	exit(1);
    }
    
    /* Open the kernel image file */
    if (strcmp(argv[optind], "-") == 0) {
	param.kernel_fd = STDIN_FILENO;
    } else {
	param.kernel_fd = open(argv[optind], O_RDONLY);
	if (param.kernel_fd == -1) {
	    perror(argv[optind]);
	    exit(1);
	}
    }
    
    /* The rest of the arguments get assembled into the the kernel
     * command line */
    len = 1;
    for (i=optind+1; i < argc; i++)
	len += strlen(argv[i])+1;
    param.command_line = malloc (len);
    if (!param.command_line) {
	fprintf(stderr, "Out of memory.\n");
	exit(1);
    }
    param.command_line[0] = 0;
    for (i=optind+1; i < argc; i++) {
	strcat(param.command_line, argv[i]);
	if (i+1 < argc) strcat(param.command_line, " ");
    }
    
    syscall(__NR_reboot, MONTE_MAGIC_1, MONTE_MAGIC_2, 0, &param);
    /* only returns on failure */

    perror("monte");
    exit(1);
} 
