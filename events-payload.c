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
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER
 * IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING
 * OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/types.h>

#include <event.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "tmux.h"

/* Event payload item. */
struct event_payload_item {
	char				*name;
	enum event_payload_type		 type;

	union {
		char			*string;
		time_t			 time;
		struct client		*client;
		struct session		*session;
		struct window		*window;
		u_int			 pane;
		struct {
			u_int		 session;
			u_int		 window;
			int		 idx;
		} winlink;
		struct {
			void		*ptr;
			event_payload_free_cb free_cb;
			event_payload_print_cb print_cb;
		} pointer;
	};

	RB_ENTRY(event_payload_item)	 entry;
};

static int
event_payload_cmp(struct event_payload_item *epi1,
    struct event_payload_item *epi2)
{
	return (strcmp(epi1->name, epi2->name));
}
RB_HEAD(event_payload, event_payload_item);
RB_GENERATE_STATIC(event_payload, event_payload_item, entry, event_payload_cmp);

/* Find an item. */
static struct event_payload_item *
event_payload_find(struct event_payload *ep, const char *name)
{
	struct event_payload_item	find = { .name = (char *)name };

	return (RB_FIND(event_payload, ep, &find));
}

/* Free the value in an item. */
static void
event_payload_free_value(struct event_payload_item *epi)
{
	switch (epi->type) {
	case EVENT_PAYLOAD_STRING:
		free(epi->string);
		break;
	case EVENT_PAYLOAD_CLIENT:
		if (epi->client != NULL)
			server_client_unref(epi->client);
		break;
	case EVENT_PAYLOAD_SESSION:
		if (epi->session != NULL)
			session_remove_ref(epi->session, __func__);
		break;
	case EVENT_PAYLOAD_WINDOW:
		if (epi->window != NULL)
			window_remove_ref(epi->window, __func__);
		break;
	case EVENT_PAYLOAD_POINTER:
		if (epi->pointer.free_cb != NULL)
			epi->pointer.free_cb(epi->pointer.ptr);
		break;
	case EVENT_PAYLOAD_TIME:
	case EVENT_PAYLOAD_PANE:
	case EVENT_PAYLOAD_WINLINK:
		break;
	}
}

/* Set an item. */
static void
event_payload_set_item(struct event_payload *ep, const char *name,
    struct event_payload_item *new)
{
	struct event_payload_item	*old;

	new->name = xstrdup(name);
	old = RB_INSERT(event_payload, ep, new);
	if (old != NULL) {
		RB_REMOVE(event_payload, ep, old);
		event_payload_free_value(old);
		free(old->name);
		free(old);
		RB_INSERT(event_payload, ep, new);
	}
}

/* Create an event payload. */
struct event_payload *
event_payload_create(void)
{
	struct event_payload	*ep;

	ep = xcalloc(1, sizeof *ep);
	RB_INIT(ep);
	return (ep);
}

/* Free an event payload. */
void
event_payload_free(struct event_payload *ep)
{
	struct event_payload_item	*epi, *epi1;

	if (ep != NULL) {
		RB_FOREACH_SAFE(epi, event_payload, ep, epi1) {
			RB_REMOVE(event_payload, ep, epi);
			event_payload_free_value(epi);
			free(epi->name);
			free(epi);
		}
		free(ep);
	}
}

/* Set a string item. */
void
event_payload_set_string(struct event_payload *ep, const char *name,
    const char *fmt, ...)
{
	struct event_payload_item	*epi;
	va_list				 ap;

	va_start(ap, fmt);

	epi = xcalloc(1, sizeof *epi);
	epi->type = EVENT_PAYLOAD_STRING;
	xvasprintf(&epi->string, fmt, ap);
	event_payload_set_item(ep, name, epi);

	va_end(ap);
}

/* Set a time item. */
void
event_payload_set_time(struct event_payload *ep, const char *name,
    time_t value)
{
	struct event_payload_item	*epi;

	epi = xcalloc(1, sizeof *epi);
	epi->type = EVENT_PAYLOAD_TIME;
	epi->time = value;
	event_payload_set_item(ep, name, epi);
}

/* Set a client item. */
void
event_payload_set_client(struct event_payload *ep, const char *name,
    struct client *c)
{
	struct event_payload_item	*epi;

	if (c != NULL)
		c->references++;

	epi = xcalloc(1, sizeof *epi);
	epi->type = EVENT_PAYLOAD_CLIENT;
	epi->client = c;
	event_payload_set_item(ep, name, epi);
}

