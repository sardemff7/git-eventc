/*
 * git-eventc-post-receive - post-receive Git hook
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

#include "config.h"

#include <string.h>
#include <stdio.h>
#include <errno.h>

#include <glib.h>
#include <glib/gstdio.h>
#include <glib-object.h>
#include <gio/gio.h>

#include "nkutils-token.h"

#include <git2.h>

#include "libgit-eventc.h"

typedef struct {
    git_repository *repository;
    const gchar *repository_name;
    gchar *repository_url;
    gchar *repository_guessed_name;
    const gchar *pusher;
    const gchar *project[2];
    gchar *project_group;
    gchar *project_name;
    NkTokenList *branch_url;
    NkTokenList *commit_url;
    NkTokenList *tag_url;
    NkTokenList *diff_url;
    gboolean branch_created_commits;
} GitEventcPostReceiveContext;

typedef enum {
    GIT_EVENTC_POST_RECEIVE_TOKEN_REPOSITORY_NAME,
    GIT_EVENTC_POST_RECEIVE_TOKEN_BRANCH,
    GIT_EVENTC_POST_RECEIVE_TOKEN_COMMIT,
    GIT_EVENTC_POST_RECEIVE_TOKEN_TAG,
    GIT_EVENTC_POST_RECEIVE_TOKEN_OLD_COMMIT,
    GIT_EVENTC_POST_RECEIVE_TOKEN_NEW_COMMIT,
} GitEventcPostReceiveFormatTokens;

typedef enum {
    GIT_EVENTC_POST_RECEIVE_FLAG_REPOSITORY_NAME = (1 << GIT_EVENTC_POST_RECEIVE_TOKEN_REPOSITORY_NAME),
    GIT_EVENTC_POST_RECEIVE_FLAG_BRANCH = (1 << GIT_EVENTC_POST_RECEIVE_TOKEN_BRANCH),
    GIT_EVENTC_POST_RECEIVE_FLAG_COMMIT = (1 << GIT_EVENTC_POST_RECEIVE_TOKEN_COMMIT),
    GIT_EVENTC_POST_RECEIVE_FLAG_TAG = (1 << GIT_EVENTC_POST_RECEIVE_TOKEN_TAG),
    GIT_EVENTC_POST_RECEIVE_FLAG_OLD_COMMIT = (1 << GIT_EVENTC_POST_RECEIVE_TOKEN_OLD_COMMIT),
    GIT_EVENTC_POST_RECEIVE_FLAG_NEW_COMMIT = (1 << GIT_EVENTC_POST_RECEIVE_TOKEN_NEW_COMMIT),
} GitEventcPostReceiveFormatFlags;

static const gchar *_git_eventc_post_receive_format_tokens[] = {
    [GIT_EVENTC_POST_RECEIVE_TOKEN_REPOSITORY_NAME] = "repository-name",
    [GIT_EVENTC_POST_RECEIVE_TOKEN_BRANCH] = "branch",
    [GIT_EVENTC_POST_RECEIVE_TOKEN_COMMIT] = "commit",
    [GIT_EVENTC_POST_RECEIVE_TOKEN_TAG] = "tag",
    [GIT_EVENTC_POST_RECEIVE_TOKEN_OLD_COMMIT] = "old-commit",
    [GIT_EVENTC_POST_RECEIVE_TOKEN_NEW_COMMIT] = "new-commit",
};

static git_diff_options _git_eventc_diff_options;
static git_diff_find_options _git_eventc_diff_find_options;

static int
_git_eventc_diff_foreach_callback(const git_diff_delta *delta, float progress, void *payload)
{
    GList **paths = payload;
    const gchar *old_path = delta->old_file.path;
    const gchar *new_path = delta->new_file.path;
    gchar *path = NULL;

    switch ( delta->status )
    {
        case GIT_DELTA_COPIED:
        case GIT_DELTA_RENAMED:
        {
            gint o;
            o = git_eventc_get_path_prefix_length(old_path, new_path, strlen(old_path));
            path = g_strdup_printf( "%.*s{%s => %s}", o, old_path, old_path + o, new_path + o);
        }
        break;
        case GIT_DELTA_UNMODIFIED:
            return 0;
        case GIT_DELTA_DELETED:
            new_path = old_path;
            /* fallthrough */
        default:
            path = g_strdup(new_path);
        break;
    }

    *paths = g_list_prepend(*paths, path);

    return 0;
}

