/*
 * github-eventd-gateway - Gateway to push GitHub commits to eventd
 *
 * Copyright © 2013 Quentin "Sardem FF7" Glidic
 *
 * This file is part of eventd.
 *
 * eventd is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * eventd is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with eventd. If not, see <http://www.gnu.org/licenses/>.
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
#include <json-glib/json-glib.h>

#define json_object_dup_string_member(o, m) (g_strdup(json_object_get_string_member(o, m)))

static gchar *token = NULL;
static guint merge_thresold = 5;
static guint commit_id_size = 7;

static void
_github_eventd_gateway_server_callback(SoupServer *server, SoupMessage *msg, const char *path, GHashTable *query, SoupClientContext *client, gpointer user_data)
{
    if ( msg->method != SOUP_METHOD_POST )
    {
        g_warning("Non-POST request from %s", soup_message_headers_get_one(msg->request_headers, "User-Agent"));
        soup_message_set_status(msg, SOUP_STATUS_NOT_IMPLEMENTED);
        return;
    }

    const gchar *query_token = NULL;
    if ( query != NULL )
        query_token = g_hash_table_lookup(query, "token");
    if ( ( token != NULL ) && ( ( query_token == NULL ) || ( g_strcmp0(token, query_token) != 0 ) ) )
    {
        g_warning("Unauthorized request from %s", soup_message_headers_get_one(msg->request_headers, "User-Agent"));
        soup_message_set_status(msg, SOUP_STATUS_UNAUTHORIZED);
        return;
    }

    const gchar *project = NULL;
    if ( query != NULL )
        project = g_hash_table_lookup(query, "project");

    GHashTable *data = soup_form_decode(msg->request_body->data);

    if ( data == NULL )
    {
        g_warning("Bad POST (no data) from %s", soup_message_headers_get_one(msg->request_headers, "User-Agent"));
        soup_message_set_status(msg, SOUP_STATUS_BAD_REQUEST);
        return;
    }

    const gchar *payload = g_hash_table_lookup(data, "payload");
    if ( payload == NULL )
    {
        g_warning("Bad POST (no paylaod) from %s", soup_message_headers_get_one(msg->request_headers, "User-Agent"));
        soup_message_set_status(msg, SOUP_STATUS_BAD_REQUEST);
        return;
    }
    JsonParser *parser = json_parser_new();

    json_parser_load_from_data(parser, payload, -1, NULL);
    g_hash_table_unref(data);

    JsonNode *root_node = json_parser_get_root(parser);
    JsonObject *root = json_node_get_object(root_node);

    JsonObject *repository = json_object_get_object_member(root, "repository");

    JsonArray *commits = json_object_get_array_member(root, "commits");
    GList *commit_list = json_array_get_elements(commits);
    GList *commit_;

    EventdEvent *event;

    if ( json_array_get_length(commits) >= merge_thresold )
    {
        JsonObject *pusher = json_object_get_object_member(root, "pusher");

        event = eventd_event_new("scm", "commit-group");
        eventd_event_add_data(event, g_strdup("author-name"), json_object_dup_string_member(pusher, "name"));
        //eventd_event_add_data(event, g_strdup("author-username"), json_object_dup_string_member(pusher, "username"));
        //eventd_event_add_data(event, g_strdup("author-email"), json_object_dup_string_member(pusher, "email"));
        eventd_event_add_data(event, g_strdup("size"), g_strdup_printf("%u", json_array_get_length(commits)));
        eventd_event_add_data(event, g_strdup("url"), json_object_dup_string_member(root, "compare"));

        eventd_event_add_data(event, g_strdup("repository-name"), json_object_dup_string_member(repository, "name"));
        eventd_event_add_data(event, g_strdup("branch"), g_strdup(json_object_get_string_member(root, "ref") + strlen("refs/heads/")));
        if ( project != NULL )
            eventd_event_add_data(event, g_strdup("project"), g_strdup(project));

        eventc_connection_event(user_data, event, NULL);
        g_object_unref(event);
    }
    else
    {
        for ( commit_ = commit_list ; commit_ != NULL ; commit_ = g_list_next(commit_) )
        {
            JsonObject *commit = json_node_get_object(commit_->data);
            JsonObject *author = json_object_get_object_member(commit, "author");

            const gchar *base_message = json_object_get_string_member(commit, "message");
            const gchar *new_line = g_utf8_strchr(base_message, -1, '\n');
            gchar *message;
            if ( new_line != NULL )
                message = g_strndup(base_message, ( new_line - base_message ));
            else
                message = g_strdup(base_message);

            event = eventd_event_new("scm", "commit");
            eventd_event_add_data(event, g_strdup("id"), g_strndup(json_object_get_string_member(commit, "id"), commit_id_size));
            eventd_event_add_data(event, g_strdup("author-name"), json_object_dup_string_member(author, "name"));
            eventd_event_add_data(event, g_strdup("author-username"), json_object_dup_string_member(author, "username"));
            eventd_event_add_data(event, g_strdup("author-email"), json_object_dup_string_member(author, "email"));
            eventd_event_add_data(event, g_strdup("message"), message);
            eventd_event_add_data(event, g_strdup("url"), json_object_dup_string_member(commit, "url"));

            eventd_event_add_data(event, g_strdup("repository-name"), json_object_dup_string_member(repository, "name"));
            eventd_event_add_data(event, g_strdup("branch"), g_strdup(json_object_get_string_member(root, "ref") + strlen("refs/heads/")));
            if ( project != NULL )
                eventd_event_add_data(event, g_strdup("project"), g_strdup(project));

            eventc_connection_event(user_data, event, NULL);
            g_object_unref(event);
        }
    }

    g_object_unref(parser);


    soup_message_set_status(msg, SOUP_STATUS_OK);
}

#ifdef G_OS_UNIX
#if GLIB_CHECK_VERSION(2,32,0)
static gboolean
_eventd_core_stop(gpointer user_data)
{
    g_main_loop_quit(user_data);
    return FALSE;
}
#else /* ! GLIB_CHECK_VERSION(2,32,0) */
static GMainLoop *_github_eventd_gateway_main_loop = NULL;
static void
_eventd_core_sigaction_stop(int sig, siginfo_t *info, void *data)
{
    g_main_loop_quit(_github_eventd_gateway_main_loop);
}
#endif /* ! GLIB_CHECK_VERSION(2,32,0) */
#endif /* G_OS_UNIX */

