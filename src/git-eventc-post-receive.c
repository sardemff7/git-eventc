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

static void
_git_eventc_post_receive(git_repository *repository, const gchar *before, const gchar *after, const gchar *ref)
{
    int error;

    const char *pusher = NULL;
    const char *repository_name = NULL;
    gchar *repository_guessed_name = NULL;
    const char *project = NULL;
    const char *commit_url = NULL;
    const char *diff_url = NULL;
    const gchar *branch = ref + strlen("refs/heads/");

    /* Use Gitolite env */
    pusher = g_getenv("GL_USER");
    repository_name = g_getenv("GL_REPO");

    git_config *config;
    error = git_repository_config(&config, repository);
    if ( error != 0 )
        g_warning("Couldn't get repository configuration: %s", giterr_last()->message);
    else
    {
        git_config_get_string(&project, config, PACKAGE_NAME ".project");
        git_config_get_string(&commit_url, config, PACKAGE_NAME ".commit-url");
        git_config_get_string(&diff_url, config, PACKAGE_NAME ".diff-url");
        git_config_get_string(&repository_name, config, PACKAGE_NAME ".repository");
    }

    if ( pusher == NULL )
        pusher = "Jane Doe";
    if ( repository_name == NULL )
    {
        const gchar *path = git_repository_path(repository);
        if ( git_repository_is_bare(repository) )
            repository_guessed_name = g_path_get_basename(path);
        else
        {
            gsize last = strlen(path) - strlen("/.git/");
            gchar *tmp;
            tmp = g_utf8_strrchr(path, last, G_DIR_SEPARATOR);
            repository_guessed_name = g_utf8_substring(path, (tmp+1 - path), last);
        }
        repository_name = repository_guessed_name;
    }

    git_revwalk *walker;
    error = git_revwalk_new(&walker, repository);
    if ( error != 0 )
    {
        g_warning("Couldn't initialize revision walker: %s", giterr_last()->message);
        return;
    }
    git_revwalk_sorting(walker, GIT_SORT_TOPOLOGICAL);

    git_oid from, to;
    git_oid_fromstr(&from, before);
    git_oid_fromstr(&to, after);

    git_commit *commit;
    error = git_revwalk_push(walker, &to);
    if ( error != 0 )
    {
        g_warning("Couldn't push the revision list: %s", giterr_last()->message);
        return;
    }
    guint size = 0;
    GSList *commits = NULL;
    while ( ( ( error = git_revwalk_next(&to, walker) ) != GIT_ITEROVER ) && ( ! git_oid_equal(&from, &to) ) )
    {
        if ( error != 0 )
        {
            g_warning("Couldn't walk the revision list: %s", giterr_last()->message);
            return;
        }
        git_commit_lookup(&commit, repository, &to);
        ++size;
        commits = g_slist_prepend(commits, commit);
    }

    gchar *url = NULL;
    if ( git_eventc_is_above_threshold(size) )
    {
        if ( diff_url != NULL )
            url = g_strdup_printf(diff_url, repository_name, before, after);

        git_eventc_send_commit_group(pusher, size, url, repository_name, branch, project);
    }
    else
    {
        char id[GIT_OID_HEXSZ+1];
        GSList *commit_;
        for ( commit_ = commits ; commit_ != NULL ; commit_ = g_slist_next(commit_) )
        {
            const git_commit *commit = commit_->data;
            const git_signature *author = git_commit_author(commit);

            git_oid_tostr(id, GIT_OID_HEXSZ+1, git_commit_id(commit));
            if ( commit_url != NULL )
            {
                g_free(url);
                url = g_strdup_printf(commit_url, repository_name, id);
            }

            git_eventc_send_commit(id, git_commit_message(commit), url, author->name, NULL, author->email, repository_name, branch, NULL, project);
        }
    }
    g_free(url);
    g_slist_free_full(commits, (GDestroyNotify) git_commit_free);

    g_free(repository_guessed_name);
}

int
main(int argc, char *argv[])
{
    gboolean print_version;

    int retval = 0;

#if ! GLIB_CHECK_VERSION(2,35,1)
    g_type_init();
#endif /* ! GLIB_CHECK_VERSION(2,35,1) */
    git_threads_init();

    git_eventc_parse_options(&argc, &argv, NULL, "- Git hook to eventd gateway", &print_version);
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
        if ( error != 0 )
            g_warning("Couldn't open repository: %s", giterr_last()->message);
        else
        {
            char *line = NULL;
            size_t len = 0;
            ssize_t r;
            while ( ( r = getline(&line, &len, stdin) ) != -1 )
            {
                const gchar *before = line, *after, *ref;
                line[r-1] = '\0';
                gchar *s;

                s = g_utf8_strchr(line, -1, ' ');
                *s = '\0';
                after = ++s;

                s = g_utf8_strchr(after, -1, ' ');
                *s = '\0';
                ref = ++s;

                _git_eventc_post_receive(repository, before, after, ref);
            }
            free(line);
        }
        giterr_clear();
    }

end:
    git_eventc_uninit();
    git_threads_shutdown();

    return retval;
}
