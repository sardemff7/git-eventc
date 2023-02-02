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

const gchar * const git_eventc_webhook_gitlab_parsers_events[] = {
    [GIT_EVENTC_WEBHOOK_GITLAB_PARSER_PUSH]          = "Push Hook",
    [GIT_EVENTC_WEBHOOK_GITLAB_PARSER_TAG]           = "Tag Push Hook",
    [GIT_EVENTC_WEBHOOK_GITLAB_PARSER_ISSUE]         = "Issue Hook",
    [GIT_EVENTC_WEBHOOK_GITLAB_PARSER_MERGE_REQUEST] = "Merge Request Hook",
    [GIT_EVENTC_WEBHOOK_GITLAB_PARSER_PIPELINE]      = "Pipeline Hook",
    [GIT_EVENTC_WEBHOOK_GITLAB_PARSER_SYSTEM]        = "System Hook",
};

static const gchar * const git_eventc_webhook_gitlab_parsers_system_events[] = {
    [GIT_EVENTC_WEBHOOK_GITLAB_PARSER_PUSH]          = "push",
    [GIT_EVENTC_WEBHOOK_GITLAB_PARSER_TAG]           = "tag_push",
    [GIT_EVENTC_WEBHOOK_GITLAB_PARSER_MERGE_REQUEST] = "merge_request",
};

void git_eventc_webhook_payload_parse_gitlab_branch(GitEventcEventBase *base, JsonObject *root);
void git_eventc_webhook_payload_parse_gitlab_tag(GitEventcEventBase *base, JsonObject *root);
void git_eventc_webhook_payload_parse_gitlab_issue(GitEventcEventBase *base, JsonObject *root);
void git_eventc_webhook_payload_parse_gitlab_merge_request(GitEventcEventBase *base, JsonObject *root);
void git_eventc_webhook_payload_parse_gitlab_pipeline(GitEventcEventBase *base, JsonObject *root);
void git_eventc_webhook_payload_parse_gitlab_system(GitEventcEventBase *base, JsonObject *root);

const GitEventcWebhookParseFunc git_eventc_webhook_gitlab_parsers[] = {
    [GIT_EVENTC_WEBHOOK_GITLAB_PARSER_PUSH]          = git_eventc_webhook_payload_parse_gitlab_branch,
    [GIT_EVENTC_WEBHOOK_GITLAB_PARSER_TAG]           = git_eventc_webhook_payload_parse_gitlab_tag,
    [GIT_EVENTC_WEBHOOK_GITLAB_PARSER_ISSUE]         = git_eventc_webhook_payload_parse_gitlab_issue,
    [GIT_EVENTC_WEBHOOK_GITLAB_PARSER_MERGE_REQUEST] = git_eventc_webhook_payload_parse_gitlab_merge_request,
    [GIT_EVENTC_WEBHOOK_GITLAB_PARSER_PIPELINE]      = git_eventc_webhook_payload_parse_gitlab_pipeline,
    [GIT_EVENTC_WEBHOOK_GITLAB_PARSER_SYSTEM]        = git_eventc_webhook_payload_parse_gitlab_system,
};

static JsonNode *
_git_eventc_webhook_gitlab_api_get(GitEventcEventBase *base, JsonObject *repository, const gchar *suffix, gsize length)
{
    const gchar *web_url = json_object_get_string_member(repository, "web_url");
    const gchar *path_with_namespace = json_object_get_string_member(repository, "path_with_namespace");
    gsize pl, l;
    gchar *url;

    pl = strlen(web_url) - strlen(path_with_namespace);
    l = pl + strlen("api/v4") + length + 1;
    url = g_alloca(sizeof(gchar) * l);
    g_snprintf(url, l, "%.*sapi/v4%s", (gint) pl, web_url, suffix);

    return git_eventc_webhook_api_get(base, url);
}