int
main(int argc, char *argv[])
{
    gchar *cert_file = NULL;
    gchar *key_file = NULL;
    gint port = 0;
    gchar *host = NULL;
    gboolean print_version = FALSE;

    int retval = 0;
    GError *error = NULL;
    GOptionContext *option_context = NULL;

#if ! GLIB_CHECK_VERSION(2,35,1)
    g_type_init();
#endif /* ! GLIB_CHECK_VERSION(2,35,1) */

    GOptionEntry entries[] =
    {
        { "port",           'p', 0,                    G_OPTION_ARG_INT,      &port,                      "Port to listen to",                            "<port>" },
        { "token",          't', 0,                    G_OPTION_ARG_STRING,   &token,                     "Token to check in the client query",           "<token>" },
        { "cert-file",      'c', 0,                    G_OPTION_ARG_FILENAME, &cert_file,                 "Path to the certificate file",                 "<path>" },
        { "key-file",       'k', 0,                    G_OPTION_ARG_FILENAME, &key_file,                  "Path to the key file (defaults to cert-file)", "<path>" },
        { "host",           'h', 0,                    G_OPTION_ARG_STRING,   &host,                      "eventd host to connect to",                    "<host>" },
        { "merge-thresold", 'm', 0,                    G_OPTION_ARG_INT,      &merge_thresold,            "Number of commits to start merging",           "<thresold>" },
        { "commit-id-size",  0,  0,                    G_OPTION_ARG_INT,      &commit_id_size,            "Number of chars to limmit the commit id to",   "<limit>" },
        { "version",        'V', 0,                    G_OPTION_ARG_NONE,     &print_version,             "Print version",                                NULL },
        { NULL }
    };

    option_context = g_option_context_new("- GitHub WebHook to eventd gateway");

    g_option_context_add_main_entries(option_context, entries, NULL);

    if ( ! g_option_context_parse(option_context, &argc, &argv, &error) )
        g_error("Option parsing failed: %s\n", error->message);
    g_option_context_free(option_context);

    if ( print_version )
    {
        g_print(PACKAGE_NAME " " PACKAGE_VERSION "\n");
        goto end;
    }

    GMainLoop *loop;
    loop = g_main_loop_new(NULL, FALSE);

#ifdef G_OS_UNIX
#if GLIB_CHECK_VERSION(2,32,0)
    g_unix_signal_add(SIGTERM, _eventd_core_stop, loop);
    g_unix_signal_add(SIGINT, _eventd_core_stop, loop);
#else /* ! GLIB_CHECK_VERSION(2,32,0) */
    _github_eventd_gateway_main_loop = loop;
    struct sigaction action;
    action.sa_sigaction = _eventd_core_sigaction_stop;
    action.sa_flags = SA_SIGINFO;
    sigemptyset(&action.sa_mask);
    sigaction(SIGTERM, &action, NULL);
    sigaction(SIGINT, &action, NULL);
#endif /* ! GLIB_CHECK_VERSION(2,32,0) */
#endif /* G_OS_UNIX */

    EventcConnection *client;
    client = eventc_connection_new(host);
    eventc_connection_set_passive(client, TRUE);

    if ( ! eventc_connection_connect_sync(client, &error) )
    {
        g_warning("Couldn't connect to eventd: %s", error->message);
        g_error_free(error);
        retval = 1;
    }
    else
    {

        SoupServer *server;
        server = soup_server_new(SOUP_SERVER_PORT, port, SOUP_SERVER_SSL_CERT_FILE, cert_file, SOUP_SERVER_SSL_KEY_FILE, ( ( key_file == NULL ) ? cert_file : key_file ), NULL);
        if ( server == NULL )
        {
            g_warning("Couldn't create the server");
        }
        else
        {
            soup_server_add_handler(server, NULL, _github_eventd_gateway_server_callback, client, NULL);
            soup_server_run_async(server);

            g_main_loop_run(loop);
            g_main_loop_unref(loop);

            soup_server_quit(server);
        }
    }

    g_object_unref(client);

end:
    g_free(host);
    g_free(key_file);
    g_free(cert_file);

    return retval;
}