/* Set a session item. */
void
event_payload_set_session(struct event_payload *ep, const char *name,
    struct session *s)
{
	struct event_payload_item	*epi;

	if (s != NULL)
		session_add_ref(s, __func__);

	epi = xcalloc(1, sizeof *epi);
	epi->type = EVENT_PAYLOAD_SESSION;
	epi->session = s;
	event_payload_set_item(ep, name, epi);
}

/* Set a window item. */
void
event_payload_set_window(struct event_payload *ep, const char *name,
    struct window *w)
{
	struct event_payload_item	*epi;

	if (w != NULL)
		window_add_ref(w, __func__);

	epi = xcalloc(1, sizeof *epi);
	epi->type = EVENT_PAYLOAD_WINDOW;
	epi->window = w;
	event_payload_set_item(ep, name, epi);
}

/* Set a pane item. */
void
event_payload_set_pane(struct event_payload *ep, const char *name,
    struct window_pane *wp)
{
	struct event_payload_item	*epi;

	epi = xcalloc(1, sizeof *epi);
	epi->type = EVENT_PAYLOAD_PANE;
	epi->pane = wp->id;
	event_payload_set_item(ep, name, epi);
}

/* Set a winlink item. */
void
event_payload_set_winlink(struct event_payload *ep, const char *name,
    struct winlink *wl)
{
	struct event_payload_item	*epi;

	epi = xcalloc(1, sizeof *epi);
	epi->type = EVENT_PAYLOAD_WINLINK;
	epi->winlink.session = wl->session->id;
	epi->winlink.window = wl->window->id;
	epi->winlink.idx = wl->idx;
	event_payload_set_item(ep, name, epi);
}

/* Set a pointer item. */
void
event_payload_set_pointer(struct event_payload *ep, const char *name,
    void *ptr, event_payload_free_cb free_cb, event_payload_print_cb print_cb)
{
	struct event_payload_item	*epi;

	epi = xcalloc(1, sizeof *epi);
	epi->type = EVENT_PAYLOAD_POINTER;
	epi->pointer.ptr = ptr;
	epi->pointer.free_cb = free_cb;
	epi->pointer.print_cb = print_cb;
	event_payload_set_item(ep, name, epi);
}

/* Get a string item. */
const char *
event_payload_get_string(struct event_payload *ep, const char *name)
{
	struct event_payload_item	*epi;

	epi = event_payload_find(ep, name);
	if (epi == NULL || epi->type != EVENT_PAYLOAD_STRING)
		return (NULL);
	return (epi->string);
}

/* Print a payload item. */
static void
event_payload_add_item(struct event_payload_item *epi, struct evbuffer *evb)
{
	switch (epi->type) {
	case EVENT_PAYLOAD_STRING:
		evbuffer_add_printf(evb, "%s", epi->string);
		break;
	case EVENT_PAYLOAD_TIME:
		evbuffer_add_printf(evb, "%lld", (long long)epi->time);
		break;
	case EVENT_PAYLOAD_CLIENT:
		if (epi->client != NULL)
			evbuffer_add_printf(evb, "%s", epi->client->name);
		break;
	case EVENT_PAYLOAD_SESSION:
		if (epi->session != NULL)
			evbuffer_add_printf(evb, "$%u", epi->session->id);
		break;
	case EVENT_PAYLOAD_WINDOW:
		if (epi->window != NULL)
			evbuffer_add_printf(evb, "@%u", epi->window->id);
		break;
	case EVENT_PAYLOAD_PANE:
		evbuffer_add_printf(evb, "%%%u", epi->pane);
		break;
	case EVENT_PAYLOAD_WINLINK:
		evbuffer_add_printf(evb, "$%u:%d", epi->winlink.session,
		    epi->winlink.idx);
		break;
	case EVENT_PAYLOAD_POINTER:
		if (epi->pointer.print_cb != NULL)
			epi->pointer.print_cb(epi->pointer.ptr, evb);
		else
			evbuffer_add_printf(evb, "%p", epi->pointer.ptr);
		break;
	}
}

/* Print a payload item. */
char *
event_payload_item_print(struct event_payload_item *epi)
{
	struct evbuffer			*evb;
	char				*value = NULL;
	size_t				 size;

	evb = evbuffer_new();
	if (evb == NULL)
		fatalx("out of memory");
	event_payload_add_item(epi, evb);
	if ((size = EVBUFFER_LENGTH(evb)) != 0)
		value = xmemdup(EVBUFFER_DATA(evb), size);
	else
		value = xstrdup("");
	evbuffer_free(evb);
	return (value);
}