static int
_git_eventc_tree_walk_callback(const char *root, const git_tree_entry *entry, void *payload)
{
    GList **paths = payload;

    *paths = g_list_prepend(*paths, g_strdup(git_tree_entry_name(entry)));

    return 0;
}

static gchar *
_git_eventc_commit_get_files(git_repository *repository, const git_commit *commit)
{
    int error = 0;
    gchar *files = NULL;
    git_commit *parent_commit = NULL;
    git_tree *tree = NULL, *parent_tree = NULL;
    GList *paths = NULL;

    error = git_commit_tree(&tree, commit);
    if ( error < 0 )
    {
        g_warning("Couldn't get commit tree: %s", giterr_last()->message);
        goto fail;
    }

    if ( git_commit_parentcount(commit) > 0 )
    {
        error = git_commit_parent(&parent_commit, commit, 0);
        if ( error < 0 )
        {
            g_warning("Couldn't parent commit: %s", giterr_last()->message);
            goto fail;
        }
        error = git_commit_tree(&parent_tree, parent_commit);
        if ( error < 0 )
        {
            g_warning("Couldn't parent commit tree: %s", giterr_last()->message);
            goto fail;
        }

        git_diff *diff;
        error = git_diff_tree_to_tree(&diff, repository, parent_tree, tree, &_git_eventc_diff_options);
        if ( error < 0 )
        {
            g_warning("Couldn't get the diff: %s", giterr_last()->message);
            goto fail;
        }
        error = git_diff_find_similar(diff, &_git_eventc_diff_find_options);
        if ( error < 0 )
        {
            g_warning("Couldn't find similar files: %s", giterr_last()->message);
            goto fail;
        }

        git_diff_foreach(diff, _git_eventc_diff_foreach_callback, NULL, NULL, NULL, &paths);
    }
    else
        git_tree_walk(tree, GIT_TREEWALK_PRE, _git_eventc_tree_walk_callback, &paths);

    files = git_eventc_get_files(paths);

fail:
    if ( parent_tree != NULL )
        git_tree_free(parent_tree);
    if ( tree != NULL )
        git_tree_free(tree);
    if ( parent_commit != NULL )
        git_commit_free(parent_commit);
    return files;
}

static gchar *
_git_eventc_post_receive_get_config_string(git_config *config, const gchar *name)
{
    git_config_entry *entry;
    if ( git_config_get_entry(&entry, config, name) < 0 )
        return NULL;

    gchar *value;

    value = g_strdup(entry->value);

    git_config_entry_free(entry);
    return value;
}

static NkTokenList *
_git_eventc_post_receive_get_config_url_format(git_config *config, const gchar *name, GitEventcPostReceiveFormatFlags tokens)
{
    git_config_entry *entry;
    if ( git_config_get_entry(&entry, config, name) < 0 )
        return NULL;

    gchar *string;

    string = _git_eventc_post_receive_get_config_string(config, name);
    if ( string == NULL )
        return NULL;

    NkTokenList *value;
    guint64 used_tokens;
    value = nk_token_list_parse_enum(string, _git_eventc_post_receive_format_tokens, G_N_ELEMENTS(_git_eventc_post_receive_format_tokens), &used_tokens);
    if ( value == NULL )
        return NULL;

    if ( ( used_tokens & tokens ) == used_tokens )
        return value;

    g_warning("Found unexpected tokens in URL template %s", name);
    nk_token_list_unref(value);
    return NULL;
}

typedef struct {
    GitEventcPostReceiveContext *context;
    const gchar *branch;
    const gchar *commit;
    const gchar *tag;
    const gchar *old_commit;
    const gchar *new_commit;
} GitEventcPostReceiveFormatData;

