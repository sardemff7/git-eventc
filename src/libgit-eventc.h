/*
 * libgit-eventc - Convenience internal library
 *
 * Copyright © 2013-2014 Quentin "Sardem FF7" Glidic
 *
 * This file is part of git-eventc.
 *
 * git-eventc is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * git-eventc is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with git-eventc. If not, see <http://www.gnu.org/licenses/>.
 *
 */

#ifndef __GIT_EVENTC_LIBGIT_EVENTC_H__
#define __GIT_EVENTC_LIBGIT_EVENTC_H__

void git_eventc_parse_options(gint *argc, gchar ***argv, GOptionEntry *extra_entries, const gchar *description, gboolean *print_version);
gboolean git_eventc_init(GMainLoop *loop, gint *retval);
void git_eventc_uninit(void);

gboolean git_eventc_is_above_thresold(guint size);

void git_eventc_send_commit_group(const gchar *pusher_name, guint size, const gchar *url, const gchar *repository_name, const gchar *branch, const gchar *project);
void git_eventc_send_commit(const gchar *id, const gchar *base_message, const gchar *url, const gchar *author_name, const gchar *author_username, const gchar *author_email, const gchar *repository_name, const gchar *branch, const gchar *project);

#endif /* __GIT_EVENTC_LIBGIT_EVENTC_H__ */
