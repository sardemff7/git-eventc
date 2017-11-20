/*
 * git-eventc-webhook - WebHook to eventd server for various Git hosting providers
 *
 * Copyright © 2013-2017 Quentin "Sardem FF7" Glidic
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

#include "config.h"

#include <string.h>

#include <glib.h>
#include <glib-object.h>
#ifdef G_OS_UNIX
#include <glib-unix.h>
#endif /* G_OS_UNIX */

#include <libsoup/soup.h>
#include <json-glib/json-glib.h>

#include "nkutils-enum.h"
#include "libgit-eventc.h"
#include "webhook.h"
#include "webhook-gitlab.h"

static JsonNode *
_git_eventc_webhook_gitlab_api_get(JsonObject *root, const gchar *suffix, gsize length)
{
    JsonObject *project = json_object_get_object_member(root, "project");
    const gchar *web_url = json_object_get_string_member(project, "web_url");
    const gchar *path_with_namespace = json_object_get_string_member(project, "path_with_namespace");
    gsize pl, l;
    gchar *url;

    pl = strlen(web_url) - strlen(path_with_namespace);
    l = pl + strlen("api/v4") + length + 1;
    url = g_alloca(sizeof(gchar) * l);
    g_snprintf(url, l, "%.*sapi/v4%s", (gint) pl, web_url, suffix);

    return git_eventc_webhook_api_get(url);
}

static JsonNode *
_git_eventc_webhook_gitlab_api_get_project(JsonObject *root, const gchar *suffix, gsize length)
{
    gint64 id = json_object_get_int_member(root, "project_id");
    gsize l;
    gchar *url;

    l = strlen("projects/") + 20 /* max int64 */ + length + 1;
    url = g_alloca(sizeof(gchar) * l);
    g_snprintf(url, l, "/projects/%" G_GINT64_FORMAT"%s", id, suffix);

    return _git_eventc_webhook_gitlab_api_get(root, url, l - 1);
}

static JsonObject *
_git_eventc_webhook_gitlab_get_user(JsonObject *root, gint64 id)
{
    gsize l;
    gchar *url;
    l = strlen("users/") + 20 /* max int64 */ + 1;
    url = g_alloca(sizeof(gchar) * l);
    g_snprintf(url, l, "/users/%" G_GINT64_FORMAT, id);

    g_debug("GET USER %s", url);

    JsonNode *node;
    JsonObject *user;
    node = _git_eventc_webhook_gitlab_api_get(root, url, l - 1);
    user = json_object_ref(json_node_get_object(node));
    json_node_free(node);

    return user;
}

static JsonArray *
_git_eventc_webhook_gitlab_get_tags(JsonObject *root)
{
    JsonNode *node;
    JsonArray *tags;

    node = _git_eventc_webhook_gitlab_api_get_project(root, "/repository/tags", strlen("/repository/tags"));

    tags = json_array_ref(json_node_get_array(node));
    json_node_free(node);

    return tags;
}

static gchar *
_git_eventc_webhook_payload_pusher_name_gitlab(JsonObject *root)
{
    const gchar *name = json_object_get_string_member(root, "user_name");
    const gchar *login = json_object_get_string_member(root, "user_username");

    gchar *pusher_name;
    if ( name != NULL )
        pusher_name = g_strdup_printf("%s (%s)", name, login);
    else
        pusher_name = g_strdup(login);

    return pusher_name;
}

static gchar *
_git_eventc_webhook_payload_get_files_gitlab(JsonObject *commit)
{
    JsonArray *added_files, *modified_files, *removed_files;
    added_files = json_object_get_array_member(commit, "added");
    modified_files = json_object_get_array_member(commit, "modified");
    removed_files = json_object_get_array_member(commit, "removed");

    GList *paths = NULL;
    paths = g_list_concat(paths, json_array_get_elements(added_files));
    paths = g_list_concat(paths, json_array_get_elements(modified_files));
    paths = g_list_concat(paths, json_array_get_elements(removed_files));

    git_eventc_webhook_node_list_to_string_list(paths);

    return git_eventc_get_files(paths);
}

