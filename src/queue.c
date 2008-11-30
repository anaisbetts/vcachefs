/*
 * queue.c - Queued workitem system
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
#include "config.h"

struct WorkitemQueue {
	GAsyncQueue* to_process;
	GThread* thread;
	gboolean should_quit;
};

struct Workitem {
	GFunc func;
	gpointer data;
	gpointer context;
};

static gpointer worker_thread_proc(gpointer data)
{
	struct WorkitemQueue* this = data;
	g_async_queue_ref(this->to_process);

	while(!this->should_quit) {
		GTimeVal two_second_delay;
		g_get_current_time(&two_second_delay);
		g_time_val_add(&two_second_delay, 2 * 1000 * 1000);
		struct Workitem* item = g_async_queue_timed_pop(this->to_process, &two_second_delay);

		if (!item)
			continue;

		if (item->func)
			(item->func)(item->data, item->context);
		g_free(item);
	}

	g_async_queue_unref(this->to_process);

	return 0;
}

struct WorkitemQueue* workitem_queue_new(void)
{
	struct WorkitemQueue* ret = g_new0(struct WorkitemQueue, 1);
	if (!ret) 
		goto failed;

	ret->to_process = g_async_queue_new_full(g_free);
	if (!ret->to_process) 
		goto failed;

	if (!(ret->thread = g_thread_create(worker_thread_proc, ret, TRUE, NULL))) 
		goto failed;

	return ret;

failed:
	if (ret) { 
		if (ret->to_process)
			g_async_queue_unref(ret->to_process);
		g_free(ret);
	}

	return NULL; 
}

void workitem_queue_free(struct WorkitemQueue* queue)
{
	if (!queue)
		return;

	queue->should_quit = TRUE;

	/* Clear out the action queue */
	struct Workitem* to_free;
	g_async_queue_lock(queue->to_process);
	while( (to_free = g_async_queue_try_pop_unlocked(queue->to_process)) ) {
		g_free(to_free);
	}

	g_thread_join(queue->thread);
	g_async_queue_unref(queue->to_process);
	g_free(queue);
}

gboolean workitem_queue_insert(struct WorkitemQueue* queue, GFunc func, gpointer data, gpointer context)
{
	if (!queue)
		return FALSE;

	struct Workitem* obj = g_new(struct Workitem, 1);
	obj->func = func;  obj->data = data;  obj->context = context;
	g_async_queue_push(queue->to_process, obj);
	return TRUE;
}
