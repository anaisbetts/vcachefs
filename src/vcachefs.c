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


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/uio.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <dirent.h>
#include <unistd.h>

#include <fuse.h>
#include <glib.h>

#include "stdafx.h"
#include "vcachefs.h"
#include "stats.h"
#include "queue.h"
#include "cachemgr.h"

/* Globals */
GIOChannel* stats_file = NULL;


/* 
 * Utility Routines 
 */

static struct vcachefs_mount* get_current_mountinfo(void)
{
	/* NOTE: This function *only* works inside a FUSE callout */
	struct fuse_context* ctx = fuse_get_context();
	return (ctx ? ctx->private_data : NULL);
}

static struct vcachefs_fdentry* fdentry_new(void)
{
	struct vcachefs_fdentry* ret = g_new0(struct vcachefs_fdentry, 1);
	ret->refcnt = 1;
	return ret;
}

static struct vcachefs_fdentry* fdentry_ref(struct vcachefs_fdentry* obj)
{
	g_atomic_int_inc(&obj->refcnt);
	return obj;
}

static void fdentry_unref(struct vcachefs_fdentry* obj)
{
	if(g_atomic_int_dec_and_test(&obj->refcnt)) {
		if(obj->source_fd > 0)
			close(obj->source_fd);
		if(obj->filecache_fd > 0)
			close(obj->filecache_fd);
		g_free(obj->relative_path);
		g_free(obj);
	}
}

static struct vcachefs_fdentry* fdentry_from_fd(uint fd)
{
	struct vcachefs_fdentry* ret = NULL;
	struct vcachefs_mount* mount_obj = get_current_mountinfo();

	g_static_rw_lock_reader_lock(&mount_obj->fd_table_rwlock);
	ret = g_hash_table_lookup(mount_obj->fd_table, &fd);
	g_static_rw_lock_reader_unlock(&mount_obj->fd_table_rwlock);

	return (ret ? fdentry_ref(ret) : NULL);
}

static char* build_cache_path(const char* source_path)
{
	const char* env = getenv("VCACHEFS_CACHEPATH");
	char* cache_root;
	if (env) {
		cache_root = g_strdup(env);
	} else {
		cache_root = g_build_filename(getenv("HOME"), ".vcachefs", NULL);
	}

	// Calculate the MD5 of the source path
	gchar* sum = NULL;
	gchar* ret = NULL;
	sum = g_compute_checksum_for_string(G_CHECKSUM_MD5, source_path, -1);
	ret = g_build_filename(cache_root, sum, NULL);
	g_free(sum);

	g_free(cache_root);
	return ret;
}


/*
 * File-based cache functions
 */

static int try_open_from_cache(const char* cache_root, const char* relative_path, int flags)
{
	gchar* path = g_build_filename(cache_root, relative_path, NULL);
	int ret = open(path, flags, 0);
	g_free(path);

	return ret;
}

static int copy_file_and_return_destfd(const char* source_root, const char* dest_root, const char* relative_path, gint* quitflag_atomic)
{
	gchar* src_path = g_build_filename(source_root, relative_path, NULL);
	gchar* dest_path = g_build_filename(dest_root, relative_path, NULL);
	int src_fd = 0, dest_fd = 0;

	g_debug("Copying '%s' to '%s'", src_path, dest_path);
	src_fd = open(src_path, O_RDONLY);
	dest_fd = open(dest_path, O_RDWR | O_CREAT | O_EXCL, S_IRWXU | S_IRGRP | S_IROTH);
	if (src_fd <= 0 || dest_fd <= 0)
		goto out;

	stats_write_record(stats_file, "copyfile", 0, 0, relative_path);

	/* We've got files, let's go to town */
	char buf[4096];
	int has_read, has_written;
	do {
		has_read = read(src_fd, buf, 4096);

		if (g_atomic_int_get(quitflag_atomic)) {
			has_read = -1;
			has_written = -1;
			break;
		}

		has_written = 0;
		if (has_read > 0)
			has_written = write(dest_fd, buf, has_read);
	} while (has_read > 0 && has_written == has_read );

	/* Something has gone wrong */
	if (has_written != has_read || has_read < 0) {
		unlink(dest_path);
		close(dest_fd);
		dest_fd = -1;
		goto out;
	}

	g_debug("Copy succeeded");
	lseek(dest_fd, 0, SEEK_SET);

out:
	g_debug("Exiting, dest_fd = %d", dest_fd);
	if (src_fd > 0)
		close(src_fd);
	g_free(src_path);
	g_free(dest_path);

	return dest_fd;
}

