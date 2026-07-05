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

/* Add formats for a notify event. */
static void
notify_add_formats(void *data, struct format_tree *ft)
{
	struct notify_entry	*ne = data;

	if (ne->formats != NULL)
		format_merge(ft, ne->formats);
}

/* Find state for a notify event. */
static int
notify_find_state(void *data, struct cmd_find_state *fs)
{
	struct notify_entry	*ne = data;

	if (cmd_find_empty_state(&ne->fs) || !cmd_find_valid_state(&ne->fs))
		return (0);
	cmd_find_copy_state(fs, &ne->fs);
	return (1);
}

/* Get client for a notify event. */
static struct client *
notify_get_client(void *data)
{
	struct notify_entry	*ne = data;

	return (ne->client);
}

/* Build a notify event type. */
static void
notify_build_event(const char *name)
{
	events_add_event(name, notify_add_formats, notify_find_state,
	    notify_get_client);
	hooks_add_event(name);
}

/* Fire a queued notify event. */
static enum cmd_retval
notify_callback(__unused struct cmdq_item *item, void *data)
{
	struct notify_entry	*ne = data;

	log_debug("%s: %s", __func__, ne->name);

	notify_build_event(ne->name);
	events_fire(ne->name, ne);

	if (ne->client != NULL)
		server_client_unref(ne->client);
	if (ne->session != NULL)
		session_remove_ref(ne->session, __func__);
	if (ne->window != NULL)
		window_remove_ref(ne->window, __func__);

	if (ne->fs.s != NULL)
		session_remove_ref(ne->fs.s, __func__);

	format_free(ne->formats);
	free((void *)ne->name);
	free((void *)ne->pbname);
	free(ne);

	return (CMD_RETURN_NORMAL);
}

/* Queue a notify event. */
static void
notify_add(const char *name, struct cmd_find_state *fs, struct client *c,
    struct session *s, struct window *w, struct window_pane *wp,
    const char *pbname)
{
	struct notify_entry	*ne;
	struct cmdq_item	*item;

	item = cmdq_running(NULL);
	if (item != NULL && (cmdq_get_flags(item) & CMDQ_STATE_NOHOOKS))
		return;

	ne = xcalloc(1, sizeof *ne);
	ne->name = xstrdup(name);

	ne->client = c;
	ne->session = s;
	ne->window = w;
	ne->pane = (wp != NULL ? (int)wp->id : -1);
	ne->pbname = (pbname != NULL ? xstrdup(pbname) : NULL);

	ne->formats = format_create(NULL, NULL, 0, FORMAT_NOJOBS);
	format_add(ne->formats, "hook", "%s", name);
	if (c != NULL)
		format_add(ne->formats, "hook_client", "%s", c->name);
	if (s != NULL) {
		format_add(ne->formats, "hook_session", "$%u", s->id);
		format_add(ne->formats, "hook_session_name", "%s", s->name);
	}
	if (w != NULL) {
		format_add(ne->formats, "hook_window", "@%u", w->id);
		format_add(ne->formats, "hook_window_name", "%s", w->name);
	}
	if (wp != NULL) {
		format_add(ne->formats, "hook_pane", "%%%d", wp->id);
		format_add(ne->formats, "hook_window", "@%u", wp->window->id);
		format_add(ne->formats, "hook_window_name", "%s",
		    wp->window->name);
	}
	format_log_debug(ne->formats, __func__);

	if (c != NULL)
		c->references++;
	if (s != NULL)
		session_add_ref(s, __func__);
	if (w != NULL)
		window_add_ref(w, __func__);

	cmd_find_copy_state(&ne->fs, fs);
	if (ne->fs.s != NULL) /* cmd_find_valid_state needs session */
		session_add_ref(ne->fs.s, __func__);

	cmdq_append(NULL, cmdq_get_callback(notify_callback, ne));
}

void
notify_client(const char *name, struct client *c)
{
	struct cmd_find_state	fs;

	cmd_find_from_client(&fs, c, 0);
	notify_add(name, &fs, c, NULL, NULL, NULL, NULL);
}

void
notify_session(const char *name, struct session *s)
{
	struct cmd_find_state	fs;

	if (session_alive(s))
		cmd_find_from_session(&fs, s, 0);
	else
		cmd_find_from_nothing(&fs, 0);
	notify_add(name, &fs, NULL, s, NULL, NULL, NULL);
}

void
notify_winlink(const char *name, struct winlink *wl)
{
	struct cmd_find_state	fs;

	cmd_find_from_winlink(&fs, wl, 0);
	notify_add(name, &fs, NULL, wl->session, wl->window, NULL, NULL);
}

void
notify_session_window(const char *name, struct session *s, struct window *w)
{
	struct cmd_find_state	fs;

	cmd_find_from_session_window(&fs, s, w, 0);
	notify_add(name, &fs, NULL, s, w, NULL, NULL);
}

void
notify_window(const char *name, struct window *w)
{
	struct cmd_find_state	fs;

	cmd_find_from_window(&fs, w, 0);
	notify_add(name, &fs, NULL, NULL, w, NULL, NULL);
}

void
notify_pane(const char *name, struct window_pane *wp)
{
	struct cmd_find_state	fs;

	cmd_find_from_pane(&fs, wp, 0);
	notify_add(name, &fs, NULL, NULL, NULL, wp, NULL);
}

void
notify_paste_buffer(const char *pbname, int deleted)
{
	struct cmd_find_state	fs;

	cmd_find_clear_state(&fs, 0);
	if (deleted) {
		notify_add("paste-buffer-deleted", &fs, NULL, NULL, NULL,
		    NULL, pbname);
	} else {
		notify_add("paste-buffer-changed", &fs, NULL, NULL, NULL,
		    NULL, pbname);
	}
}
