/*
 * vcachefs.c - Userspace video caching filesystem 
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

#include <fuse.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <glib.h>

#include "vcachefs.h"

static void* vcachefs_init(struct fuse_conn_info *conn)
{
	// TODO: Implement me
}

static void vcachefs_destroy(void *mount_object)
{
	// TODO: Implement me
}

static int vcachefs_getattr(const char *path, struct stat *stbuf)
{
	int res = 0;

	memset(stbuf, 0, sizeof(struct stat));
	if(strcmp(path, "/") == 0) {
		stbuf->st_mode = S_IFDIR | 0755;
		stbuf->st_nlink = 2;
	}
	else if(strcmp(path, vcachefs_path) == 0) {
		stbuf->st_mode = S_IFREG | 0444;
		stbuf->st_nlink = 1;
		stbuf->st_size = strlen(vcachefs_str);
	}
	else
		res = -ENOENT;

	return res;
}

static int vcachefs_open(const char *path, struct fuse_file_info *fi)
{
	if(strcmp(path, vcachefs_path) != 0)
		return -ENOENT;

	if((fi->flags & 3) != O_RDONLY)
		return -EACCES;

	return 0;
}

static int vcachefs_read(const char *path, char *buf, size_t size, off_t offset,
		struct fuse_file_info *fi)
{
	size_t len;
	(void) fi;
	if(strcmp(path, vcachefs_path) != 0)
		return -ENOENT;

	len = strlen(vcachefs_str);
	if (offset < len) {
		if (offset + size > len)
			size = len - offset;
		memcpy(buf, vcachefs_str + offset, size);
	} else
		size = 0;

	return size;
}


static int vcachefs_statfs(const char *path, struct statvfs *stat)
{
	// TODO: Implement me
}


static int vcachefs_release(const char *path, struct fuse_file_info *info)
{
	// TODO: Implement me
}


static int vcachefs_access(const char *path, int amode)
{
	// TODO: Implement me
}

static int vcachefs_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
		off_t offset, struct fuse_file_info *fi)
{
	(void) offset;
	(void) fi;

	if(strcmp(path, "/") != 0)
		return -ENOENT;

	filler(buf, ".", NULL, 0);
	filler(buf, "..", NULL, 0);
	filler(buf, vcachefs_path + 1, NULL, 0);

	return 0;
}


static struct fuse_operations vcachefs_oper = {
	.getattr	= vcachefs_getattr,
	/*.readlink 	= vcachefs_readlink, */
	.open 		= vcachefs_open,
	.read		= vcachefs_read,
	.statfs 	= vcachefs_statfs,
	/* TODO: do we need flush? */
	.release 	= vcachefs_release,
	.init 		= vcachefs_init,
	.destroy 	= vcachefs_destroy,
	.access 	= vcachefs_access,

	/* TODO: implement these later
	.getxattr 	= vcachefs_getxattr,
	.listxattr 	= vcachefs_listxattr,
	.opendir 	= vcachefs_opendir, */
	.readdir	= vcachefs_readdir,
	/*.releasedir 	= vcachefs_releasedir,
	.fsyncdir 	= vcachefs_fsyncdir, */
};

int main(int argc, char *argv[])
{
	return fuse_main(argc, argv, &vcachefs_oper, NULL);
}
