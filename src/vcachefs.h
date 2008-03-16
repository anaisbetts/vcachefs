/*
 * vcachefs.h - Userspace video caching filesystem 
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

#ifndef _VCACHEFS_H
#define _VCACHEFS_H

#include <glib.h>

/* This object is the per-mount data we carry around with us throughout the 
 * life of the app until we release it */
struct vcachefs_mount {
	const char* 	source_path;
	
	/* File descriptor table */
	GHashTable* 	fd_table;
	uint 		next_fd;
	GStaticRWLock 	fd_table_rwlock;
};

struct vcachefs_fdentry {
	gint 		refcnt; 
	uint 	 	fd;

	uint64_t 	source_fd;
	off_t 		source_offset;
};

#endif 