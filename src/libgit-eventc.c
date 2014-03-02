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

#ifdef HAVE_STRING_H
#include <string.h>
#endif /* HAVE_STRING_H */

#include <glib.h>
#include <glib/gstdio.h>
#include <glib-object.h>
#ifdef G_OS_UNIX
#if GLIB_CHECK_VERSION(2,32,0)
#include <glib-unix.h>
#endif /* GLIB_CHECK_VERSION(2,32,0) */
#endif /* G_OS_UNIX */

#include <libeventc.h>
#include <libsoup/soup.h>

#include "libgit-eventc.h"

typedef gchar *(*GitEventcShortenerParse)(SoupMessage *msg);

typedef struct {
    const gchar *name;
    const gchar *method;
    const gchar *url;
    const gchar *field_name;
    const gchar *prefix;
    GitEventcShortenerParse parse;
} GitEventcShortener;

static gchar *host = NULL;
static guint merge_threshold = 5;
static guint commit_id_size = 7;
static gboolean shortener = FALSE;

static EventcConnection *client = NULL;
static SoupSession *shortener_session = NULL;


void
git_eventc_parse_options(gint *argc, gchar ***argv, GOptionEntry *extra_entries, const gchar *description, gboolean *print_version)
{
    *print_version = FALSE;

    GOptionContext *option_context;
    GOptionEntry entries[] =
    {
        { "host",            'h', 0, G_OPTION_ARG_STRING,   &host,            "eventd host to connect to",                                  "<host>" },
        { "merge-threshold", 'm', 0, G_OPTION_ARG_INT,      &merge_threshold, "Number of commits to start merging (defaults to 5)",         "<threshold>" },
        { "commit-id-size",   0,  0, G_OPTION_ARG_INT,      &commit_id_size,  "Number of chars to limmit the commit id to (defaults to 7)", "<limit>" },
        { "use-shortener",   's', 0, G_OPTION_ARG_NONE,     &shortener,       "Use a URL shortener service)",                               NULL },
        { "version",         'V', 0, G_OPTION_ARG_NONE,     print_version,    "Print version",                                              NULL },
        { NULL }
    };
    GError *error = NULL;

    option_context = g_option_context_new(description);

    if ( extra_entries != NULL )
        g_option_context_add_main_entries(option_context, extra_entries, NULL);
    g_option_context_add_main_entries(option_context, entries, NULL);

    if ( ! g_option_context_parse(option_context, argc, argv, &error) )
        g_error("Option parsing failed: %s\n", error->message);
    g_option_context_free(option_context);

}

#ifdef G_OS_UNIX
#if GLIB_CHECK_VERSION(2,32,0)
static gboolean
_git_eventc_stop(gpointer user_data)
{
    g_main_loop_quit(user_data);
    return FALSE;
}
#else /* ! GLIB_CHECK_VERSION(2,32,0) */
static GMainLoop *_git_eventc_main_loop = NULL;
static void
_git_eventc_sigaction_stop(int sig, siginfo_t *info, void *data)
{
    g_main_loop_quit(_git_eventc_main_loop);
}
#endif /* ! GLIB_CHECK_VERSION(2,32,0) */
#endif /* G_OS_UNIX */

gboolean
git_eventc_init(GMainLoop *loop, gint *retval)
{
#ifdef G_OS_UNIX
#if GLIB_CHECK_VERSION(2,32,0)
    g_unix_signal_add(SIGTERM, _git_eventc_stop, loop);
    g_unix_signal_add(SIGINT, _git_eventc_stop, loop);
#else /* ! GLIB_CHECK_VERSION(2,32,0) */
    _git_eventc_main_loop = loop;
    struct sigaction action;
    action.sa_sigaction = _git_eventc_sigaction_stop;
    action.sa_flags = SA_SIGINFO;
    sigemptyset(&action.sa_mask);
    sigaction(SIGTERM, &action, NULL);
    sigaction(SIGINT, &action, NULL);
#endif /* ! GLIB_CHECK_VERSION(2,32,0) */
#endif /* G_OS_UNIX */

    GError *error = NULL;
    client = eventc_connection_new(host);
    eventc_connection_set_passive(client, TRUE);

    if ( ! eventc_connection_connect_sync(client, &error) )
    {
        g_warning("Couldn't connect to eventd: %s", error->message);
        g_error_free(error);
        g_object_unref(client);
        client = NULL;
        *retval = 1;
        return FALSE;
    }

    return TRUE;
}

void
git_eventc_uninit(void)
{
    if ( shortener_session != NULL )
        g_object_unref(shortener_session);

    if ( client != NULL )
        g_object_unref(client);

    g_free(host);
}

gboolean
git_eventc_is_above_threshold(guint size)
{
    return ( size >= merge_threshold );
}

static gchar *
_git_eventc_shortener_parse_gitio(SoupMessage *msg)
{
    if ( msg->status_code != SOUP_STATUS_CREATED )
        return NULL;

    return g_strdup(soup_message_headers_get_one(msg->response_headers, "Location"));
}