/* Print an item value. */
char *
event_payload_print(struct event_payload *ep, const char *name)
{
	struct event_payload_item	*epi;

	epi = event_payload_find(ep, name);
	if (epi == NULL)
		return (NULL);
	return (event_payload_item_print(epi));
}

/* Get the first payload item. */
struct event_payload_item *
event_payload_first(struct event_payload *ep)
{
	return (RB_MIN(event_payload, ep));
}

/* Get the next payload item. */
struct event_payload_item *
event_payload_next(struct event_payload_item *epi)
{
	return (RB_NEXT(event_payload, , epi));
}

/* Get a payload item name. */
const char *
event_payload_item_name(struct event_payload_item *epi)
{
	return (epi->name);
}

/* Get a payload item type. */
enum event_payload_type
event_payload_item_type(struct event_payload_item *epi)
{
	return (epi->type);
}

/* Log a payload. */
void
event_payload_log(struct event_payload *ep, const char *fmt, ...)
{
	struct event_payload_item	*epi;
	struct evbuffer			*evb;
	va_list				 ap;
	char				*prefix;

	va_start(ap, fmt);
	xvasprintf(&prefix, fmt, ap);
	va_end(ap);

	evb = evbuffer_new();
	if (evb == NULL)
		fatalx("out of memory");
	if (ep != NULL) {
		RB_FOREACH(epi, event_payload, ep) {
			if (EVBUFFER_LENGTH(evb) != 0)
				evbuffer_add_printf(evb, ", ");
			evbuffer_add_printf(evb, "%s=", epi->name);
			event_payload_add_item(epi, evb);
		}
	}
	log_debug("%s%.*s", prefix, (int)EVBUFFER_LENGTH(evb),
	    (char *)EVBUFFER_DATA(evb));
	evbuffer_free(evb);
	free(prefix);
}

/* Get a time item. */
int
event_payload_get_time(struct event_payload *ep, const char *name,
    time_t *value)
{
	struct event_payload_item	*epi;

	epi = event_payload_find(ep, name);
	if (epi == NULL || epi->type != EVENT_PAYLOAD_TIME)
		return (0);
	*value = epi->time;
	return (1);
}

/* Get a client item. */
struct client *
event_payload_get_client(struct event_payload *ep, const char *name)
{
	struct event_payload_item	*epi;

	epi = event_payload_find(ep, name);
	if (epi == NULL || epi->type != EVENT_PAYLOAD_CLIENT)
		return (NULL);
	return (epi->client);
}

/* Get a session item. */
struct session *
event_payload_get_session(struct event_payload *ep, const char *name)
{
	struct event_payload_item	*epi;

	epi = event_payload_find(ep, name);
	if (epi == NULL || epi->type != EVENT_PAYLOAD_SESSION)
		return (NULL);
	return (epi->session);
}

/* Get a window item. */
struct window *
event_payload_get_window(struct event_payload *ep, const char *name)
{
	struct event_payload_item	*epi;

	epi = event_payload_find(ep, name);
	if (epi == NULL || epi->type != EVENT_PAYLOAD_WINDOW)
		return (NULL);
	return (epi->window);
}

/* Get a pane item. */
struct window_pane *
event_payload_get_pane(struct event_payload *ep, const char *name)
{
	struct event_payload_item	*epi;

	epi = event_payload_find(ep, name);
	if (epi == NULL || epi->type != EVENT_PAYLOAD_PANE)
		return (NULL);
	return (window_pane_find_by_id(epi->pane));
}

/* Get a winlink item. */
struct winlink *
event_payload_get_winlink(struct event_payload *ep, const char *name)
{
	struct event_payload_item	*epi;
	struct session			*s;
	struct winlink			*wl;

	epi = event_payload_find(ep, name);
	if (epi == NULL || epi->type != EVENT_PAYLOAD_WINLINK)
		return (NULL);
	s = session_find_by_id(epi->winlink.session);
	if (s == NULL)
		return (NULL);
	wl = winlink_find_by_index(&s->windows, epi->winlink.idx);
	if (wl == NULL || wl->window->id != epi->winlink.window)
		return (NULL);
	return (wl);
}

/* Get a pointer item. */
void *
event_payload_get_pointer(struct event_payload *ep, const char *name)
{
	struct event_payload_item	*epi;

	epi = event_payload_find(ep, name);
	if (epi == NULL || epi->type != EVENT_PAYLOAD_POINTER)
		return (NULL);
	return (epi->pointer.ptr);
}