static JsonNode *
_git_eventc_webhook_gitlab_api_get_project(GitEventcEventBase *base, JsonObject *repository, const gchar *suffix, gsize length)
{
    gint64 id = json_object_get_int_member(repository, "id");
    gsize l;
    gchar *url;

    l = strlen("/projects/") + 20 /* max int64 */ + length + 1;
    url = g_alloca(sizeof(gchar) * l);
    g_snprintf(url, l, "/projects/%" G_GINT64_FORMAT"%s", id, suffix);

    return _git_eventc_webhook_gitlab_api_get(base, repository, url, l - 1);
}

static JsonObject *
_git_eventc_webhook_gitlab_get_user(GitEventcEventBase *base, JsonObject *repository, gint64 id)
{
    gsize l;
    gchar *url;
    l = strlen("/users/") + 20 /* max int64 */ + 1;
    url = g_alloca(sizeof(gchar) * l);
    g_snprintf(url, l, "/users/%" G_GINT64_FORMAT, id);

    g_debug("GET USER %s", url);

    JsonNode *node;
    JsonObject *user;
    node = _git_eventc_webhook_gitlab_api_get(base, repository, url, l - 1);
    if ( node == NULL )
        return NULL;
    user = json_object_ref(json_node_get_object(node));
    json_node_free(node);

    return user;
}

static const gchar *
_git_eventc_webhook_gitlab_get_email(JsonObject *object, const gchar *member)
{
    const gchar *email = json_get_string_safe(object, member);
    if ( email == NULL )
        return NULL;
    if ( g_strcmp0(email, "[REDACTED]") == 0 )
        return NULL;
    return email;
}

static GVariant *
_git_eventc_webhook_gitlab_get_email_gvariant(JsonObject *object, const gchar *member)
{
    const gchar *email = _git_eventc_webhook_gitlab_get_email(object, member);
    return ( email == NULL ) ? NULL : g_variant_new_string(email);
}

