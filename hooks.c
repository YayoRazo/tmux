/* $OpenBSD$ */

/*
 * Copyright (c) 2026 Nicholas Marriott <nicholas.marriott@gmail.com>
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

/* Hook monitor state owned by an option entry. */
struct hook_monitor {
	struct options		*oo;

	struct monitor_set	*set;
	struct events_sink	*sink;
	struct cmd_find_state	 fs;

	enum monitor_type	 type;
	int			 id;
	char			*format;
};

/* Hook monitor event data passed through events_fire. */
struct hook_monitor_event {
	struct hook_monitor	*hm;
	struct monitor_change	*change;
};

/* Hook event sink registered for a notify event name. */
struct hooks_event {
	char			*name;
	struct events_sink	*sink;
	TAILQ_ENTRY(hooks_event) entry;
};
static TAILQ_HEAD(, hooks_event) hooks_events =
    TAILQ_HEAD_INITIALIZER(hooks_events);

/* Insert one hook command list. */
static struct cmdq_item *
hooks_insert_one(struct cmdq_item *item, struct notify_entry *ne,
    struct cmd_list *cmdlist, struct cmdq_state *state)
{
	struct cmdq_item	*new_item;
	char			*s;

	if (cmdlist == NULL)
		return (item);
	if (log_get_level() != 0) {
		s = cmd_list_print(cmdlist, 0);
		log_debug("%s: hook %s is: %s", __func__, ne->name, s);
		free(s);
	}
	new_item = cmdq_get_command(cmdlist, state);
	if (item != NULL)
		return (cmdq_insert_after(item, new_item));
	return (cmdq_append(NULL, new_item));
}

/* Parse a hook command. */
static struct cmd_parse_result *
hooks_parse(struct notify_entry *ne, struct cmd_find_state *fs,
    const char *value)
{
	struct cmd_parse_result	*pr;
	struct format_tree	*ft;
	char			*expanded;

	if (!ne->expand)
		return (cmd_parse_from_string(value, NULL));

	ft = format_create_defaults(NULL, ne->client, fs->s, fs->wl, fs->wp);
	if (ne->formats != NULL)
		format_merge(ft, ne->formats);
	expanded = format_expand(ft, value);
	format_free(ft);

	pr = cmd_parse_from_string(expanded, NULL);
	free(expanded);
	return (pr);
}

/* Insert commands for a hook. */
static void
hooks_insert(struct cmdq_item *item, struct notify_entry *ne)
{
	struct cmd_find_state		 fs;
	struct options			*oo;
	struct cmdq_state		*state;
	struct options_entry		*o;
	struct options_array_item	*a;
	struct cmd_list			*cmdlist;
	const char			*value;
	struct cmd_parse_result		*pr;

	log_debug("%s: inserting hook %s", __func__, ne->name);

	cmd_find_clear_state(&fs, 0);
	if (cmd_find_empty_state(&ne->fs) || !cmd_find_valid_state(&ne->fs))
		cmd_find_from_nothing(&fs, 0);
	else
		cmd_find_copy_state(&fs, &ne->fs);

	if (ne->oo != NULL) {
		oo = ne->oo;
		o = options_get_only(oo, ne->name);
	} else {
		if (fs.s == NULL)
			oo = global_s_options;
		else
			oo = fs.s->options;
		o = options_get(oo, ne->name);
		if (o == NULL && fs.wp != NULL) {
			oo = fs.wp->options;
			o = options_get(oo, ne->name);
		}
		if (o == NULL && fs.wl != NULL) {
			oo = fs.wl->window->options;
			o = options_get(oo, ne->name);
		}
	}
	if (o == NULL) {
		log_debug("%s: hook %s not found", __func__, ne->name);
		return;
	}

	state = cmdq_new_state(&fs, NULL, CMDQ_STATE_NOHOOKS);
	cmdq_add_formats(state, ne->formats);

	if (*ne->name == '@') {
		value = options_get_string(oo, ne->name);
		pr = hooks_parse(ne, &fs, value);
		switch (pr->status) {
		case CMD_PARSE_ERROR:
			log_debug("%s: can't parse hook %s: %s", __func__,
			    ne->name, pr->error);
			free(pr->error);
			break;
		case CMD_PARSE_SUCCESS:
			hooks_insert_one(item, ne, pr->cmdlist, state);
			break;
		}
	} else {
		a = options_array_first(o);
		while (a != NULL) {
			if (ne->expand) {
				value = options_array_item_value(a)->string;
				pr = hooks_parse(ne, &fs, value);
				switch (pr->status) {
				case CMD_PARSE_ERROR:
					if (pr->error != NULL)
						cmdq_error(item, "%s", pr->error);
					break;
				case CMD_PARSE_SUCCESS:
					item = hooks_insert_one(item, ne,
					    pr->cmdlist, state);
					break;
				}
			} else {
				cmdlist = options_array_item_value(a)->cmdlist;
				item = hooks_insert_one(item, ne, cmdlist,
				    state);
			}
			a = options_array_next(a);
		}
	}

	cmdq_free_state(state);
}