/* Stupid struct to pass a tuple through to this fn */
struct cache_entry {
	int fd;
	char* relative_path;
};

static void add_cache_fd_to_item(gpointer key, gpointer value, gpointer cache_entry)
{
	/* NOTE: Since we've grabbed the fd table lock before this function, we don't need
	 * to grab a reference to the fd entry */
	struct vcachefs_fdentry* fde = value;
	struct cache_entry* ce = cache_entry;
	if (strcmp(fde->relative_path, ce->relative_path))
		return;

	int fd = dup(ce->fd);
	lseek(fd, 0, SEEK_SET);

	/* Maybe some thread beat us to it? */
	if (fde->source_fd > 0) {
		int tmp = fde->source_fd;
		fde->source_fd = 0;
		close(tmp);
	}

	fde->source_offset = fd;
	fde->source_fd = fd;
}

static gpointer file_cache_copy_thread(gpointer data)
{
	struct vcachefs_mount* mount_obj = data;

	g_debug("Starting cache copy thread...");
	while(g_atomic_int_get(&mount_obj->quitflag_atomic) == 0) {
		int err, destfd;
		struct stat st;
		struct cache_entry ce;
		GTimeVal five_secs_from_now;
		char* relative_path;

		g_get_current_time(&five_secs_from_now);
		g_time_val_add(&five_secs_from_now, 5 * 1000 * 1000);
		relative_path = g_async_queue_timed_pop(mount_obj->file_copy_queue, &five_secs_from_now);

		/* We didn't have anything to do - let's clean up the cache */
		if (!relative_path) {
			cache_manager_reclaim_space(mount_obj->cache_manager, mount_obj->max_cache_size);
			continue;
		}

		/* Create the parent directory if we have to */
		char* dirname = g_path_get_dirname(relative_path);
		char* parent_path = g_build_filename(mount_obj->cache_path, dirname, NULL);
		g_debug("Starting copy, picked up '%s'", relative_path);
		err = lstat(parent_path, &st);
		if (err == -1 && errno == ENOENT) {
			g_debug("Creating '%s'", parent_path);
			if (g_mkdir_with_parents(parent_path, 5+7*8+7*8*8) != 0)
				goto done;
		} 
		if(err < 0) 	/* Couldn't create dir */
			goto done;
		
		destfd = copy_file_and_return_destfd(mount_obj->source_path, mount_obj->cache_path, 
				relative_path, &mount_obj->quitflag_atomic);

		if (destfd < 0)
			goto done;

		ce.fd = destfd; 	ce.relative_path = relative_path;

		/* Grab the file table lock, and set the source file handle for every file */
		g_static_rw_lock_writer_lock(&mount_obj->fd_table_rwlock);
		g_hash_table_foreach(mount_obj->fd_table, add_cache_fd_to_item, &ce);
		g_static_rw_lock_writer_unlock(&mount_obj->fd_table_rwlock);

		/* Notify the cache manager */
		char* dest_path = g_build_filename(mount_obj->cache_path, relative_path, NULL);
		cache_manager_notify_added(mount_obj->cache_manager, dest_path);
		g_free(dest_path);

		/* Let go of our original fd */
		close(destfd);

done:
		g_free(dirname);
		g_free(parent_path);
		g_free(relative_path);
	}
	g_debug("Ending cache copy thread...");

	return NULL;
}

gboolean can_delete_cached_file(const char* path, gpointer context)
{
	/* Blowing away files who we have an open handle to is probably bad */
	struct vcachefs_mount* mount_obj = context;
	gboolean ret;

	g_static_rw_lock_reader_lock(&mount_obj->fd_table_rwlock);
	/* FIXME: Relative path? */
	ret = (g_hash_table_lookup(mount_obj->fd_table_byname, path) != NULL);
	g_static_rw_lock_reader_unlock(&mount_obj->fd_table_rwlock);

	return ret;
}

static void insert_fdtable_entry(struct vcachefs_mount* mount_obj, struct vcachefs_fdentry* fde)
{
	g_hash_table_insert(mount_obj->fd_table, &fde->fd, fde);

	GSList* iter;
	if ( (iter = g_hash_table_lookup(mount_obj->fd_table_byname, fde->relative_path)) ) {
		iter = g_slist_alloc();
		iter->data = fde;
		g_hash_table_insert(mount_obj->fd_table_byname, fde->relative_path, fde);
	} else {
		iter = g_slist_prepend(iter, fde);
		g_hash_table_replace(mount_obj->fd_table_byname, fde->relative_path, fde);
	}
}

