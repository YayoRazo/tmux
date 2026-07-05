/* $OpenBSD$ */

/*
 * Copyright (c) 2026 Nicholas Marriott <nicholas.marriott@gmail.com>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/types.h>

#include <stdlib.h>
#include <string.h>

#include "tmux.h"

/* Event type metadata. */
struct events_type {
	char				*name;

	RB_ENTRY(events_type)		 entry;
};

/* Event sink. */
struct events_sink {
	char				*name;
	events_cb			 cb;
	void				*data;
	int				 dead;
	u_int				 generation;

	TAILQ_ENTRY(events_sink)	 entry;
};

static int
events_type_cmp(struct events_type *et1, struct events_type *et2)
{
	return (strcmp(et1->name, et2->name));
}
RB_HEAD(events_types, events_type);
static struct events_types events_types = RB_INITIALIZER(events_types);
RB_GENERATE_STATIC(events_types, events_type, entry, events_type_cmp);

TAILQ_HEAD(events_sinks, events_sink);
static struct events_sinks events_sinks = TAILQ_HEAD_INITIALIZER(events_sinks);

static u_int events_dispatching;
static u_int events_generation;

/* Find an event type by name. */
static struct events_type *
events_find_type(const char *name)
{
	struct events_type	find;

	find.name = (char *)name;
	return (RB_FIND(events_types, &events_types, &find));
}

/* Free an event sink. */
static void
events_free_sink(struct events_sink *es)
{
	TAILQ_REMOVE(&events_sinks, es, entry);
	free(es->name);
	free(es);
}

/* Free dead event sinks. */
static void
events_free_dead(void)
{
	struct events_sink	*es, *es1;

	TAILQ_FOREACH_SAFE(es, &events_sinks, entry, es1) {
		if (es->dead)
			events_free_sink(es);
	}
}

/* Add an event type. */
int
events_add_event(const char *name)
{
	struct events_type	*et;

	if ((et = events_find_type(name)) != NULL)
		return (0);

	et = xcalloc(1, sizeof *et);
	et->name = xstrdup(name);
	RB_INSERT(events_types, &events_types, et);
	return (0);
}

/* Add an event sink. */
struct events_sink *
events_add_sink(const char *name, events_cb cb, void *data)
{
	struct events_sink	*es;

	es = xcalloc(1, sizeof *es);
	es->name = xstrdup(name);
	es->cb = cb;
	es->data = data;
	es->generation = ++events_generation;

	TAILQ_INSERT_TAIL(&events_sinks, es, entry);
	return (es);
}

/* Remove an event sink. */
void
events_remove_sink(struct events_sink *es)
{
	if (es != NULL && !es->dead) {
		if (events_dispatching != 0)
			es->dead = 1;
		else
			events_free_sink(es);
	}
}

/* Fire an event. */
void
events_fire(const char *name, struct event_payload *ep)
{
	struct events_sink	*es;
	u_int			 generation = events_generation;

	event_payload_log(ep, "%s: %s: ", __func__, name);

	events_dispatching++;
	TAILQ_FOREACH(es, &events_sinks, entry) {
		if (es->dead || es->generation > generation)
			continue;
		if (strcmp(es->name, name) == 0)
			es->cb(name, ep, es->data);
	}
	if (--events_dispatching == 0)
		events_free_dead();
	event_payload_free(ep);
}
