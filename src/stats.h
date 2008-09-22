/*
 * stats.h - Userspace video caching filesystem 
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

#ifndef _STATS_H
#define _STATS_H

#define USE_IOSTATS 	1

#ifndef USE_IOSTATS

#define stats_open_logging() 	NULL
#define stats_close_logging(x)
#define stats_write_record(c, op, of, s, i) 	1

#else

GIOChannel* stats_open_logging(void);
void stats_close_logging(GIOChannel* channel);
int stats_write_record(GIOChannel* channel, const char* operation, off_t offset, size_t size, const char* info);
long long unsigned int get_time_code(void);

#endif

#endif 
