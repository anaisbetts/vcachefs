/*
 * cachemgr.c - Cache size manager
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

#include "stdafx.h"
#include "stats.h"
#include "cachemgr.h"

#define CACHEITEM_TAG 'tIaC'

struct CacheManager {
	char* cache_root;

	CMCanDeleteCallback can_delete_callback;
	gpointer user_context;

	GSList* cached_file_list;
	GStaticRWLock cached_file_list_rwlock;
};

/* FIXME: This code is porta-tarded */
struct CacheItemHeader {
	unsigned long tag;
	size_t struct_size;
	time_t mtime;
	guint64 filesize;
};

struct CacheItem {
	struct CacheItemHeader h;
	char* path;
};

static struct CacheItem* cacheitem_new(const char* full_path)
{
	/* We will only return a new item if this is a valid path, and not 
	 * something other than a file */
	struct stat st;
	if(lstat(full_path, &st) != 0 || !S_ISREG(st.st_mode))
		return NULL;

	struct CacheItem* ret = g_new0(struct CacheItem, 1);
	ret->path = g_strdup(full_path);
	ret->h.tag = CACHEITEM_TAG;
	ret->h.mtime = st.st_mtime;
	ret->h.filesize = st.st_size;
	ret->h.struct_size = sizeof(struct CacheItemHeader) + ((strlen(full_path) + 1) * sizeof(char));
	return ret;
}

static struct CacheItem* cacheitem_load(int fd)
{
	char* buf = NULL;
	struct CacheItem* ret = g_new0(struct CacheItem, 1);

	if(read(fd, ret, sizeof(struct CacheItemHeader)) != sizeof(struct CacheItemHeader))
		goto failed;

	if(ret->h.tag != CACHEITEM_TAG)
		goto failed;

	if(ret->h.struct_size <= sizeof(struct CacheItemHeader))
		goto failed;

	size_t to_read = ret->h.struct_size - sizeof(struct CacheItemHeader);
	buf = g_new0(char, to_read);
	if(read(fd, buf, to_read) != to_read)
		goto failed;

	ret->path = buf;
	return ret;

failed:
	if(buf != NULL)
		g_free(buf);
	if(ret != NULL)
		g_free(ret);
	return NULL;
}

static int cacheitem_save(int fd, struct CacheItem* obj)
{
	if(write(fd, &obj->h, sizeof(struct CacheItemHeader)) != sizeof(struct CacheItemHeader))
		return -errno;

	size_t to_write = obj->h.struct_size - sizeof(struct CacheItemHeader);
	if(write(fd, obj->path, to_write) != to_write)
		return -errno;

	return 0;
}

static void cacheitem_free(struct CacheItem* obj)
{
	if (!obj)
		return;

	g_free(obj->path);
	g_free(obj);
}

static void cacheitem_free_list(GSList* to_free)
{
	if (!to_free)
		return;

	GSList* iter = to_free;
	while (iter) {
		if (iter->data)
			cacheitem_free(iter->data);
		iter = g_slist_next(iter);
	}
	g_slist_free(to_free);
}

static void cacheitem_touch(struct CacheItem* this)
{
	this->h.mtime = time(NULL);

	/* Attempt to touch the file itself */
	int fd;
	if ( (fd = open(this->path, O_RDWR)) < 0)
		return;

	int bytes_read;
	char buf[512];
	if ( (bytes_read = read(fd, buf, 512 * sizeof(char))) < 0)
		goto out;
	lseek(fd, 0, SEEK_SET);
	write(fd, buf, bytes_read);

out:
	if (fd >= 0)
		close(fd);
}

static gint cache_item_sortfunc(gconstpointer lhs, gconstpointer rhs)
{
	time_t lhs_t = ((struct CacheItem*)lhs)->h.mtime;
	time_t rhs_t = ((struct CacheItem*)rhs)->h.mtime;

	if (lhs_t == rhs_t)
		return 0;
	return (lhs_t < rhs_t ? 1 : -1);
}

static void rebuild_cacheitem_list_from_root_helper(GSList** list, const char* root_path, GDir* root)
{
	const gchar* entry = NULL;
	while ( (entry = g_dir_read_name(root)) ) {
		gchar* full_path = g_build_filename(root_path, entry, NULL);

		/* If this is a file in the cache, add it, sorted by mtime */
		struct CacheItem* item = cacheitem_new(full_path);
		if (item) {
			*list = g_slist_insert_sorted(*list, item, cache_item_sortfunc);
			goto done;
		}

		/* If this is a directory, recurse through it */
		GDir* subdir = NULL;
		if ( (subdir = g_dir_open(full_path, 0, NULL)) ) {
			rebuild_cacheitem_list_from_root_helper(list, full_path, subdir);
			g_dir_close(subdir);
		}
done:
		g_free(full_path);
	}
}

static void rebuild_cacheitem_list_from_root(struct CacheManager* this, const char* root_path)
{
	GSList* ret = NULL;
	GDir* root = g_dir_open(root_path, 0, NULL);
	if (!root)
		return;

	rebuild_cacheitem_list_from_root_helper(&ret, root_path, root);
	g_dir_close(root);

	/* Switch out the list and trash the old one */
	g_static_rw_lock_writer_lock(&this->cached_file_list_rwlock);
	GSList* to_free = this->cached_file_list;
	this->cached_file_list = ret;
	g_static_rw_lock_writer_unlock(&this->cached_file_list_rwlock);

	cacheitem_free_list(to_free);
}

