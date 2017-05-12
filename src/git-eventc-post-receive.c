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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif /* HAVE_CONFIG_H */

#ifdef HAVE_STRING_H
#include <string.h>
#endif /* HAVE_STRING_H */

#ifdef HAVE_STDIO_H
#include <stdio.h>
#endif /* HAVE_STDIO_H */

#include <glib.h>
#include <glib-object.h>
#include <gio/gio.h>

#include <git2.h>

#include "libgit-eventc.h"

typedef struct {
    git_repository *repository;
    const gchar *repository_name;
    gchar *repository_url;
    gchar *repository_guessed_name;
    const gchar *pusher;
    const char *project[2];
    const char *branch_url;
    const char *commit_url;
    const char *tag_url;
    const char *diff_url;
} GitEventcPostReceiveContext;

static git_diff_options _git_eventc_diff_options = GIT_DIFF_OPTIONS_INIT;
static git_diff_find_options _git_eventc_diff_find_options = GIT_DIFF_FIND_OPTIONS_INIT;

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

static void
_git_eventc_post_receive_init(GitEventcPostReceiveContext *context, git_repository *repository)
{
    int error;
    const gchar *repository_url = NULL;

    context->repository = repository;

    /* Use Gitolite env */
    context->pusher = g_getenv("GL_USER");
    context->repository_name = g_getenv("GL_REPO");
    context->repository_guessed_name = NULL;

    context->project[0] = NULL;
    context->project[1] = NULL;
    context->branch_url = NULL;
    context->commit_url = NULL;
    context->tag_url = NULL;
    context->diff_url = NULL;

    git_config *config;
    error = git_repository_config(&config, context->repository);
    if ( error < 0 )
        g_warning("Couldn't get repository configuration: %s", giterr_last()->message);
    else
    {
        git_config_get_string(&context->project[0], config, PACKAGE_NAME ".project-group");
        git_config_get_string(&context->project[1], config, PACKAGE_NAME ".project");
        git_config_get_string(&repository_url, config, PACKAGE_NAME ".repository-url");
        git_config_get_string(&context->branch_url, config, PACKAGE_NAME ".branch-url");
        git_config_get_string(&context->tag_url, config, PACKAGE_NAME ".tag-url");
        git_config_get_string(&context->commit_url, config, PACKAGE_NAME ".commit-url");
        git_config_get_string(&context->diff_url, config, PACKAGE_NAME ".diff-url");
        git_config_get_string(&context->repository_name, config, PACKAGE_NAME ".repository");
    }

    if ( context->pusher == NULL )
        context->pusher = "Jane Doe";
    if ( context->repository_name == NULL )
    {
        const gchar *path = git_repository_path(repository);
        if ( git_repository_is_bare(repository) )
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
        context->repository_url = g_strdup_printf(repository_url, context->repository_name);
}

static void
_git_eventc_post_receive_clean(GitEventcPostReceiveContext *context)
{
    g_free(context->repository_guessed_name);
}

static void
_git_eventc_post_receive_branch(GitEventcPostReceiveContext *context, const gchar *ref_name, const gchar *branch, const gchar *before, const git_oid *from, const gchar *after, const git_oid *to)
{
    int error;
    GSList *commits = NULL;
    gchar *diff_url = NULL;

    if ( git_oid_iszero(from) )
    {
        gchar *branch_url = NULL;
        if ( context->branch_url != NULL )
            branch_url = g_strdup_printf(context->branch_url, context->repository_name, branch);
        git_eventc_send_branch_created(context->pusher, branch_url, context->repository_name, context->repository_url, branch, context->project);
        g_free(branch_url);
    }
    else if ( git_oid_iszero(to) )
    {
        git_eventc_send_branch_deleted(context->pusher, context->repository_name, context->repository_url, branch, context->project);
        goto send_push;
    }

    git_revwalk *walker;
    error = git_revwalk_new(&walker, context->repository);
    if ( error < 0 )
    {
        g_warning("Couldn't initialize revision walker: %s", giterr_last()->message);
        goto cleanup;
    }
    git_revwalk_sorting(walker, GIT_SORT_TOPOLOGICAL);

    git_commit *commit;
    error = git_revwalk_push(walker, to);
    if ( error < 0 )
    {
        g_warning("Couldn't push the revision list: %s", giterr_last()->message);
        goto cleanup;
    }
    git_oid id;
    guint size = 0;
    while ( ( ( error = git_revwalk_next(&id, walker) ) != GIT_ITEROVER ) && ( ! git_oid_equal(from, &id) ) )
    {
        if ( error < 0 )
        {
            g_warning("Couldn't walk the revision list: %s", giterr_last()->message);
            goto cleanup;
        }
        git_commit_lookup(&commit, context->repository, &id);
        ++size;
        commits = g_slist_prepend(commits, commit);
    }

    if ( context->diff_url != NULL )
        diff_url = g_strdup_printf(context->diff_url, context->repository_name, before, after);

    if ( git_eventc_is_above_threshold(size) )
        git_eventc_send_commit_group(context->pusher, size, diff_url, context->repository_name, context->repository_url, branch, context->project);
    else
    {
        char id[GIT_OID_HEXSZ+1];
        GSList *commit_;
        for ( commit_ = commits ; commit_ != NULL ; commit_ = g_slist_next(commit_) )
        {
            const git_commit *commit = commit_->data;
            const git_signature *author = git_commit_author(commit);
            gchar *commit_url = NULL;

            git_oid_tostr(id, GIT_OID_HEXSZ+1, git_commit_id(commit));
            if ( context->commit_url != NULL )
                commit_url = g_strdup_printf(context->commit_url, context->repository_name, id);
            gchar *files;
            files = _git_eventc_commit_get_files(context->repository, commit);

            git_eventc_send_commit(id, git_commit_message(commit), commit_url, context->pusher, author->name, NULL, author->email, context->repository_name, context->repository_url, branch, files, context->project);

            g_free(commit_url);
            g_free(files);
        }
    }

send_push:
    git_eventc_send_push(diff_url, context->pusher, context->repository_name, context->repository_url, branch, context->project);

cleanup:
    g_free(diff_url);
    g_slist_free_full(commits, (GDestroyNotify) git_commit_free);
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

    if ( ! git_oid_equal(tag_id, search->id) )
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
        const gchar *previous_tag_name = NULL;

        if ( context->tag_url != NULL )
            url = g_strdup_printf(context->tag_url, context->repository_name, tag_name);


        error = git_commit_lookup(&commit, context->repository, to);
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
        }

        git_eventc_send_tag_created(context->pusher, url, context->repository_name, context->repository_url, tag_name, previous_tag_name, context->project);
    }