static gpointer force_terminate_on_ioblock(gpointer dontcare)
{
	sleep(15);
	kill(0, SIGKILL);
	return 0;
}

static void trash_fdtable_byname_item(gpointer key, gpointer val, gpointer dontcare) 
{ 
	GSList* fde_list = val;
	if (fde_list)
		g_slist_free(fde_list);
}

static void trash_fdtable_item(gpointer key, gpointer val, gpointer dontcare) 
{ 
	fdentry_unref((struct vcachefs_fdentry*)val);
}



/*
 * FUSE callouts
 */

static void* vcachefs_init(struct fuse_conn_info *conn)
{
	struct vcachefs_mount* mount_object = g_new0(struct vcachefs_mount, 1);
	mount_object->source_path = g_strdup(getenv("VCACHEFS_TARGET"));
	mount_object->cache_path = build_cache_path(mount_object->source_path);

	/* TODO: This is actually a config param */
	mount_object->max_cache_size = 20 * 1024 * 1024;

	if (getenv("VCACHEFS_PASSTHROUGH"))
		mount_object->pass_through = 1;

	g_thread_init(NULL);

	stats_file = stats_open_logging();

	/* Create the file descriptor tables */
	mount_object->fd_table = g_hash_table_new(g_int_hash, g_int_equal);
	mount_object->fd_table_byname = g_hash_table_new(g_str_hash, g_str_equal);
	g_static_rw_lock_init(&mount_object->fd_table_rwlock);
	mount_object->next_fd = 4;

	mount_object->cache_manager = cache_manager_new(mount_object->cache_path, can_delete_cached_file, mount_object);
	mount_object->work_queue = workitem_queue_new();

	/* Set up the file cache thread */
	mount_object->file_copy_queue = g_async_queue_new();
	mount_object->file_copy_thread = g_thread_create(file_cache_copy_thread, mount_object, TRUE/*joinable*/, NULL);

	stats_write_record(stats_file, "init_target", 0, 0, mount_object->cache_path);

	return mount_object;
}

static void vcachefs_destroy(void *mount_object_ptr)
{
	struct vcachefs_mount* mount_object = mount_object_ptr;

	/* Kick off a watchdog thread; this is our last chance to bail; while
	 * Mac and Linux both support async IO for reads/writes, if a remote FS
	 * wanders off on an access or readdir call, there's absolutely zilch
	 * that we can do about it, except for force kill everyone involved. */
	g_thread_create(force_terminate_on_ioblock, NULL, FALSE, NULL);

	/* Signal the file cache thread to terminate and wait for it
	 * XXX: if the thread hangs, we're boned - no way to timeout this */
	g_atomic_int_set(&mount_object->quitflag_atomic, 1);
	g_thread_join(mount_object->file_copy_thread);

	/* Free the async queue */
	char* item;
	g_async_queue_lock(mount_object->file_copy_queue);
	while ( (item = g_async_queue_try_pop_unlocked(mount_object->file_copy_queue)) ) {
		if (item)
			g_free(item);
	}
	g_async_queue_unlock(mount_object->file_copy_queue);
	g_async_queue_unref(mount_object->file_copy_queue);

	workitem_queue_free(mount_object->work_queue);
	cache_manager_free(mount_object->cache_manager);

	/* XXX: We need to make sure no one is using this before we trash it */
	g_hash_table_foreach(mount_object->fd_table, trash_fdtable_item, NULL);
	g_hash_table_foreach(mount_object->fd_table_byname, trash_fdtable_byname_item, NULL);
	g_hash_table_destroy(mount_object->fd_table);
	g_hash_table_destroy(mount_object->fd_table_byname);
	mount_object->fd_table = NULL;
	mount_object->fd_table_byname = NULL;
	g_free(mount_object->cache_path);
	g_free(mount_object->source_path);
	g_free(mount_object);

	stats_close_logging(stats_file);

	g_debug("Finished cleanup");
}

static int is_quitting(struct vcachefs_mount* mount_obj)
{
	return (g_atomic_int_get(&mount_obj->quitflag_atomic) != 0);
}

/* File ops callouts */