static JsonArray *
_git_eventc_webhook_gitlab_get_tags(GitEventcEventBase *base, JsonObject *repository)
{
    JsonNode *node;
    JsonArray *tags;

    node = _git_eventc_webhook_gitlab_api_get_project(base, repository, "/repository/tags", strlen("/repository/tags"));

    tags = json_array_ref(json_node_get_array(node));
    json_node_free(node);

    return tags;
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
git_eventc_webhook_payload_parse_gitlab_branch(GitEventcEventBase *base, JsonObject *root)
{
    const gchar *branch = json_object_get_string_member(root, "ref") + strlen("refs/heads/");

    JsonObject *repository = json_object_get_object_member(root, "project");

    JsonArray *commits = json_object_get_array_member(root, "commits");
    guint size = json_array_get_length(commits);

    base->repository_name = json_object_get_string_member(repository, "name");
    base->repository_url = json_object_get_string_member(repository, "git_http_url");

    const gchar *web_url = json_object_get_string_member(repository, "web_url");
    const gchar *before = json_object_get_string_member(root, "before");
    const gchar *after = json_object_get_string_member(root, "after");

    gchar *diff_url;
    diff_url = git_eventc_get_url(g_strdup_printf("%s/compare/%s...%s", web_url, before, after));

    if ( g_strcmp0(before, "0000000000000000000000000000000000000000") == 0 )
    {
        base->url = git_eventc_get_url(g_strdup_printf("%s/tree/%s", web_url, branch));
        git_eventc_send_branch_creation(base,
            json_object_get_string_member(root, "user_name"),
            json_object_get_string_member(root, "user_username"),
            _git_eventc_webhook_gitlab_get_email(root, "user_email"),
            branch,
            "pusher-avatar-url", json_get_string_gvariant_safe(root, "user_avatar"),
            NULL);
    }
    else if ( g_strcmp0(after, "0000000000000000000000000000000000000000") == 0 )
    {
        git_eventc_send_branch_deletion(base,
            json_object_get_string_member(root, "user_name"),
            json_object_get_string_member(root, "user_username"),
            _git_eventc_webhook_gitlab_get_email(root, "user_email"),
            branch,
            "pusher-avatar-url", json_get_string_gvariant_safe(root, "user_avatar"),
            NULL);
        goto send_push;
    }

    if ( git_eventc_is_above_threshold(size) )
    {
        base->url = g_strdup(diff_url);
        git_eventc_send_commit_group(base,
            json_object_get_string_member(root, "user_name"),
            json_object_get_string_member(root, "user_username"),
            _git_eventc_webhook_gitlab_get_email(root, "user_email"),
            size,
            branch,
            "pusher-avatar-url", json_get_string_gvariant_safe(root, "user_avatar"),
            NULL);
    }
    else
    {
        GList *commit_list = json_array_get_elements(commits);
        GList *commit_;
        for ( commit_ = commit_list ; commit_ != NULL ; commit_ = g_list_next(commit_) )
        {
            JsonObject *commit = json_node_get_object(commit_->data);
            JsonObject *author = json_object_get_object_member(commit, "author");

            gchar *files;
            base->url = git_eventc_get_url_const(json_object_get_string_member(commit, "url"));
            files = _git_eventc_webhook_payload_get_files_gitlab(commit);

            git_eventc_send_commit(base,
                json_object_get_string_member(commit, "id"),
                json_object_get_string_member(commit, "message"),
                json_object_get_string_member(root, "user_name"),
                json_object_get_string_member(root, "user_username"),
                _git_eventc_webhook_gitlab_get_email(root, "user_email"),
                json_object_get_string_member(author, "name"),
                NULL,
                json_object_get_string_member(author, "email"),
                branch,
                files,
                "author-avatar-url", json_get_string_gvariant_safe(author, "avatar_url"),
                "pusher-avatar-url", json_get_string_gvariant_safe(root, "user_avatar"),
                NULL);

            g_free(files);
        }
        g_list_free(commit_list);
    }

send_push:
    base->url = diff_url;
    git_eventc_send_push(base,
        json_object_get_string_member(root, "user_name"),
        json_object_get_string_member(root, "user_username"),
        _git_eventc_webhook_gitlab_get_email(root, "user_email"),
        branch,
        "pusher-avatar-url", json_get_string_gvariant_safe(root, "user_avatar"),
        NULL);
}

void
git_eventc_webhook_payload_parse_gitlab_tag(GitEventcEventBase *base, JsonObject *root)
{
    const gchar *tag = json_object_get_string_member(root, "ref") + strlen("refs/tags/");

    JsonObject *repository = json_object_get_object_member(root, "project");

    base->repository_name = json_object_get_string_member(repository, "name");
    base->repository_url = json_object_get_string_member(repository, "git_http_url");

    const gchar *web_url = json_object_get_string_member(repository, "web_url");
    const gchar *before = json_object_get_string_member(root, "before");
    const gchar *after = json_object_get_string_member(root, "after");

    gchar *url;
    url = git_eventc_get_url(g_strdup_printf("%s/tags/%s", web_url, tag));

    if ( g_strcmp0(before, "0000000000000000000000000000000000000000") != 0 )
            git_eventc_send_tag_deletion(base,
            json_object_get_string_member(root, "user_name"),
            json_object_get_string_member(root, "user_username"),
            _git_eventc_webhook_gitlab_get_email(root, "user_email"),
            tag,
            "pusher-avatar-url", json_get_string_gvariant_safe(root, "user_avatar"),
            NULL);

    if ( g_strcmp0(after, "0000000000000000000000000000000000000000") != 0 )
    {
        JsonArray *tags = _git_eventc_webhook_gitlab_get_tags(base, repository);
        guint length = json_array_get_length(tags);
        const gchar *previous_tag = NULL;

        if ( length > 1 )
            previous_tag = json_object_get_string_member(json_array_get_object_element(tags, 1), "name");

        base->url = g_strdup(url);
        git_eventc_send_tag_creation(base,
            json_object_get_string_member(root, "user_name"),
            json_object_get_string_member(root, "user_username"),
            _git_eventc_webhook_gitlab_get_email(root, "user_email"),
            tag, NULL, NULL, NULL, previous_tag,
            "pusher-avatar-url", json_get_string_gvariant_safe(root, "user_avatar"),
            NULL);

        json_array_unref(tags);
    }

    base->url = url;
    git_eventc_send_push(base,
        json_object_get_string_member(root, "user_name"),
        json_object_get_string_member(root, "user_username"),
        _git_eventc_webhook_gitlab_get_email(root, "user_email"),
        NULL,
        "pusher-avatar-url", json_get_string_gvariant_safe(root, "user_avatar"),
        NULL);
}

static const gchar * const _git_eventc_webhook_gitlab_issue_action_name[] = {
    [GIT_EVENTC_BUG_REPORT_ACTION_OPENING]  = "open",
    [GIT_EVENTC_BUG_REPORT_ACTION_CLOSING]  = "close",
    [GIT_EVENTC_BUG_REPORT_ACTION_REOPENING] = "reopen",
};

void
git_eventc_webhook_payload_parse_gitlab_issue(GitEventcEventBase *base, JsonObject *root)
{
    JsonObject *issue = json_object_get_object_member(root, "object_attributes");

    if ( ! json_object_has_member(issue, "action") )
        return;

    const gchar *action_str = json_object_get_string_member(issue, "action");
    guint64 action;

    if ( ! nk_enum_parse(action_str, _git_eventc_webhook_gitlab_issue_action_name, GIT_EVENTC_BUG_REPORT_NUM_ACTION, NK_ENUM_MATCH_FLAGS_IGNORE_CASE, &action) )
        return;

    JsonObject *repository = json_object_get_object_member(root, "project");
    base->repository_name = json_object_get_string_member(repository, "name");
    base->repository_url = json_object_get_string_member(repository, "git_http_url");

    JsonObject *user = json_object_get_object_member(root, "user");
    JsonObject *author = _git_eventc_webhook_gitlab_get_user(base, repository, json_object_get_int_member(issue, "author_id"));

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

    base->url = git_eventc_get_url_const(json_object_get_string_member(issue, "url"));

    git_eventc_send_bugreport(base,
        git_eventc_bug_report_actions[action],
        json_object_get_int_member(issue, "iid"),
        json_object_get_string_member(issue, "title"),
        json_get_string_safe(author, "name"),
        json_get_string_safe(author, "username"),
        _git_eventc_webhook_gitlab_get_email(author, "email"),
        tags,
        "author-avatar-url", json_get_string_gvariant_safe(author, "avatar_url"),
        "user-name", json_get_string_gvariant_safe(user, "name"),
        "user-username", json_get_string_gvariant_safe(user, "username"),
        "user-email", _git_eventc_webhook_gitlab_get_email_gvariant(user, "email"),
        "user-avatar-url", json_get_string_gvariant_safe(user, "avatar_url"),
        NULL);
}

static const gchar * const _git_eventc_webhook_gitlab_merge_request_action_name[] = {
    [GIT_EVENTC_MERGE_REQUEST_ACTION_OPENING]  = "open",
    [GIT_EVENTC_MERGE_REQUEST_ACTION_CLOSING]  = "close",
    [GIT_EVENTC_MERGE_REQUEST_ACTION_REOPENING] = "reopen",
    [GIT_EVENTC_MERGE_REQUEST_ACTION_MERGE] = "merge",
};

void
git_eventc_webhook_payload_parse_gitlab_merge_request(GitEventcEventBase *base, JsonObject *root)
{
    JsonObject *mr = json_object_get_object_member(root, "object_attributes");
    const gchar *action_str = json_get_string_default(mr, "action", _git_eventc_webhook_gitlab_merge_request_action_name[GIT_EVENTC_MERGE_REQUEST_ACTION_OPENING]);
    guint64 action;

    if ( ! nk_enum_parse(action_str, _git_eventc_webhook_gitlab_merge_request_action_name, GIT_EVENTC_MERGE_REQUEST_NUM_ACTION, NK_ENUM_MATCH_FLAGS_IGNORE_CASE, &action) )
        return;

    JsonObject *repository = json_object_get_object_member(root, "project");
    base->repository_name = json_object_get_string_member(repository, "name");
    base->repository_url = json_object_get_string_member(repository, "git_http_url");
    const gchar *branch = json_object_get_string_member(mr, "target_branch");

    JsonObject *user = json_object_get_object_member(root, "user");
    JsonObject *author = _git_eventc_webhook_gitlab_get_user(base, repository, json_object_get_int_member(mr, "author_id"));

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

    base->url = git_eventc_get_url_const(json_object_get_string_member(mr, "url"));

    git_eventc_send_merge_request(base,
        git_eventc_merge_request_actions[action],
        json_object_get_int_member(mr, "iid"),
        json_object_get_string_member(mr, "title"),
        json_get_string_safe(author, "name"),
        json_get_string_safe(author, "username"),
        _git_eventc_webhook_gitlab_get_email(author, "email"),
        tags,
        branch,
        "author-avatar-url", json_get_string_gvariant_safe(author, "avatar_url"),
        "user-name", json_get_string_gvariant_safe(user, "name"),
        "user-username", json_get_string_gvariant_safe(user, "username"),
        "user-email", _git_eventc_webhook_gitlab_get_email_gvariant(user, "email"),
        "user-avatar-url", json_get_string_gvariant_safe(user, "avatar_url"),
        NULL);
}

static const gchar * const _git_eventc_webhook_gitlab_pipeline_state_name[] = {
    [GIT_EVENTC_CI_BUILD_ACTION_SUCCESS]  = "success",
    [GIT_EVENTC_CI_BUILD_ACTION_FAILURE]  = "failed",
    [GIT_EVENTC_CI_BUILD_ACTION_ERROR] = "error",
};

void
git_eventc_webhook_payload_parse_gitlab_pipeline(GitEventcEventBase *base, JsonObject *root)
{
    JsonObject *pipeline = json_object_get_object_member(root, "object_attributes");
    const gchar *state = json_object_get_string_member(pipeline, "status");
    guint64 action;

    if ( ! nk_enum_parse(state, _git_eventc_webhook_gitlab_pipeline_state_name, GIT_EVENTC_CI_BUILD_NUM_ACTION, NK_ENUM_MATCH_FLAGS_IGNORE_CASE, &action) )
        return;

    JsonObject *repository = json_object_get_object_member(root, "project");
    base->repository_name = json_object_get_string_member(repository, "name");
    base->repository_url = json_object_get_string_member(repository, "git_http_url");

    guint64 number = json_object_get_int_member(pipeline, "id");
    const gchar *branch = json_object_get_string_member(pipeline, "ref");
    guint64 duration = json_object_get_int_member(pipeline, "duration");

    base->url = g_strdup_printf("%s/pipelines/%"G_GINT64_FORMAT, json_object_get_string_member(repository, "web_url"), number);
    base->url = git_eventc_get_url(base->url);

    git_eventc_send_ci_build(base, git_eventc_ci_build_actions[action], number, branch, duration, NULL);
}

void
git_eventc_webhook_payload_parse_gitlab_system(GitEventcEventBase *base, JsonObject *root)
{
    const gchar *event_name = json_get_string_safe(root, "event_name");
    if ( event_name == NULL )
        event_name = json_get_string_safe(root, "event_type");

    if ( event_name == NULL )
        return;

    guint64 event_type;
    if ( ! nk_enum_parse(event_name, git_eventc_webhook_gitlab_parsers_system_events, G_N_ELEMENTS(git_eventc_webhook_gitlab_parsers_system_events), NK_ENUM_MATCH_FLAGS_NONE, &event_type) )
        return;

    git_eventc_webhook_gitlab_parsers[event_type](base, root);
}
