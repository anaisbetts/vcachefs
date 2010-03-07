/*
 * stats.c - I/O statistics
 *
 * Copyright 2008 Paul Betts <paul.betts@gmail.com>
 *
 *
 * License:
 *
 * This package is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This package is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this package; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/uio.h>
#include <sys/statvfs.h>
#include <dirent.h>
#include <unistd.h>

#include <fuse.h>
#include <glib.h>

#include "stats.h"
#include "config.h"

GIOChannel* stats_open_logging(void)
{
	char* path = getenv("VCACHEFS_STATS_FILE");
	if (!path)
		return NULL;

	GIOChannel* ret = g_io_channel_new_file(path, "w", NULL);

	if (ret) {
		gsize dontcare;
		g_io_channel_write_chars(ret, "Timecode,Operation,Offset,Size,Info,Pid\n", -1, &dontcare, NULL);
	}

	return ret;
}

void stats_close_logging(GIOChannel* channel)
{
	if (!channel)
		return;

	g_io_channel_shutdown(channel, TRUE, NULL);
}

int stats_write_record(GIOChannel* channel, const char* operation, off_t offset, size_t size, const char* info)
{
	if (!channel)
		return FALSE;

	const char* safe_info = (info ? info : "");

	struct fuse_context* ctx = fuse_get_context(); 

	gchar* buf;
	if (sizeof(off_t) == 8) {
	 	buf = g_strdup_printf("%llu,\"%s\",%llu,%lu,\"%s\",%u\n", 
			get_time_code(), operation, (unsigned long long)offset, size, safe_info, ctx->pid);
	} else {
	 	buf = g_strdup_printf("%llu,\"%s\",%lu,%lu,\"%s\",%u\n", 
			get_time_code(), operation, (unsigned long)offset, size, safe_info, ctx->pid);
	}

	gsize dontcare;
	g_io_channel_write_chars(channel, buf, -1, &dontcare, NULL);
	g_free(buf);
	return TRUE;
}

long long unsigned int get_time_code(void)
{
	/* TODO: This function's resolution blows, but getting something better requires
	 * us to jump into platform-specific nonsense */
	struct timeval t;
	gettimeofday(&t, NULL);
	unsigned long long ret = t.tv_sec * 1000 * 1000 + t.tv_usec;
	return ret;
}
