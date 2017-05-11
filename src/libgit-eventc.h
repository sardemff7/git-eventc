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

gsize git_eventc_get_path_prefix_length(const gchar *a, const gchar *b, gsize max_length);
gchar *git_eventc_get_files(GList *paths);

typedef gboolean (*GitEventcKeyFileFunc)(GKeyFile *key_file, GError **error);
gboolean git_eventc_parse_options(gint *argc, gchar ***argv, GOptionEntry *extra_entries, const gchar *description, GitEventcKeyFileFunc extra_parsing, gboolean *print_version);
gboolean git_eventc_init(GMainLoop *loop, gint *retval);
void git_eventc_uninit(void);

gboolean git_eventc_is_above_threshold(guint size);

void git_eventc_send_branch_created(const gchar *pusher_name, const gchar *url, const gchar *repository_name, const gchar *repository_url, const gchar *branch, const gchar **project);
void git_eventc_send_branch_deleted(const gchar *pusher_name, const gchar *repository_name, const gchar *repository_url, const gchar *branch, const gchar **project);

void git_eventc_send_tag_created(const gchar *pusher_name, const gchar *url, const gchar *repository_name, const gchar *repository_url, const gchar *tag, const gchar *previous_tag, const gchar **project);
void git_eventc_send_tag_deleted(const gchar *pusher_name, const gchar *repository_name, const gchar *repository_url, const gchar *tag, const gchar **project);

void git_eventc_send_commit_group(const gchar *pusher_name, guint size, const gchar *url, const gchar *repository_name, const gchar *repository_url, const gchar *branch, const gchar **project);
void git_eventc_send_commit(const gchar *id, const gchar *base_message, const gchar *url, const gchar *author_name, const gchar *author_username, const gchar *author_email, const gchar *repository_name, const gchar *repository_url, const gchar *branch, const gchar *files, const gchar **project);

void git_eventc_send_bugreport(const gchar *action, guint64 number, const gchar *title, const gchar *url, const gchar *author_name, const gchar *author_username, const gchar *author_email, GVariant *tags, const gchar *repository_name, const gchar *repository_url, const gchar **project);
#endif /* __GIT_EVENTC_LIBGIT_EVENTC_H__ */