static int vcachefs_getattr(const char *path, struct stat *stbuf)
{
	int ret = 0; 
	struct vcachefs_mount* mount_obj = get_current_mountinfo();

	if(path == NULL || strlen(path) == 0) {
		return -ENOENT;
	}

	/* On shutdown, fail new requests */
	if(is_quitting(mount_obj))
		return -EIO;

	stats_write_record(stats_file, "getattr", 0, 0, path);
	if(strcmp(path, "/") == 0) {
		return (stat(mount_obj->source_path, stbuf) ? -errno : 0);
	}

	gchar* full_path = g_build_filename(mount_obj->source_path, &path[1], NULL);
	ret = stat((char *)full_path, stbuf);
	g_free(full_path);

	return (ret ? -errno : 0);
}

static int vcachefs_open(const char *path, struct fuse_file_info *fi)
{
	struct vcachefs_mount* mount_obj = get_current_mountinfo();
	struct vcachefs_fdentry* fde = NULL;

	if(path == NULL || strlen(path) == 0) {
		return -ENOENT;
	}

	/* On shutdown, fail new requests */
	if(is_quitting(mount_obj))
		return -EIO;

	gchar* full_path = g_build_filename(mount_obj->source_path, &path[1], NULL);

	int source_fd = open(full_path, fi->flags, 0);
	if(source_fd <= 0) 
		return -errno;

	/* Open succeeded - time to create a fdentry */
	fde = fdentry_new();
	fde->relative_path = g_strdup(path);
	fde->source_fd = source_fd;
	fde->source_offset = 0;

	g_static_rw_lock_writer_lock(&mount_obj->fd_table_rwlock);
	fi->fh = fde->fd = mount_obj->next_fd;
	mount_obj->next_fd++;
	insert_fdtable_entry(mount_obj, fde);
	g_static_rw_lock_writer_unlock(&mount_obj->fd_table_rwlock);

	if (mount_obj->pass_through)
		goto out;

	/* Try to open the file cached version; if it's not there, add it to the fetch list */
	if( (fde->filecache_fd = try_open_from_cache(mount_obj->cache_path, path, fi->flags)) == -1 && errno == ENOENT) {
		g_async_queue_push(mount_obj->file_copy_queue, g_strdup(path));
	}

	/* Touch the file so it doesn't get reclaimed by the cache manager */
	if (fde->filecache_fd) {
		gchar* full_cache_path = g_build_filename(mount_obj->cache_path, path, NULL);
		cache_manager_touch_file(mount_obj->cache_manager, full_cache_path);
		g_free(full_cache_path);
	}

out:
	
	/* FUSE handles this differently */
	stats_write_record(stats_file, "open", 0, 0, path);
	return 0;
}

static int read_from_fd(int fd, off_t* cur_offset, char* buf, size_t size, off_t offset)
{
	int ret = 0;

	if(*cur_offset != offset) {
		int tmp = lseek(fd, offset, SEEK_SET);
		if (tmp < 0) {
			ret = tmp;
			goto out;
		}
	}

	ret = read(fd, buf, size);
	if (ret >= 0)
		*cur_offset = offset + ret;
out:
	return ret;
}

static int vcachefs_read(const char *path, char *buf, size_t size, off_t offset,
		struct fuse_file_info *fi)
{
	int ret = 0;
	struct vcachefs_mount* mount_obj = get_current_mountinfo();
	struct vcachefs_fdentry* fde = fdentry_from_fd(fi->fh);
	if(!fde)
		return -ENOENT;

	/* On shutdown, fail new requests */
	if(is_quitting(mount_obj))
		return -EIO;

	/* Let's see if we can do this read from the file cache */
	if (!mount_obj->pass_through &&
	    (ret = read_from_fd(fde->filecache_fd, &fde->filecache_offset, buf, size, offset)) >= 0) {
		stats_write_record(stats_file, "cached_read", size, offset, path);
		goto out;
	}

	stats_write_record(stats_file, "uncached_read", size, offset, path);
	ret = read_from_fd(fde->source_fd, &fde->source_offset, buf, size, offset);

out:
	fdentry_unref(fde);
	return (ret < 0 ? -errno : ret);
}

static int vcachefs_statfs(const char *path, struct statvfs *stat)
{
	struct vcachefs_mount* mount_obj = get_current_mountinfo();

	/* On shutdown, fail new requests */
	if(is_quitting(mount_obj))
		return -EIO;

	return statvfs(mount_obj->source_path, stat);
}