/* Insert commands for a hook event. */
static void
hooks_insert_event(struct cmdq_item *item, const char *name,
    struct events_type *type, void *data, struct options *oo, int expand)
{
	struct cmd_find_state	 fs;
	struct notify_entry	 ne;
	struct format_tree	*ft;
	struct client		*c;

	if (item != NULL && (cmdq_get_flags(item) & CMDQ_STATE_NOHOOKS))
		return;

	cmd_find_clear_state(&fs, 0);
	if (!events_find_state(type, data, &fs) ||
	    cmd_find_empty_state(&fs) || !cmd_find_valid_state(&fs))
		cmd_find_from_nothing(&fs, 0);

	c = events_get_client(type, data);
	ft = format_create(c, item, FORMAT_NONE, FORMAT_NOJOBS);
	events_add_formats(type, data, ft);

	memset(&ne, 0, sizeof ne);
	ne.name = name;
	cmd_find_copy_state(&ne.fs, &fs);
	ne.formats = ft;
	ne.oo = oo;
	ne.client = c;
	ne.expand = expand;

	hooks_insert(item, &ne);
	format_free(ft);
}

/* Handle an event for hooks. */
static void
hooks_event_cb(const char *name, void *data, struct events_type *type,
    __unused void *sink_data)
{
	hooks_insert_event(cmdq_running(NULL), name, type, data, NULL, 0);
}

/* Add a hook event sink. */
void
hooks_add_event(const char *name)
{
	struct hooks_event	*he;

	TAILQ_FOREACH(he, &hooks_events, entry) {
		if (strcmp(he->name, name) == 0)
			return;
	}

	he = xcalloc(1, sizeof *he);
	he->name = xstrdup(name);
	he->sink = events_add_sink(name, hooks_event_cb, NULL);
	TAILQ_INSERT_TAIL(&hooks_events, he, entry);
}

/* Run a hook immediately. */
void
hooks_run(struct cmdq_item *item, const char *name)
{
	struct cmd_find_state	*target = cmdq_get_target(item);
	struct notify_entry	 ne;

	memset(&ne, 0, sizeof ne);

	ne.name = name;
	cmd_find_copy_state(&ne.fs, target);

	ne.client = cmdq_get_client(item);
	ne.session = target->s;
	ne.window = target->w;
	ne.pane = (target->wp != NULL ? (int)target->wp->id : -1);

	ne.formats = format_create(NULL, NULL, 0, FORMAT_NOJOBS);
	format_add(ne.formats, "hook", "%s", name);
	format_log_debug(ne.formats, __func__);

	hooks_insert(item, &ne);
	format_free(ne.formats);
}

/* Add formats for a hook monitor event. */
static void
hooks_monitor_event_add_formats(void *data, struct format_tree *ft)
{
	struct hook_monitor_event	*hme = data;
	struct monitor_change		*change = hme->change;
	struct window			*w;

	format_add(ft, "hook", "%s", change->name);
	format_add(ft, "hook_value", "%s",
	    change->value == NULL ? "" : change->value);
	format_add(ft, "hook_last", "%s",
	    change->last == NULL ? "" : change->last);
	if (change->s != NULL) {
		format_add(ft, "hook_session", "$%u", change->s->id);
		format_add(ft, "hook_session_name", "%s", change->s->name);
	}
	if (change->wl != NULL) {
		w = change->wl->window;
		format_add(ft, "hook_window", "@%u", w->id);
		format_add(ft, "hook_window_name", "%s", w->name);
		format_add(ft, "hook_window_index", "%d", change->wl->idx);
	}
	if (change->wp != NULL)
		format_add(ft, "hook_pane", "%%%u", change->wp->id);
}