static const gchar *
_git_eventc_post_receive_url_format_replace(const gchar *token, guint64 value, const gchar *key, gint64 index, gpointer user_data)
{
    GitEventcPostReceiveFormatData *data = user_data;
    switch ( value )
    {
    case GIT_EVENTC_POST_RECEIVE_TOKEN_REPOSITORY_NAME:
        return data->context->repository_name;
    case GIT_EVENTC_POST_RECEIVE_TOKEN_BRANCH:
        return data->branch;
    case GIT_EVENTC_POST_RECEIVE_TOKEN_COMMIT:
        return data->branch;
    case GIT_EVENTC_POST_RECEIVE_TOKEN_TAG:
        return data->tag;
    case GIT_EVENTC_POST_RECEIVE_TOKEN_OLD_COMMIT:
        return data->old_commit;
    case GIT_EVENTC_POST_RECEIVE_TOKEN_NEW_COMMIT:
        return data->new_commit;
    }

    return NULL;
}

static void
_git_eventc_post_receive_init(GitEventcPostReceiveContext *context, gboolean branch_created_commits)
{
    int error;
    NkTokenList *repository_url = NULL;


    git_config *config;
    error = git_repository_config_snapshot(&config, context->repository);
    if ( error < 0 )
        g_warning("Couldn't get repository configuration: %s", giterr_last()->message);
    else
    {
        context->project_group = _git_eventc_post_receive_get_config_string(config, PACKAGE_NAME ".project-group");
        context->project_name = _git_eventc_post_receive_get_config_string(config, PACKAGE_NAME ".project");
        repository_url = _git_eventc_post_receive_get_config_url_format(config, PACKAGE_NAME ".repository-url", GIT_EVENTC_POST_RECEIVE_FLAG_REPOSITORY_NAME);
        context->branch_url = _git_eventc_post_receive_get_config_url_format(config, PACKAGE_NAME ".branch-url", GIT_EVENTC_POST_RECEIVE_FLAG_REPOSITORY_NAME | GIT_EVENTC_POST_RECEIVE_FLAG_BRANCH);
        context->tag_url = _git_eventc_post_receive_get_config_url_format(config, PACKAGE_NAME ".tag-url", GIT_EVENTC_POST_RECEIVE_FLAG_REPOSITORY_NAME | GIT_EVENTC_POST_RECEIVE_FLAG_TAG);
        context->commit_url = _git_eventc_post_receive_get_config_url_format(config, PACKAGE_NAME ".commit-url", GIT_EVENTC_POST_RECEIVE_FLAG_REPOSITORY_NAME | GIT_EVENTC_POST_RECEIVE_FLAG_COMMIT);
        context->diff_url = _git_eventc_post_receive_get_config_url_format(config, PACKAGE_NAME ".diff-url", GIT_EVENTC_POST_RECEIVE_FLAG_REPOSITORY_NAME | GIT_EVENTC_POST_RECEIVE_FLAG_OLD_COMMIT | GIT_EVENTC_POST_RECEIVE_FLAG_NEW_COMMIT);
        context->repository_name = _git_eventc_post_receive_get_config_string(config, PACKAGE_NAME ".repository");

        git_config_free(config);
    }
    context->project[0] = context->project_group;
    context->project[1] = context->project_name;

    /* Use Gitolite env */
    context->pusher = g_getenv("GL_USER");
    if ( context->repository_name == NULL )
        context->repository_name = g_getenv("GL_REPO");

    if ( context->pusher == NULL )
        context->pusher = "Jane Doe";
    if ( context->repository_name == NULL )
    {
        const gchar *path = git_repository_path(context->repository);
        if ( git_repository_is_bare(context->repository) )
            context->repository_guessed_name = g_path_get_basename(path);
        else
        {
            gsize last = strlen(path) - strlen("/.git/");
            gchar *tmp;
            tmp = g_utf8_strrchr(path, last, G_DIR_SEPARATOR);
            context->repository_guessed_name = g_utf8_substring(path, (tmp+1 - path), last);
        }
        context->repository_name = context->repository_guessed_name;
    }

    if ( repository_url != NULL )
    {
        GitEventcPostReceiveFormatData data = {
            .context = context,
        };
        context->repository_url = nk_token_list_replace(repository_url, _git_eventc_post_receive_url_format_replace, &data);
        nk_token_list_unref(repository_url);
    }

    context->branch_created_commits = branch_created_commits;
}