struct CacheManager* cache_manager_new(const char* cache_root, CMCanDeleteCallback callback, gpointer context)
{
	struct CacheManager* ret = g_new0(struct CacheManager, 1);
	if (!ret)
		goto failed;
	ret->cache_root = g_strdup(cache_root);
	ret->can_delete_callback = callback;  ret->user_context = context;

	g_static_rw_lock_init(&ret->cached_file_list_rwlock);

	rebuild_cacheitem_list_from_root(ret, cache_root);

	return ret;

failed:
	if (ret) {
		if (ret->cache_root)
			g_free(ret->cache_root);
		g_free(ret);
	}
	return NULL;
}

void cache_manager_free(struct CacheManager* obj)
{
	if (!obj)
		return;

	cacheitem_free_list(obj->cached_file_list);
	g_free(obj->cache_root);
	g_free(obj);
}

guint64 cache_manager_get_size(struct CacheManager* this)
{
	if (!this)
		return 0;

	/* Run through the list and sum the sizes */
	guint64 ret = 0;
	g_static_rw_lock_reader_lock(&this->cached_file_list_rwlock);
	GSList* iter = this->cached_file_list;
	while (iter) {
		ret += ((struct CacheItem*)iter->data)->h.filesize;
		iter = g_slist_next(iter);
	}
	g_static_rw_lock_reader_unlock(&this->cached_file_list_rwlock);

	return ret;
}


int cache_manager_loadstate(struct CacheManager* this, const char* path)
{
	int fd;
	if ((fd = open(path, O_RDONLY)) < 0)
		return -errno;

	g_static_rw_lock_writer_lock(&this->cached_file_list_rwlock);

	struct CacheItem* item;
	cacheitem_free_list(this->cached_file_list);
	while( (item = cacheitem_load(fd)) ) {
		this->cached_file_list = g_slist_insert_sorted(this->cached_file_list, item, cache_item_sortfunc);
	}

	g_static_rw_lock_writer_unlock(&this->cached_file_list_rwlock);
	close(fd);
	return 0;
}

int cache_manager_savestate(struct CacheManager* this, const char* path)
{
	int ret = 0;
	int fd;
	if ((fd = open(path, O_WRONLY)) < 0)
		return -errno;

	g_static_rw_lock_reader_lock(&this->cached_file_list_rwlock);

	GSList* iter = this->cached_file_list;
	while (iter) {
		struct CacheItem* item = iter->data;

		if ((ret = cacheitem_save(fd, item)))
			goto out;

		iter = g_slist_next(iter);
	}

out:
	g_static_rw_lock_reader_unlock(&this->cached_file_list_rwlock);

	close(fd);
	return ret;
}

void cache_manager_notify_added(struct CacheManager* this, const char* full_path)
{
	struct CacheItem* item = NULL;
	if (! (item = cacheitem_new(full_path)) )
		return;

	g_static_rw_lock_writer_lock(&this->cached_file_list_rwlock);
	this->cached_file_list = g_slist_insert_sorted(this->cached_file_list, item, cache_item_sortfunc);
	g_static_rw_lock_writer_unlock(&this->cached_file_list_rwlock);
}

guint64 cache_manager_reclaim_space(struct CacheManager* this, guint64 max_size)
{
	guint64 current_size = cache_manager_get_size(this);
	if (current_size <= max_size)
		return 0;

	GSList* remove_list = NULL;
	guint64 removed_size = 0;
	guint64 remove_at_least = current_size;

	/* Iterate through the sorted list, looking for files we can delete */
	g_static_rw_lock_reader_lock(&this->cached_file_list_rwlock);
	GSList* iter = this->cached_file_list;
	while (iter && removed_size < remove_at_least) {
		struct CacheItem* item = iter->data;

		if ( item && (this->can_delete_callback)(item->path, this->user_context) ) {
			remove_list = g_slist_prepend(remove_list, item);
			unlink(item->path);
			removed_size += item->h.filesize;
		}

		iter = g_slist_next(iter);
	}
	g_static_rw_lock_reader_unlock(&this->cached_file_list_rwlock);

	/* Remove the list of deleted items from the cache manager and free
	 * the delete list */
	g_static_rw_lock_writer_lock(&this->cached_file_list_rwlock);
	iter = remove_list;
	while (iter) {
		this->cached_file_list = g_slist_remove(this->cached_file_list, iter->data);
		iter = g_slist_next(iter);
	}
	g_static_rw_lock_writer_unlock(&this->cached_file_list_rwlock);

	cacheitem_free_list(remove_list);

	return removed_size;
}

void cache_manager_touch_file(struct CacheManager* this, const char* full_path)
{
	g_static_rw_lock_writer_lock(&this->cached_file_list_rwlock);

	GSList* iter = this->cached_file_list;
	struct CacheItem* item = iter->data;
	while (iter) {
		item = iter->data;
		if (!strcmp(item->path, full_path))
			break;

		iter = g_slist_next(iter);
	}

	if (iter != NULL) {
		this->cached_file_list = g_slist_remove_link(this->cached_file_list, iter);
	}

	g_static_rw_lock_writer_unlock(&this->cached_file_list_rwlock);
}