/* Find state for a hook monitor event. */
static int
hooks_monitor_event_find_state(void *data, struct cmd_find_state *fs)
{
	struct hook_monitor_event	*hme = data;
	struct hook_monitor		*hm = hme->hm;
	struct monitor_change		*change = hme->change;

	if (change->wp != NULL && change->wl != NULL)
		cmd_find_from_winlink_pane(fs, change->wl, change->wp, 0);
	else if (change->wl != NULL)
		cmd_find_from_winlink(fs, change->wl, 0);
	else if (change->s != NULL)
		cmd_find_from_session(fs, change->s, 0);
	else
		cmd_find_copy_state(fs, &hm->fs);
	return (1);
}

/* Get client for a hook monitor event. */
static struct client *
hooks_monitor_event_get_client(void *data)
{
	struct hook_monitor_event	*hme = data;
	struct monitor_change		*change = hme->change;

	return (change->c);
}

/* Free a hook monitor. */
void
hooks_monitor_free(void *data)
{
	struct hook_monitor	*hm = data;

	events_remove_sink(hm->sink);
	monitor_destroy(hm->set);
	free(hm->format);
	free(hm);
}

/* Remove a hook monitor. */
void
hooks_monitor_remove(struct options *oo, const char *name)
{
	struct options_entry	*o;
	struct hook_monitor	*hm;

	o = options_get_only(oo, name);
	if (o == NULL)
		return;

	hm = options_get_monitor_data(o);
	if (hm != NULL) {
		options_set_monitor_data(o, NULL);
		hooks_monitor_free(hm);
	}
}

/* Handle a hook monitor event. */
static void
hooks_monitor_hook_cb(const char *name, void *data, struct events_type *type,
    void *sink_data)
{
	struct hook_monitor_event	*hme = data;
	struct hook_monitor		*hm = sink_data;

	if (hme->hm != hm)
		return;

	hooks_insert_event(cmdq_running(NULL), name, type, data, hm->oo, 1);
}

/* Fire a hook monitor event. */
static void
hooks_monitor_cb(struct monitor_change *change, void *data)
{
	struct hook_monitor_event	hme;

	hme.hm = data;
	hme.change = change;
	events_fire(change->name, &hme);
}

/* Add a hook monitor. */
void
hooks_monitor_add(__unused struct cmdq_item *item, struct options *oo,
    const char *name, enum monitor_type type, int id, const char *format,
    struct cmd_find_state *fs, struct session *s)
{
	struct options_entry	*o;
	struct hook_monitor	*hm;

	hooks_monitor_remove(oo, name);
	o = options_get_only(oo, name);
	if (o == NULL)
		o = options_set_string(oo, name, 0, "%s", "");

	hm = xcalloc(1, sizeof *hm);
	hm->oo = oo;
	cmd_find_copy_state(&hm->fs, fs);
	hm->type = type;
	hm->id = id;
	hm->format = xstrdup(format);
	hm->set = monitor_create_session(s, hooks_monitor_cb, hm);
	events_add_event(name, hooks_monitor_event_add_formats,
	    hooks_monitor_event_find_state, hooks_monitor_event_get_client);
	hm->sink = events_add_sink(name, hooks_monitor_hook_cb, hm);
	options_set_monitor_data(o, hm);
	monitor_add(hm->set, name, type, id, format, 0);
}

/* Convert a hook monitor to its value. */
char *
hooks_monitor_to_string(struct options_entry *o)
{
	struct hook_monitor	*hm = options_get_monitor_data(o);
	const char		*name = options_name(o);
	char			*s;

	if (hm == NULL)
		return (NULL);

	switch (hm->type) {
	case MONITOR_SESSION:
		xasprintf(&s, "%s::%s", name, hm->format);
		break;
	case MONITOR_PANE:
		xasprintf(&s, "%s:%%%d:%s", name, hm->id, hm->format);
		break;
	case MONITOR_ALL_PANES:
		xasprintf(&s, "%s:%%*:%s", name, hm->format);
		break;
	case MONITOR_WINDOW:
		xasprintf(&s, "%s:@%d:%s", name, hm->id, hm->format);
		break;
	case MONITOR_ALL_WINDOWS:
		xasprintf(&s, "%s:@*:%s", name, hm->format);
		break;
	}
	return (s);
}