void
git_eventc_webhook_payload_parse_gitlab_branch(const gchar **project, JsonObject *root)
{
    const gchar *branch = json_object_get_string_member(root, "ref") + strlen("refs/heads/");

    JsonObject *repository = json_object_get_object_member(root, "project");

    JsonArray *commits = json_object_get_array_member(root, "commits");
    guint size = json_array_get_length(commits);

    const gchar *repository_name = json_object_get_string_member(repository, "name");
    const gchar *repository_url = json_object_get_string_member(repository, "git_http_url");
    gchar *pusher_name = _git_eventc_webhook_payload_pusher_name_gitlab(root);

    const gchar *web_url = json_object_get_string_member(repository, "web_url");
    const gchar *before = json_object_get_string_member(root, "before");
    const gchar *after = json_object_get_string_member(root, "after");

    gchar *diff_url;
    diff_url = git_eventc_get_url(g_strdup_printf("%s/compare/%s...%s", web_url, after, before));

    if ( g_strcmp0(before, "0000000000000000000000000000000000000000") == 0 )
    {
        gchar *url;
        url = git_eventc_get_url(g_strdup_printf("%s/tree/%s", web_url, branch));
        git_eventc_send_branch_creation(pusher_name, url, repository_name, repository_url, branch, project);
    }
    else if ( g_strcmp0(after, "0000000000000000000000000000000000000000") == 0 )
    {
        git_eventc_send_branch_deletion(pusher_name, repository_name, repository_url, branch, project);
        goto send_push;
    }

    if ( git_eventc_is_above_threshold(size) )
    {
        git_eventc_send_commit_group(
            pusher_name,
            size,
            g_strdup(diff_url),
            repository_name, repository_url, branch, project);
    }
    else
    {
        GList *commit_list = json_array_get_elements(commits);
        GList *commit_;
        for ( commit_ = commit_list ; commit_ != NULL ; commit_ = g_list_next(commit_) )
        {
            JsonObject *commit = json_node_get_object(commit_->data);
            JsonObject *author = json_object_get_object_member(commit, "author");

            gchar *url, *files;
            url = git_eventc_get_url_const(json_object_get_string_member(commit, "url"));
            files = _git_eventc_webhook_payload_get_files_gitlab(commit);

            git_eventc_send_commit(
                json_object_get_string_member(commit, "id"),
                json_object_get_string_member(commit, "message"),
                url,
                pusher_name,
                json_object_get_string_member(author, "name"),
                NULL,
                json_object_get_string_member(author, "email"),
                repository_name, repository_url, branch,
                files,
                project);

            g_free(files);
        }
        g_list_free(commit_list);
    }

send_push:
    git_eventc_send_push(diff_url, pusher_name, repository_name, repository_url, branch, project);

    g_free(pusher_name);
}

void
git_eventc_webhook_payload_parse_gitlab_tag(const gchar **project, JsonObject *root)
{
    const gchar *tag = json_object_get_string_member(root, "ref") + strlen("refs/tags/");

    JsonObject *repository = json_object_get_object_member(root, "project");

    const gchar *repository_name = json_object_get_string_member(repository, "name");
    const gchar *repository_url = json_object_get_string_member(repository, "git_http_url");
    gchar *pusher_name = _git_eventc_webhook_payload_pusher_name_gitlab(root);

    const gchar *web_url = json_object_get_string_member(repository, "web_url");
    const gchar *before = json_object_get_string_member(root, "before");
    const gchar *after = json_object_get_string_member(root, "after");

    gchar *url;
    url = git_eventc_get_url(g_strdup_printf("%s/tags/%s", web_url, tag));

    if ( g_strcmp0(before, "0000000000000000000000000000000000000000") != 0 )
            git_eventc_send_tag_deletion(pusher_name, repository_name, repository_url, tag, project);

    if ( g_strcmp0(after, "0000000000000000000000000000000000000000") != 0 )
    {
        JsonArray *tags = _git_eventc_webhook_gitlab_get_tags(repository);
        guint length = json_array_get_length(tags);
        const gchar *previous_tag = NULL;

        if ( length > 1 )
            previous_tag = json_object_get_string_member(json_array_get_object_element(tags, 1), "name");

        git_eventc_send_tag_creation(pusher_name, g_strdup(url), repository_name, repository_url, tag, NULL, NULL, NULL, previous_tag, project);

        json_array_unref(tags);
    }

    git_eventc_send_push(url, pusher_name, repository_name, repository_url, NULL, project);

    g_free(pusher_name);
}

static const gchar * const _git_eventc_webhook_gitlab_issue_action_name[] = {
    [GIT_EVENTC_BUG_REPORT_ACTION_OPENING]  = "open",
    [GIT_EVENTC_BUG_REPORT_ACTION_CLOSING]  = "close",
    [GIT_EVENTC_BUG_REPORT_ACTION_REOPENING] = "reopen",
};

void
git_eventc_webhook_payload_parse_gitlab_issue(const gchar **project, JsonObject *root)
{
    JsonObject *issue = json_object_get_object_member(root, "object_attributes");
    const gchar *action_str = json_object_get_string_member(issue, "action");
    guint64 action;

    if ( ! nk_enum_parse(action_str, _git_eventc_webhook_gitlab_issue_action_name, GIT_EVENTC_BUG_REPORT_NUM_ACTION, TRUE, FALSE, &action) )
        return;

    JsonObject *author = _git_eventc_webhook_gitlab_get_user(root, json_object_get_int_member(issue, "author_id"));

    JsonObject *repository = json_object_get_object_member(root, "repository");
    const gchar *repository_name = json_object_get_string_member(repository, "name");
    const gchar *repository_url = json_object_get_string_member(repository, "url");

    JsonArray *tags_array = json_object_get_array_member(root, "labels");
    guint length = json_array_get_length(tags_array);
    GVariant *tags = NULL;
    if ( length > 0 )
    {
        GVariantBuilder *builder;
        guint i;
        builder = g_variant_builder_new(G_VARIANT_TYPE_STRING_ARRAY);
        for ( i = 0 ; i < length ; ++i )
            g_variant_builder_add_value(builder, g_variant_new_string(json_object_get_string_member(json_array_get_object_element(tags_array, i), "title")));
        tags = g_variant_builder_end(builder);
        g_variant_builder_unref(builder);
    }

    gchar *url;
    url = git_eventc_get_url_const(json_object_get_string_member(issue, "url"));

    git_eventc_send_bugreport(git_eventc_bug_report_actions[action],
        json_object_get_int_member(issue, "iid"),
        json_object_get_string_member(issue, "title"),
        url,
        json_object_get_string_member(author, "name"),
        json_object_get_string_member(author, "username"),
        NULL,
        tags,
        repository_name, repository_url, project);
}