static void
_git_eventc_post_receive_clean(GitEventcPostReceiveContext *context)
{
    g_free(context->repository_guessed_name);
    if ( context->diff_url != NULL )
        nk_token_list_unref(context->diff_url);
    if ( context->tag_url != NULL )
        nk_token_list_unref(context->tag_url);
    if ( context->commit_url != NULL )
        nk_token_list_unref(context->commit_url);
    if ( context->branch_url != NULL )
        nk_token_list_unref(context->branch_url);
    g_free(context->project_name);
    g_free(context->project_group);
}

static void
_git_eventc_post_receive_branch(GitEventcPostReceiveContext *context, const gchar *ref_name, const gchar *branch, const gchar *before, const git_oid *from, const gchar *after, const git_oid *to)
{
    int error;
    git_revwalk *walker = NULL;
    gchar *diff_url = NULL;

    if ( git_oid_iszero(from) )
    {
        gchar *branch_url = NULL;
        if ( context->branch_url != NULL )
        {
            GitEventcPostReceiveFormatData data = {
                .context = context,
                .branch = branch,
            };
            branch_url = git_eventc_get_url(nk_token_list_replace(context->branch_url, _git_eventc_post_receive_url_format_replace, &data));
        }
        git_eventc_send_branch_created(context->pusher, branch_url, context->repository_name, context->repository_url, branch, context->project);

        if ( ! context->branch_created_commits )
            goto send_push;
    }
    else if ( git_oid_iszero(to) )
    {
        git_eventc_send_branch_deleted(context->pusher, context->repository_name, context->repository_url, branch, context->project);
        goto send_push;
    }

    error = git_revwalk_new(&walker, context->repository);
    if ( error < 0 )
    {
        g_warning("Couldn't initialize revision walker: %s", giterr_last()->message);
        goto cleanup;
    }

    error = git_revwalk_push(walker, to);
    if ( error < 0 )
    {
        g_warning("Couldn't push the revision list head: %s", giterr_last()->message);
        goto cleanup;
    }
    if ( ! git_oid_iszero(from) )
    {
        error = git_revwalk_hide(walker, from);
        if ( error < 0 )
        {
            g_warning("Couldn't hide the revision list queue: %s", giterr_last()->message);
            goto cleanup;
        }
    }
    git_oid id;
    guint size = 0;
    while ( ( error = git_revwalk_next(&id, walker) ) == 0 )
        ++size;
    if ( error != GIT_ITEROVER )
    {
        g_warning("Couldn't walk the revision list: %s", giterr_last()->message);
        goto send_push;
    }

    if ( context->diff_url != NULL )
    {
        GitEventcPostReceiveFormatData data = {
            .context = context,
            .old_commit = before,
            .new_commit = after,
        };
        diff_url = git_eventc_get_url(nk_token_list_replace(context->diff_url, _git_eventc_post_receive_url_format_replace, &data));
    }

    if ( git_eventc_is_above_threshold(size) )
        git_eventc_send_commit_group(context->pusher, size, g_strdup(diff_url), context->repository_name, context->repository_url, branch, context->project);
    else
    {
        git_revwalk_sorting(walker, GIT_SORT_TOPOLOGICAL | GIT_SORT_REVERSE);
        error = git_revwalk_push(walker, to);
        if ( error < 0 )
        {
            g_warning("Couldn't push the revision list head: %s", giterr_last()->message);
            goto cleanup;
        }
        if ( ! git_oid_iszero(from) )
        {
            error = git_revwalk_hide(walker, from);
            if ( error < 0 )
            {
                g_warning("Couldn't hide the revision list queue: %s", giterr_last()->message);
                goto cleanup;
            }
        }
        char idstr[GIT_OID_HEXSZ+1];
        while ( ( error = git_revwalk_next(&id, walker) ) == 0 )
        {
            git_commit *commit;
            git_commit_lookup(&commit, context->repository, &id);

            const git_signature *author = git_commit_author(commit);
            gchar *commit_url = NULL;

            git_oid_tostr(idstr, sizeof(idstr), git_commit_id(commit));
            if ( context->commit_url != NULL )
            {
                GitEventcPostReceiveFormatData data = {
                    .context = context,
                    .commit = idstr,
                };
                commit_url = git_eventc_get_url(nk_token_list_replace(context->commit_url, _git_eventc_post_receive_url_format_replace, &data));
            }
            gchar *files;
            files = _git_eventc_commit_get_files(context->repository, commit);

            git_eventc_send_commit(idstr, git_commit_message(commit), commit_url, context->pusher, author->name, NULL, author->email, context->repository_name, context->repository_url, branch, files, context->project);

            g_free(files);
            git_commit_free(commit);
        }
        if ( error != GIT_ITEROVER )
            g_warning("Couldn't walk the revision list: %s", giterr_last()->message);
    }

send_push:
    git_eventc_send_push(diff_url, context->pusher, context->repository_name, context->repository_url, branch, context->project);
    diff_url = NULL;

cleanup:
    g_free(diff_url);
    if ( walker != NULL )
        git_revwalk_free(walker);
}

