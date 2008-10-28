/*
 * osd_test.c - A user-mode program that calls into the osd ULD
 *
 * Copyright (C) 2008 Panasas Inc.  All rights reserved.
 *
 * Authors:
 *   Boaz Harrosh <bharrosh@panasas.com>
 *   Benny Halevy <bhalevy@panasas.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *  1. Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *  2. Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *  3. Neither the name of the Panasas company nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <open-osd/libosd.h>
#include "exofs.h"

#include <dirent.h>
#include <fcntl.h>
#include <getopt.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <unistd.h>

/* missing from old distro headers*/
#ifndef ENOIOCTLCMD
#  define ENOIOCTLCMD	515	/* No ioctl command */
#endif

static void usage(void)
{
	static char msg[] = {
	"usage: mkfs.exofs --pid=pid_no [options] /dev/osdX\n"
	"--pid=pid_no -p pid_no\n"
	"        pid_no is the partition number that will contain the new\n"
	"        exofs filesystem. Minimum is %d (0x%x). pid_no can\n"
	"        start with 0x to denote an hex number\n"
	"\n"
	"--format[=format_size_meg]\n"
	"        First format the OSD LUN (device) before preparing the\n"
	"        filesystem. format_size_meg * 2^20 is the size in bytes\n"
	"        to use with the OSD_FORMAT command (0x for hex). The size\n"
	"        is optional, if not specified, or is bigger then, all\n"
	"        available space will be used\n"
	"\n"
	"--not_destructive\n"
	"        Do NOT remove a partition if one already exists.\n"
	"        Hence refuse the mkfs and return\n"
	"        (Meaningless if used with --format\n"
	"\n"
	"/dev/osdX is the osd LUN (char-dev) to use for preparing the exofs\n"
	"filesystem on\n"
	"\n"
	"Description: An exofs filesystem sits inside an OSD partition.\n"
	"  /dev/osdX + pid_no is the partition to use. If the partition\n"
	"  exists and --not_distructive is not used the old partition is\n"
	"   removed and a new one is created in its place.\n"
	};

	printf(msg, EXOFS_MIN_PID, EXOFS_MIN_PID);
}

static int _mkfs(char *path, osd_id p_id, bool destructive,
		 u64 format_size_meg)
{
	struct osd_dev *od;
	int ret;

	ret = osd_open(path, &od);
	if (ret)
		return ret;

	ret = exofs_mkfs(od, p_id, destructive, format_size_meg);

	osd_close(od);

	if (ret) {
		/* exofs_mkfs is a kernel API it returns negative errors */
		ret = -ret;
		if (ret == EEXIST && !destructive)
			printf("pid 0x%llx already exist and --not_destructive"
			       " specified, will not remove an existing"
			       " partition\n", _LLU(p_id));
		else
			printf("exofs_mkfs --pid=0x%llx returned %d: %s\n",
				_LLU(p_id), ret, strerror(ret));
	}

	return ret;
}

int main(int argc, char *argv[])
{
	struct option opt[] = {
		{.name = "pid", .has_arg = 1, .flag = NULL, .val = 'p'} ,
		{.name = "not_destructive", .has_arg = 0, .flag = NULL,
								.val = 'n'} ,
		{.name = "format", .has_arg = 2, .flag = NULL, .val = 'f'} ,
		{.name = 0, .has_arg = 0, .flag = 0, .val = 0} ,
	};
	osd_id pid = 0;
	bool desctructive = true;
	u64 format_size_meg = 0;
	char op;

	while ((op = getopt_long(argc, argv, "p:f::n", opt, NULL)) != -1) {
		switch (op) {
		case 'p':
			pid = strtoll(optarg, NULL, 0);
			break;

		case 'f':
			format_size_meg = optarg ?
				_LLU(strtoll(optarg, NULL, 0)) :
				EXOFS_FORMAT_ALL;
			if (!format_size_meg) /* == 0 is accepted */
				format_size_meg = EXOFS_FORMAT_ALL;
			break;

		case 'n':
			desctructive = false;
			break;
		}
	}

	argc -= optind;
	argv += optind;

	if (argc <= 0) {
		usage();
		return 1;
	}

	if (pid < EXOFS_MIN_PID) {
		usage();
		return 1;
	}

	return _mkfs(argv[0], pid, desctructive, format_size_meg);
}
