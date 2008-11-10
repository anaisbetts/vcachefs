/*
 * cachemgr.h - Userspace video caching filesystem 
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

#ifndef _CACHEMGR_H
#define _CACHEMGR_H

#include "stdafx.h"
#include "queue.h"

typedef gboolean (*CMCanDeleteCallback) (const char* path, gpointer context);
struct CacheManager;

struct CacheManager* cache_manager_new(const char* cache_root, CMCanDeleteCallback callback, gpointer context);
void cache_manager_free(struct CacheManager* obj);
guint64 cache_manager_get_size(struct CacheManager* this);
void cache_manager_notify_added(struct CacheManager* this, const char* full_path);
guint64 cache_manager_reclaim_space(struct CacheManager* this, guint64 max_size);

#endif 