typedef struct {
    GitEventcPostReceiveContext *context;
    git_oid *id;
    const char *name;
} GitEventcPostReceiveTagSearch;

static int
_git_eventc_post_receive_is_tag(const char *name, git_oid *tag_id, void *payload)
{
    GitEventcPostReceiveTagSearch *search = payload;
    int error;
    git_tag *tag;
    gboolean same;

    error = git_tag_lookup(&tag, search->context->repository, tag_id);
    if ( error == 0 )
    {
        same = git_oid_equal(git_tag_target_id(tag), search->id);
        git_tag_free(tag);
    }
    else
        same = git_oid_equal(tag_id, search->id);

    if ( ! same )
        return 0;

    search->name = name + strlen("refs/tags/");
    return 1;
}

static void
_git_eventc_post_receive_tag(GitEventcPostReceiveContext *context, const gchar *ref_name, const gchar *tag_name, const gchar *before, const git_oid *from, const gchar *after, const git_oid *to)
{
    int error;
    gchar *url = NULL;

    if ( ! git_oid_iszero(from) )
        git_eventc_send_tag_deleted(context->pusher, context->repository_name, context->repository_url, tag_name, context->project);

    if ( ! git_oid_iszero(to) )
    {
        git_commit *commit;
        git_tag *tag;
        const gchar *previous_tag_name = NULL;

        if ( context->tag_url != NULL )
        {
            GitEventcPostReceiveFormatData data = {
                .context = context,
                .tag = tag_name,
            };
            url = git_eventc_get_url(nk_token_list_replace(context->tag_url, _git_eventc_post_receive_url_format_replace, &data));
        }

        error = git_tag_lookup(&tag, context->repository, to);
        if ( error < 0 )
            error = git_commit_lookup(&commit, context->repository, to);
        else
        {
            error = git_commit_lookup(&commit, context->repository, git_tag_target_id(tag));
            git_tag_free(tag);
        }
        if ( error < 0 )
            g_warning("Couldn't find tag commit: %s", giterr_last()->message);
        else if ( git_commit_parentcount(commit) > 0 )
        {
            git_revwalk *walker;
            error = git_revwalk_new(&walker, context->repository);
            if ( error < 0 )
            {
                g_warning("Couldn't initialize revision walker: %s", giterr_last()->message);
                goto send_push;
            }
            git_revwalk_sorting(walker, GIT_SORT_TOPOLOGICAL);

            git_commit *parent;
            error = git_commit_parent(&parent, commit, 0);
            if ( error < 0 )
            {
                g_warning("Couldn't get tag commit parent: %s", giterr_last()->message);
                goto send_push;
            }
            error = git_revwalk_push(walker, git_commit_id(parent));
            if ( error < 0 )
            {
                g_warning("Couldn't push the revision list: %s", giterr_last()->message);
                goto send_push;
            }

            git_oid id;
            while ( ( error = git_revwalk_next(&id, walker) ) != GIT_ITEROVER )
            {
                if ( error < 0 )
                {
                    g_warning("Couldn't walk the revision list: %s", giterr_last()->message);
                    break;
                }

                GitEventcPostReceiveTagSearch search = {
                    .context = context,
                    .id = &id,
                };
                error = git_tag_foreach(context->repository, _git_eventc_post_receive_is_tag, &search);
                if ( error < 0 )
                {
                    g_warning("Couldn't walk the tag list: %s", giterr_last()->message);
                    break;
                }
                if ( error )
                {
                    previous_tag_name = search.name;
                    break;
                }
            }
            git_commit_free(commit);
        }

        git_eventc_send_tag_created(context->pusher, g_strdup(url), context->repository_name, context->repository_url, tag_name, previous_tag_name, context->project);
    }

send_push:
    git_eventc_send_push(url, context->pusher, context->repository_name, context->repository_url, NULL, context->project);
}