send_push:
    git_eventc_send_push(url, context->pusher, context->repository_name, context->repository_url, NULL, context->project);

    g_free(url);
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
    g_print("-M%d%%\n", v);
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
    g_print("-C%d%%\n", v);
    return TRUE;
}

int
main(int argc, char *argv[])
{
    gboolean print_version;

    int retval = 1;

    git_libgit2_init();

    GOptionEntry entries[] =
    {
        { "find-renames", 'M', G_OPTION_FLAG_OPTIONAL_ARG, G_OPTION_ARG_CALLBACK, &_git_eventc_find_renames, "See 'git help diff'", "<n>" },
        { "find-copies",  'C', G_OPTION_FLAG_OPTIONAL_ARG, G_OPTION_ARG_CALLBACK, &_git_eventc_find_copies,  "See 'git help diff'", "<n>" },
        { NULL }
    };

    if ( ! git_eventc_parse_options(&argc, &argv, entries, "- Git hook to eventd gateway", NULL, &print_version) )
        goto end;
    if ( print_version )
    {
        g_print(PACKAGE_NAME "-post-receive " PACKAGE_VERSION "\n");
        goto end;
    }

    if ( git_eventc_init(NULL, &retval) )
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
            GitEventcPostReceiveContext context;
            _git_eventc_post_receive_init(&context, repository);
            /* Set some diff options */
            _git_eventc_diff_options.flags |= GIT_DIFF_INCLUDE_TYPECHANGE;

            char *line = NULL;
            size_t len = 0;
            ssize_t r;
            while ( ( r = getline(&line, &len, stdin) ) != -1 )
            {
                const gchar *before = line, *after, *ref;
                line[r-1] = '\0';
                gchar *s;

                s = g_utf8_strchr(line, -1, ' ');
                if ( s == NULL )
                    /* Malformed line */
                    continue;
                *s = '\0';
                after = ++s;

                s = g_utf8_strchr(after, -1, ' ');
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
            free(line);
            _git_eventc_post_receive_clean(&context);
        }
        giterr_clear();
        retval = 0;
    }
    else
        retval = 2;

end:
    git_eventc_uninit();
    git_libgit2_shutdown();

    return retval;
}