static int vcachefs_release(const char *path, struct fuse_file_info *info)
{
	struct vcachefs_mount* mount_obj = get_current_mountinfo();
	struct vcachefs_fdentry* fde = NULL;

	/* On shutdown, fail new requests */
	if(is_quitting(mount_obj))
		return -EIO;

	/* Remove the entry from the fd table */
	g_static_rw_lock_writer_lock(&mount_obj->fd_table_rwlock);
	fde = g_hash_table_lookup(mount_obj->fd_table, &info->fh);
	if (fde) {
		g_hash_table_remove(mount_obj->fd_table, &info->fh);
		GSList* iter = g_hash_table_lookup(mount_obj->fd_table_byname, fde->relative_path);
		if (iter) {
			iter = g_slist_remove(iter, fde);
		}

		if (!iter) {
			g_hash_table_remove(mount_obj->fd_table_byname, fde->relative_path);
		} else {
			g_hash_table_replace(mount_obj->fd_table_byname, fde->relative_path, iter);
		}
	}

	g_static_rw_lock_writer_unlock(&mount_obj->fd_table_rwlock);

	if(!fde)
		return -ENOENT;

	fdentry_unref(fde);

	return 0;
}

static int vcachefs_access(const char *path, int amode)
{
	int ret = 0; 
	struct vcachefs_mount* mount_obj = get_current_mountinfo();

	if(path == NULL || strlen(path) == 0)
		return -ENOENT;

	/* On shutdown, fail new requests */
	if(is_quitting(mount_obj))
		return -EIO;

	if(!mount_obj->pass_through && strcmp(path, "/") == 0) {
		stats_write_record(stats_file, "cached_access", amode, 0, path);
		return access(mount_obj->source_path, amode);
	}

	stats_write_record(stats_file, "uncached_access", amode, 0, path);
	gchar* full_path = g_build_filename(mount_obj->source_path, &path[1], NULL);
	ret = access((char *)full_path, amode);
	g_free(full_path);

	return (ret < 0 ? errno : ret);

}

static int vcachefs_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
		off_t offset, struct fuse_file_info *fi)
{
	int ret;
	gchar* full_path = NULL;
 	const gchar* next_path_try;
	struct vcachefs_mount* mount_obj = get_current_mountinfo();
	DIR* dir = NULL;
	struct dirent* dentry;
	char path_buf[512];

	if(path == NULL || strlen(path) == 0)
		return -ENOENT;

	/* On shutdown, fail new requests */
	if(is_quitting(mount_obj))
		return -EIO;

	/* Try the source path first; if it's gone, retry with the cache */
	next_path_try = mount_obj->source_path;
	while (next_path_try) {
		if(strcmp(path, "/") == 0) {
			full_path = g_strdup(next_path_try);
		} else {
			full_path = g_build_filename(next_path_try, &path[1], NULL);
		}
		
		/* Open the directory and read through it */
		if ((dir = opendir(full_path)) != NULL) 
			break;

		next_path_try = (next_path_try == mount_obj->source_path && !mount_obj->pass_through ?
				 mount_obj->cache_path : NULL);
		g_free(full_path);
	}

	/* No dice - bail */
	if (dir == NULL) {
		opendir(full_path);
		g_free(full_path);
		return -errno;
	}

	stats_write_record(stats_file, "readdir", offset, 0, path);

	/* mkdir -p the cache directory */
	if(strcmp(path, "/") != 0 && !mount_obj->pass_through) {
		char* cache_path = g_build_filename(mount_obj->cache_path, &path[1], NULL);
		g_mkdir_with_parents(cache_path, 5+7*8+7*8*8);
		g_free(cache_path);
	}

	while((dentry = readdir(dir))) {
		struct stat stbuf;
		int stat_ret;

		/* Stat the file */
		snprintf(path_buf, 512, "%s/%s", full_path, dentry->d_name);
		stat_ret = stat(path_buf, &stbuf);

		if ((ret = filler(buf, dentry->d_name, (stat_ret>=0 ? &stbuf : NULL), 0)))
			goto out;
	}

out:
	closedir(dir);
	return 0;
}


/*
 * Main
 */

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
	/* Check for our environment variables 
	 * FIXME: There's got to be a less dumb way to do this */
	if (!getenv("VCACHEFS_TARGET")) {
		printf(" *** Please set the VCACHEFS_TARGET environment variable to the path that "
		       "should be mirrored! ***\n");
		return -1;
	}

	/* Initialize libevent */
	//ev_base = event_init();
	
	return fuse_main(argc, argv, &vcachefs_oper, NULL);
}
