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

typedef enum {
    GIT_EVENTC_BUG_REPORT_ACTION_OPENING,
    GIT_EVENTC_BUG_REPORT_ACTION_CLOSING,
    GIT_EVENTC_BUG_REPORT_ACTION_REOPENING,
    GIT_EVENTC_BUG_REPORT_NUM_ACTION,
} GitEventBugReportAction;

typedef enum {
    GIT_EVENTC_MERGE_REQUEST_ACTION_OPENING,
    GIT_EVENTC_MERGE_REQUEST_ACTION_CLOSING,
    GIT_EVENTC_MERGE_REQUEST_ACTION_REOPENING,
    GIT_EVENTC_MERGE_REQUEST_ACTION_MERGE,
    GIT_EVENTC_MERGE_REQUEST_NUM_ACTION,
} GitEventMergeRequestAction;

typedef enum {
    GIT_EVENTC_CI_BUILD_ACTION_SUCCESS,
    GIT_EVENTC_CI_BUILD_ACTION_FAILURE,
    GIT_EVENTC_CI_BUILD_ACTION_ERROR,
    GIT_EVENTC_CI_BUILD_NUM_ACTION,
} GitEventCiBuildAction;

extern const gchar * const git_eventc_bug_report_actions[GIT_EVENTC_BUG_REPORT_NUM_ACTION];
extern const gchar * const git_eventc_merge_request_actions[GIT_EVENTC_MERGE_REQUEST_NUM_ACTION];
extern const gchar * const git_eventc_ci_build_actions[GIT_EVENTC_CI_BUILD_NUM_ACTION];

gsize git_eventc_get_path_prefix_length(const gchar *a, const gchar *b, gsize max_length);
gchar *git_eventc_get_files(GList *paths);

typedef gboolean (*GitEventcKeyFileFunc)(GKeyFile *key_file, GError **error);
gboolean git_eventc_parse_options(gint *argc, gchar ***argv, const gchar *group, GOptionEntry *extra_entries, const gchar *description, GitEventcKeyFileFunc extra_parsing, gboolean *print_version);
gboolean git_eventc_init(GMainLoop *loop, gint *retval);
void git_eventc_disconnect(void);
void git_eventc_uninit(void);

gboolean git_eventc_is_above_threshold(guint size);

gchar *git_eventc_get_url(gchar *url);
gchar *git_eventc_get_url_const(const gchar *url);

typedef struct {
    const gchar **project;
    const gchar *repository_name;
    const gchar *repository_url;
    const gchar *repository_namespace;
    gchar *url;
    GVariant *extra_data;
} GitEventcEventBase;

G_GNUC_NULL_TERMINATED
void git_eventc_send_branch_creation(const GitEventcEventBase *base, const gchar *pusher_name, const gchar *pusher_username, const gchar *pusher_email, const gchar *branch, ...);
G_GNUC_NULL_TERMINATED
void git_eventc_send_branch_deletion(const GitEventcEventBase *base, const gchar *pusher_name, const gchar *pusher_username, const gchar *pusher_email, const gchar *branch, ...);

G_GNUC_NULL_TERMINATED
void git_eventc_send_tag_creation(const GitEventcEventBase *base, const gchar *pusher_name, const gchar *pusher_username, const gchar *pusher_email, const gchar *tag, const gchar *author_name, const gchar *author_email, const gchar *message, const gchar *previous_tag, ...);
G_GNUC_NULL_TERMINATED
void git_eventc_send_tag_deletion(const GitEventcEventBase *base, const gchar *pusher_name, const gchar *pusher_username, const gchar *pusher_email, const gchar *tag, ...);

G_GNUC_NULL_TERMINATED
void git_eventc_send_commit_group(const GitEventcEventBase *base, const gchar *pusher_name, const gchar *pusher_username, const gchar *pusher_email, guint size, const gchar *branch, ...);
G_GNUC_NULL_TERMINATED
void git_eventc_send_commit(const GitEventcEventBase *base, const gchar *id, const gchar *base_message, const gchar *pusher_name, const gchar *pusher_username, const gchar *pusher_email, const gchar *author_name, const gchar *author_username, const gchar *author_email, const gchar *branch, const gchar *files, ...);
G_GNUC_NULL_TERMINATED
void git_eventc_send_push(const GitEventcEventBase *base, const gchar *pusher_name, const gchar *pusher_username, const gchar *pusher_email, const gchar *branch, ...);

G_GNUC_NULL_TERMINATED
void git_eventc_send_bugreport(const GitEventcEventBase *base, const gchar *action, guint64 id, const gchar *title, const gchar *author_name, const gchar *author_username, const gchar *author_email, GVariant *tags, ...);
G_GNUC_NULL_TERMINATED
void git_eventc_send_merge_request(const GitEventcEventBase *base, const gchar *action, guint64 id, const gchar *title, const gchar *author_name, const gchar *author_username, const gchar *author_email, GVariant *tags, const gchar *branch, ...);

G_GNUC_NULL_TERMINATED
void git_eventc_send_ci_build(const GitEventcEventBase *base, const gchar *action, guint64 id, const gchar *branch, guint64 duration, ...);
G_GNUC_NULL_TERMINATED
void git_eventc_send_ci_build_for_merge_request(const GitEventcEventBase *base, const gchar *action, guint64 id, const gchar *branch, guint64 duration, guint64 mr_id, const gchar *mr_title, gchar *mr_url, ...);

#endif /* __GIT_EVENTC_LIBGIT_EVENTC_H__ */