static GitEventcShortener shorteners[] = {
    {
        .name       = "git.io",
        .method     = "POST",
        .url        = "http://git.io/",
        .field_name = "url",
        .prefix     = "https://github.com/",
        .parse      = _git_eventc_shortener_parse_gitio,
    },
    {
        .name       = "tinyurl",
        .method     = "GET",
        .url        = "http://tinyurl.com/api-create.php",
        .field_name = "url",
    },
    {
        .name       = "is.gd",
        .method     = "POST",
        .url        = "http://is.gd/create.php?format=simple",
        .field_name = "url",
    },
};

static gchar *
_git_eventc_get_url(const gchar *url)
{
    if ( ( ! shortener ) || ( url == NULL ) || ( *url == '\0') )
        return g_strdup(url);

    if ( shortener_session == NULL )
        shortener_session = g_object_new(SOUP_TYPE_SESSION, SOUP_SESSION_ADD_FEATURE_BY_TYPE, SOUP_TYPE_CONTENT_DECODER, SOUP_SESSION_USER_AGENT, PACKAGE_NAME " ", NULL);

    guint i;
    SoupURI *uri;
    SoupMessage *msg;
    gchar *escaped_url;
    gchar *data;
    gchar *short_url = NULL;

    for ( i = 0 ; ( i < G_N_ELEMENTS(shorteners) ) && ( short_url == NULL ) ; ++i )
    {
        if ( ( shorteners[i].prefix != NULL ) && ( ! g_str_has_prefix(url, shorteners[i].prefix) ) )
            continue;

        uri = soup_uri_new(shorteners[i].url);
        msg = soup_message_new_from_uri(shorteners[i].method, uri);
        escaped_url = soup_uri_encode(url, NULL);
        data = g_strdup_printf("%s=%s", shorteners[i].field_name, escaped_url);
        g_free(escaped_url);
        soup_message_set_request(msg, "application/x-www-form-urlencoded", SOUP_MEMORY_TAKE, data, strlen(data));
        soup_session_send_message(shortener_session, msg);

        if ( shorteners[i].parse != NULL )
            short_url = shorteners[i].parse(msg);
        else if ( SOUP_STATUS_IS_SUCCESSFUL(msg->status_code) )
            short_url = g_strndup(msg->response_body->data, msg->response_body->length);

        soup_uri_free(uri);
        g_object_unref(msg);
    }

    if ( short_url == NULL )
    {
        g_warning("Failed to shorten URL '%s'", url);
        short_url = g_strdup(url);
    }

    return short_url;
}

void
git_eventc_send_commit_group(const gchar *pusher_name, guint size, const gchar *url, const gchar *repository_name, const gchar *branch, const gchar *project)
{
    EventdEvent *event;

    event = eventd_event_new("scm", "commit-group");

    eventd_event_add_data(event, g_strdup("pusher-name"), g_strdup(pusher_name));
    eventd_event_add_data(event, g_strdup("size"), g_strdup_printf("%u", size));
    eventd_event_add_data(event, g_strdup("url"), _git_eventc_get_url(url));

    eventd_event_add_data(event, g_strdup("repository-name"), g_strdup(repository_name));
    eventd_event_add_data(event, g_strdup("branch"), g_strdup(branch));

    if ( project != NULL )
        eventd_event_add_data(event, g_strdup("project"), g_strdup(project));

    eventc_connection_event(client, event, NULL);
    g_object_unref(event);
}

void
git_eventc_send_commit(const gchar *id, const gchar *base_message, const gchar *url, const gchar *author_name, const gchar *author_username, const gchar *author_email, const gchar *repository_name, const gchar *branch, const gchar *project)
{
#ifdef DEBUG
    g_debug("Send commit:"
        "\nID: %s"
        "\nMessage: %s"
        "\nURL: %s"
        "\nAuthor name: %s"
        "\nAuthor username: %s"
        "\nAuthor email: %s"
        "\nRepository: %s"
        "\nBranch: %s"
        "\nProject: %s",
        id,
        base_message,
        url,
        author_name,
        author_username,
        author_email,
        repository_name,
        branch,
        project);
#endif /* DEBUG */

    const gchar *new_line = g_utf8_strchr(base_message, -1, '\n');
    gchar *message;
    if ( new_line != NULL )
        message = g_strndup(base_message, ( new_line - base_message ));
    else
        message = g_strdup(base_message);

    EventdEvent *event;

    event = eventd_event_new("scm", "commit");

    eventd_event_add_data(event, g_strdup("id"), g_strndup(id, commit_id_size));
    eventd_event_add_data(event, g_strdup("message"), message);
    eventd_event_add_data(event, g_strdup("url"), _git_eventc_get_url(url));

    eventd_event_add_data(event, g_strdup("author-name"), g_strdup(author_name));
    eventd_event_add_data(event, g_strdup("author-email"), g_strdup(author_email));
    if ( author_username != NULL )
        eventd_event_add_data(event, g_strdup("author-username"), g_strdup(author_username));

    eventd_event_add_data(event, g_strdup("repository-name"), g_strdup(repository_name));
    eventd_event_add_data(event, g_strdup("branch"), g_strdup(branch));

    if ( project != NULL )
        eventd_event_add_data(event, g_strdup("project"), g_strdup(project));

    eventc_connection_event(client, event, NULL);
    g_object_unref(event);
}
