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
#include "webhook-github.h"

static JsonNode *
_git_eventc_webhook_github_get(const gchar *url)
{
    static SoupSession *session = NULL;
    GError *error = NULL;

    if ( session == NULL )
        session = soup_session_new_with_options(SOUP_SESSION_USER_AGENT, PACKAGE_NAME " " PACKAGE_VERSION, NULL);

    SoupRequestHTTP *req;
    req = soup_session_request_http(session, "GET", url, &error);
    if ( req == NULL )
    {
        g_warning("Couldn't get %s: %s", url, error->message);
        g_clear_error(&error);
        return NULL;
    }

    SoupMessage *msg;
    guint code;

    msg = soup_request_http_get_message(req);
    code = soup_session_send_message(session, msg);

    if ( code != SOUP_STATUS_OK )
    {
        g_warning("Couldn't get %s: %s", url, soup_status_get_phrase(code));
        return NULL;
    }

    JsonParser *parser;
    parser = json_parser_new();
    if ( ! json_parser_load_from_data(parser, msg->response_body->data, msg->response_body->length, &error) )
    {
        g_warning("Couldn't parse answer to %s: %s", url, error->message);
        g_clear_error(&error);
        return NULL;
    }

    JsonNode *node;
    node = json_node_copy(json_parser_get_root(parser));

    g_object_unref(parser);

    return node;
}

static JsonObject *
_git_eventc_webhook_github_get_user(JsonObject *user)
{
    JsonNode *node;

    node = _git_eventc_webhook_github_get(json_object_get_string_member(user, "url"));
    if ( node == NULL )
        return json_object_ref(user);

    user = json_object_ref(json_node_get_object(node));
    json_node_free(node);

    return user;
}

static JsonArray *
_git_eventc_webhook_github_get_tags(JsonObject *repository)
{
    JsonNode *node;
    JsonArray *tags;

    node = _git_eventc_webhook_github_get(json_object_get_string_member(repository, "tags"));

    tags = json_array_ref(json_node_get_array(node));
    json_node_free(node);

    return tags;
}

static void
_git_eventc_webhook_node_list_to_string_list(GList *list)
{
    /* Retrieve the actual strings */
    GList *node;
    for ( node = list ; node != NULL ; node = g_list_next(node) )
        node->data = (gpointer) json_node_get_string(node->data);
}

static gchar *
_git_eventc_webhook_payload_pusher_name_github(JsonObject *root)
{
    JsonObject *sender = json_object_get_object_member(root, "sender");
    JsonObject *sender_user = _git_eventc_webhook_github_get_user(sender);
    const gchar *name = json_object_get_string_member(sender_user, "name");
    const gchar *login = json_object_get_string_member(sender_user, "login");

    gchar *pusher_name;
    if ( name != NULL )
        pusher_name = g_strdup_printf("%s (%s)", name, login);
    else
        pusher_name = g_strdup(login);
    json_object_unref(sender_user);

    return pusher_name;
}

static gchar *
_git_eventc_webhook_payload_get_files_github(JsonObject *commit)
{
    JsonArray *added_files, *modified_files, *removed_files;
    added_files = json_object_get_array_member(commit, "added");
    modified_files = json_object_get_array_member(commit, "modified");
    removed_files = json_object_get_array_member(commit, "removed");

    GList *paths = NULL;
    paths = g_list_concat(paths, json_array_get_elements(added_files));
    paths = g_list_concat(paths, json_array_get_elements(modified_files));
    paths = g_list_concat(paths, json_array_get_elements(removed_files));

    _git_eventc_webhook_node_list_to_string_list(paths);

    return git_eventc_get_files(paths);
}