static void
_git_eventc_post_receive(GitEventcPostReceiveContext *context, const gchar *before, const gchar *after, const gchar *ref_name)
{
    git_oid from, to;
    git_oid_fromstr(&from, before);
    git_oid_fromstr(&to, after);

    if ( g_str_has_prefix(ref_name, "refs/heads/") )
        _git_eventc_post_receive_branch(context, ref_name, ref_name + strlen("refs/heads/"), before, &from, after, &to);
    else if ( g_str_has_prefix(ref_name, "refs/tags/") )
        _git_eventc_post_receive_tag(context, ref_name, ref_name + strlen("refs/tags/"), before, &from, after, &to);
}

static gboolean
_git_eventc_parse_percent_arg(const gchar *option_name, const gchar *value, guint8 *ret, GError **error)
{
    gint16 v = -1;
    if ( value == NULL )
    {
        *ret = 50;
        return TRUE;
    }

    switch ( strlen(value) )
    {
    case 4: /* strlen("100%") */
        if ( g_strcmp0(value, "100%") == 0 )
            v = 100;
    break;
    case 3: /* strlen("10%") */
        if ( value[2] != '%' )
            break;
        /* fallthrough */
    case 2: /* strlen("1%") || strlen("10") */
        if ( value[1] == '%' )
            v = g_ascii_digit_value(value[0]);
        else if ( g_ascii_isdigit(value[0]) )
            v = g_ascii_digit_value(value[0]) * 10 + g_ascii_digit_value(value[1]);
    break;
    case 1: /* strlen("1") */
        v = g_ascii_digit_value(value[0]) * 10;
    break;
    }
    if ( v < 0 )
    {
        g_set_error(error, G_OPTION_ERROR, G_OPTION_ERROR_BAD_VALUE, "'%s' requires the same value format as 'git diff %s'", option_name, option_name);
        return FALSE;
    }

    *ret = v;
    return TRUE;
}

static gboolean
_git_eventc_find_renames(const gchar *option_name, const gchar *value, gpointer data, GError **error)
{
    guint8 v;
    if ( ! _git_eventc_parse_percent_arg(option_name, value, &v, error) )
        return FALSE;
    _git_eventc_diff_find_options.flags |= GIT_DIFF_FIND_RENAMES;
    _git_eventc_diff_find_options.rename_threshold = v;
    return TRUE;
}

static gboolean
_git_eventc_find_copies(const gchar *option_name, const gchar *value, gpointer data, GError **error)
{
    guint8 v;
    if ( ! _git_eventc_parse_percent_arg(option_name, value, &v, error) )
        return FALSE;
    _git_eventc_diff_find_options.flags |= GIT_DIFF_FIND_COPIES;
    _git_eventc_diff_find_options.copy_threshold = v;
    return TRUE;
}

static gboolean
_git_eventc_post_receive_disconnect_idle(gpointer user_data)
{
    git_eventc_disconnect();
    return G_SOURCE_REMOVE;
}

