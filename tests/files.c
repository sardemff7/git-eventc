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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif /* HAVE_CONFIG_H */

#include <glib.h>

#include <libgit-eventc.h>

/* The maximum number of paths tested */
#define MAX_PATHS 3

static GList *
_test_strv_to_glist(const gchar * const *paths)
{
    GList *list = NULL;
    const gchar * const *path;
    for ( path = paths ; *path != NULL ; ++path )
        list = g_list_prepend(list, (gpointer) *path);
    return g_list_reverse(list);
}

static const struct {
    const gchar *testpath;
    const gchar * const paths[MAX_PATHS + 2 /* needle and NULL */];
} _test_list[] = {
    {
        .testpath = "/path-prefix/root-only",
        .paths  = {
            "data/mylib.pc include/mylib.h src/mylib.c",
            "data/mylib.pc",
            "include/mylib.h",
            "src/mylib.c",
            NULL
        },
    },
    {
        .testpath = "/path-prefix/sub-path",
        .paths  = {
            "src/ lib/main.c app/main.c",
            "src/lib/main.c",
            "src/app/main.c",
            NULL
        },
    },
    {
        .testpath = "/path-prefix/similar-file-names",
        .paths  = {
            "src/lib/ main.c main.h",
            "src/lib/main.c",
            "src/lib/main.h",
            NULL
        },
    },
};

static void
_test_path_list(gconstpointer user_data)
{
    const gchar * const *paths = user_data;
    const gchar *needle = *paths++;
    GList *list = _test_strv_to_glist(paths);
    gchar *files;
    files = git_eventc_get_files(list);
    g_list_free(list);
    g_assert_cmpstr(files, ==, needle);
    g_free(files);
}

int
main(int argc, char *argv[])
{
    g_test_init(&argc, &argv, NULL);

    gsize i;
    for ( i = 0 ; i < G_N_ELEMENTS(_test_list) ; ++i )
        g_test_add_data_func(_test_list[i].testpath, _test_list[i].paths, _test_path_list);

    return g_test_run();
}