static void
_git_eventc_webhook_payload_parse_github_branch(const gchar **project, JsonObject *root, const gchar *branch)
{
    JsonObject *repository = json_object_get_object_member(root, "repository");

    JsonArray *commits = json_object_get_array_member(root, "commits");
    guint size = json_array_get_length(commits);

    const gchar *repository_name = json_object_get_string_member(repository, "name");
    const gchar *repository_url = json_object_get_string_member(repository, "url");
    gchar *pusher_name = _git_eventc_webhook_payload_pusher_name_github(root);

    gchar *diff_url;
    diff_url = git_eventc_get_url_const(json_object_get_string_member(root, "compare"));

    if ( json_object_get_boolean_member(root, "created") )
    {
        gchar *url;
        url = git_eventc_get_url(g_strdup_printf("%s/tree/%s", json_object_get_string_member(repository, "url"), branch));
        git_eventc_send_branch_creation(pusher_name, url, repository_name, repository_url, branch, project);
    }
    else if ( json_object_get_boolean_member(root, "deleted") )
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
            files = _git_eventc_webhook_payload_get_files_github(commit);

            git_eventc_send_commit(
                json_object_get_string_member(commit, "id"),
                json_object_get_string_member(commit, "message"),
                url,
                pusher_name,
                json_object_get_string_member(author, "name"),
                json_object_get_string_member(author, "username"),
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

static void
_git_eventc_webhook_payload_parse_github_tag(const gchar **project, JsonObject *root, const gchar *tag)
{
    JsonObject *repository = json_object_get_object_member(root, "repository");

    const gchar *repository_name = json_object_get_string_member(repository, "name");
    const gchar *repository_url = json_object_get_string_member(repository, "url");
    gchar *pusher_name = _git_eventc_webhook_payload_pusher_name_github(root);

    if ( ! json_object_get_boolean_member(root, "created") )
            git_eventc_send_tag_deletion(pusher_name, repository_name, repository_url, tag, project);

    if ( ! json_object_get_boolean_member(root, "deleted") )
    {
        JsonArray *tags = _git_eventc_webhook_github_get_tags(repository);
        guint length = json_array_get_length(tags);
        const gchar *previous_tag = NULL;
        gchar *url;

        url = git_eventc_get_url(g_strdup_printf("%s/releases/tag/%s", json_object_get_string_member(repository, "url"), tag));
        if ( length > 1 )
            previous_tag = json_object_get_string_member(json_array_get_object_element(tags, 1), "name");

        git_eventc_send_tag_creation(pusher_name, url, repository_name, repository_url, tag, NULL, NULL, NULL, previous_tag, project);

        json_array_unref(tags);
    }

    gchar *url;
    url = git_eventc_get_url_const(json_object_get_string_member(root, "compare"));

    git_eventc_send_push(url, pusher_name, repository_name, repository_url, NULL, project);

    g_free(pusher_name);
}

void
git_eventc_webhook_payload_parse_github_push(const gchar **project, JsonObject *root)
{
    const gchar *ref = json_object_get_string_member(root, "ref");

    if ( g_str_has_prefix(ref, "refs/heads/") )
        _git_eventc_webhook_payload_parse_github_branch(project, root, ref + strlen("refs/heads/"));
    else if ( g_str_has_prefix(ref, "refs/tags/") )
        _git_eventc_webhook_payload_parse_github_tag(project, root, ref + strlen("refs/tags/"));
}

static const gchar * const _git_eventc_webhook_github_issue_action_name[] = {
    [GIT_EVENTC_BUG_REPORT_ACTION_OPENING]  = "opened",
    [GIT_EVENTC_BUG_REPORT_ACTION_CLOSING]  = "closed",
    [GIT_EVENTC_BUG_REPORT_ACTION_REOPENING] = "reopened",
};

void
git_eventc_webhook_payload_parse_github_issues(const gchar **project, JsonObject *root)
{
    const gchar *action_str = json_object_get_string_member(root, "action");
    guint64 action;

    if ( ! nk_enum_parse(action_str, _git_eventc_webhook_github_issue_action_name, GIT_EVENTC_BUG_REPORT_NUM_ACTION, TRUE, FALSE, &action) )
        return;

    JsonObject *repository = json_object_get_object_member(root, "repository");
    JsonObject *issue = json_object_get_object_member(root, "issue");
    JsonObject *author = _git_eventc_webhook_github_get_user(json_object_get_object_member(issue, "user"));
    JsonArray *tags_array = json_object_get_array_member(issue, "labels");

    const gchar *repository_name = json_object_get_string_member(repository, "name");
    const gchar *repository_url = json_object_get_string_member(repository, "url");
    guint length = json_array_get_length(tags_array);
    GVariant *tags = NULL;

    if ( length > 0 )
    {
        GVariantBuilder *builder;
        guint i;
        builder = g_variant_builder_new(G_VARIANT_TYPE_STRING_ARRAY);
        for ( i = 0 ; i < length ; ++i )
            g_variant_builder_add_value(builder, g_variant_new_string(json_object_get_string_member(json_array_get_object_element(tags_array, i), "name")));
        tags = g_variant_builder_end(builder);
        g_variant_builder_unref(builder);
    }

    gchar *url;
    url = git_eventc_get_url_const(json_object_get_string_member(issue, "html_url"));

    git_eventc_send_bugreport(git_eventc_bug_report_actions[action],
        json_object_get_int_member(issue, "number"),
        json_object_get_string_member(issue, "title"),
        url,
        json_object_get_string_member(author, "name"),
        json_object_get_string_member(author, "login"),
        json_object_get_string_member(author, "email"),
        tags,
        repository_name, repository_url, project);

    json_object_unref(author);
}
