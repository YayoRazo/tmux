/* $OpenBSD$ */

/*
 * Copyright (c) 2012 George Nachman <tmux@georgester.com>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF MIND, USE, DATA OR PROFITS, WHETHER
 * IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING
 * OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/types.h>

#include <stdlib.h>
#include <string.h>

#include "tmux.h"

/* Queued notify event. */
struct notify_event {
	char			*name;
	struct event_payload	*payload;
};

/* Fire a queued notify event. */
static enum cmd_retval
notify_callback(__unused struct cmdq_item *item, void *data)
{
	struct notify_event	*ne = data;

	log_debug("%s: %s", __func__, ne->name);

	events_fire(ne->name, ne->payload);
	ne->payload = NULL;

	free(ne->name);
	free(ne);
	return (CMD_RETURN_NORMAL);
}

/* Queue a notify event. */
static void
notify_add(const char *name, struct event_payload *ep)
{
	struct notify_event	*ne;
	struct cmdq_item	*item;

	item = cmdq_running(NULL);
	if (item != NULL && (cmdq_get_flags(item) & CMDQ_STATE_NOHOOKS)) {
		event_payload_free(ep);
		return;
	}

	ne = xcalloc(1, sizeof *ne);
	ne->name = xstrdup(name);
	ne->payload = ep;

	cmdq_append(NULL, cmdq_get_callback(notify_callback, ne));
}

void
notify_client(const char *name, struct client *c)
{
	struct event_payload	*ep;

	ep = event_payload_create();
	event_payload_set_client(ep, "client", c);
	notify_add(name, ep);
}

void
notify_session(const char *name, struct session *s)
{
	struct event_payload	*ep;

	ep = event_payload_create();
	event_payload_set_session(ep, "session", s);
	notify_add(name, ep);
}

void
notify_winlink(const char *name, struct winlink *wl)
{
	struct event_payload	*ep;

	ep = event_payload_create();
	event_payload_set_session(ep, "session", wl->session);
	event_payload_set_window(ep, "window", wl->window);
	event_payload_set_int(ep, "window_index", wl->idx);
	event_payload_set_winlink(ep, "winlink", wl);
	notify_add(name, ep);
}

void
notify_session_window(const char *name, struct session *s, struct window *w)
{
	struct event_payload	*ep;

	ep = event_payload_create();
	event_payload_set_session(ep, "session", s);
	event_payload_set_window(ep, "window", w);
	notify_add(name, ep);
}

void
notify_window(const char *name, struct window *w)
{
	struct event_payload	*ep;

	ep = event_payload_create();
	event_payload_set_window(ep, "window", w);
	notify_add(name, ep);
}

void
notify_pane(const char *name, struct window_pane *wp)
{
	struct event_payload	*ep;

	ep = event_payload_create();
	event_payload_set_pane(ep, "pane", wp);
	event_payload_set_window(ep, "window", wp->window);
	notify_add(name, ep);
}

void
notify_paste_buffer(const char *pbname, int deleted)
{
	struct event_payload	*ep;

	ep = event_payload_create();
	event_payload_set_string(ep, "paste_buffer", "%s", pbname);
	if (deleted)
		notify_add("paste-buffer-deleted", ep);
	else
		notify_add("paste-buffer-changed", ep);
}
