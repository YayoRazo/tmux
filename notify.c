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

#include "tmux.h"

void
notify_client(const char *name, struct client *c)
{
	struct event_payload	*ep;
	struct cmd_find_state	 fs;

	ep = event_payload_create();
	cmd_find_from_client(&fs, c, 0);
	event_payload_set_target(ep, &fs);
	event_payload_set_client(ep, "client", c);
	events_fire(name, ep);
}

void
notify_session(const char *name, struct session *s)
{
	struct event_payload	*ep;
	struct cmd_find_state	 fs;

	ep = event_payload_create();
	if (session_alive(s)) {
		cmd_find_from_session(&fs, s, 0);
		event_payload_set_target(ep, &fs);
	}
	event_payload_set_session(ep, "session", s);
	events_fire(name, ep);
}

void
notify_winlink(const char *name, struct winlink *wl)
{
	struct event_payload	*ep;
	struct cmd_find_state	 fs;

	ep = event_payload_create();
	cmd_find_from_winlink(&fs, wl, 0);
	event_payload_set_target(ep, &fs);
	event_payload_set_session(ep, "session", wl->session);
	event_payload_set_window(ep, "window", wl->window);
	event_payload_set_int(ep, "window_index", wl->idx);
	events_fire(name, ep);
}

void
notify_session_window(const char *name, struct session *s, struct window *w)
{
	struct event_payload	*ep;
	struct cmd_find_state	 fs;

	ep = event_payload_create();
	if (session_alive(s)) {
		cmd_find_from_session_window(&fs, s, w, 0);
		event_payload_set_target(ep, &fs);
	}
	event_payload_set_session(ep, "session", s);
	event_payload_set_window(ep, "window", w);
	events_fire(name, ep);
}

void
notify_window(const char *name, struct window *w)
{
	struct event_payload	*ep;
	struct cmd_find_state	 fs;

	ep = event_payload_create();
	cmd_find_from_window(&fs, w, 0);
	event_payload_set_target(ep, &fs);
	event_payload_set_window(ep, "window", w);
	events_fire(name, ep);
}

void
notify_pane(const char *name, struct window_pane *wp)
{
	struct event_payload	*ep;
	struct cmd_find_state	 fs;

	ep = event_payload_create();
	cmd_find_from_pane(&fs, wp, 0);
	event_payload_set_target(ep, &fs);
	event_payload_set_pane(ep, "pane", wp);
	event_payload_set_window(ep, "window", wp->window);
	events_fire(name, ep);
}

void
notify_paste_buffer(const char *pbname, int deleted)
{
	struct event_payload	*ep;

	ep = event_payload_create();
	event_payload_set_string(ep, "paste_buffer", "%s", pbname);
	if (deleted)
		events_fire("paste-buffer-deleted", ep);
	else
		events_fire("paste-buffer-changed", ep);
}