int
main(int argc, char *argv[])
{
    GError *error = NULL;
    gchar *input = NULL;
    gsize length;
    gboolean print_version;
    gboolean should_fork = FALSE;
    gboolean branch_created_commits = TRUE;

    int retval = 1;

    if ( git_libgit2_init() < 0 )
        return retval;
    if ( git_diff_init_options(&_git_eventc_diff_options, GIT_DIFF_OPTIONS_VERSION) < 0 )
        return retval;
    if ( git_diff_find_init_options(&_git_eventc_diff_find_options, GIT_DIFF_FIND_OPTIONS_VERSION) < 0 )
        return retval;

    GOptionEntry entries[] =
    {
        { "find-renames",             'M', G_OPTION_FLAG_OPTIONAL_ARG, G_OPTION_ARG_CALLBACK, &_git_eventc_find_renames, "See 'git help diff'", "<n>" },
        { "find-copies",              'C', G_OPTION_FLAG_OPTIONAL_ARG, G_OPTION_ARG_CALLBACK, &_git_eventc_find_copies,  "See 'git help diff'", "<n>" },
        { "fork",                     'F', G_OPTION_FLAG_NONE,         G_OPTION_ARG_NONE,     &should_fork,              "If git-eventc-post-receive should fork", NULL },
        { "branch-create-no-commits", 'B', G_OPTION_FLAG_REVERSE,      G_OPTION_ARG_NONE,     &branch_created_commits,   "Do not send commit/commit-group events for new branches", NULL },
        { NULL }
    };

    if ( ! git_eventc_parse_options(&argc, &argv, "post-receive", entries, "- Git hook to eventd gateway", NULL, &print_version) )
        goto end;
    if ( print_version )
    {
        g_print(PACKAGE_NAME "-post-receive " PACKAGE_VERSION "\n");
        goto end;
    }

    retval = 0;

    GIOChannel *in;

    in = g_io_channel_unix_new(0);
    if ( g_io_channel_read_to_end(in, &input, &length, &error) != G_IO_STATUS_NORMAL )
    {
        g_warning("Could not read input: %s", error->message);
        g_clear_error(&error);
        g_io_channel_unref(in);
        retval = 3;
        goto end;
    }
    g_io_channel_unref(in);

    /* We read what we needed, it’s time to fork to do our blocking operations */
    if ( should_fork )
    switch ( fork() )
    {
    case 0:
        g_close(0, NULL);
        g_close(1, NULL);
        g_close(2, NULL);
    break;
    case -1:
        g_warning("Error while forking: %s", g_strerror(errno));
        retval = 4;
    default:
        goto end;
    }

    GMainLoop *loop;

    loop = g_main_loop_new(NULL, FALSE);
    if ( git_eventc_init(loop, &retval) )
    {
        int error;
        git_repository *repository = NULL;

        error = git_repository_open(&repository, ".");
        if ( error < 0 )
        {
            g_warning("Couldn't open repository: %s", giterr_last()->message);
            retval = 3;
        }
        else
        {
            GitEventcPostReceiveContext context = { .repository = repository };
            _git_eventc_post_receive_init(&context, branch_created_commits);
            /* Set some diff options */
            _git_eventc_diff_options.flags |= GIT_DIFF_INCLUDE_TYPECHANGE;

            gchar *w = input, *n;
            while ( ( n = g_utf8_strchr(w, length - ( w - input ), '\n') ) != NULL )
            {
                *n = '\0';
                const gchar *before = w, *after, *ref;
                gchar *s;

                s = g_utf8_strchr(before, n - before, ' ');
                if ( s == NULL )
                    /* Malformed line */
                    continue;
                *s = '\0';
                after = ++s;

                s = g_utf8_strchr(after, n - after, ' ');
                if ( s == NULL )
                    /* Malformed line */
                    continue;
                *s = '\0';
                ref = ++s;

                if ( ! g_str_has_prefix(ref, "refs/") )
                    /* Malformed line */
                    continue;

                _git_eventc_post_receive(&context, before, after, ref);
            }
            g_idle_add(_git_eventc_post_receive_disconnect_idle, NULL);
            g_main_loop_run(loop);
            _git_eventc_post_receive_clean(&context);
        }
        giterr_clear();
    }
    else
        retval = 2;
    g_main_loop_unref(loop);

end:
    git_eventc_uninit();
    git_libgit2_shutdown();
    g_free(input);

    return retval;
}
