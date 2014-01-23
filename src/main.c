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

#define DEFAULT_SHORTENER_URL ""

#define json_object_dup_string_member(o, m) (g_strdup(json_object_get_string_member(o, m)))

typedef enum {
    GITHUB_EVENTC_SHORTENERS_NONE = 0,
    GITHUB_EVENTC_SHORTENERS_GITIO = 1,
    GITHUB_EVENTC_SHORTENERS_TINYURL,
    GITHUB_EVENTC_SHORTENERS_ISGD,
    GITHUB_EVENTC_SHORTENERS_SIZE
} GithubEventcShorteners;

typedef gchar *(*GithubEventcShortenerParse)(SoupMessage *msg);

typedef struct {
    const gchar *name;
    const gchar *method;
    const gchar *url;
    const gchar *field_name;
    GithubEventcShortenerParse parse;
} GithubEventcShortener;

static gchar *token = NULL;
static guint merge_thresold = 5;
static guint commit_id_size = 7;
static GithubEventcShortener *shortener = NULL;
static SoupSession *shortener_session = NULL;


static gchar *
_github_eventc_shortener_parse_gitio(SoupMessage *msg)
{
    if ( msg->status_code != SOUP_STATUS_CREATED )
        return NULL;

    return g_strdup(soup_message_headers_get_one(msg->response_headers, "Location"));
}

static GithubEventcShortener shorteners[GITHUB_EVENTC_SHORTENERS_SIZE] = {
    [GITHUB_EVENTC_SHORTENERS_GITIO] = {
        .name       = "git.io",
        .method     = "POST",
        .url        = "http://git.io/",
        .field_name = "url",
        .parse      = _github_eventc_shortener_parse_gitio,
    },
    [GITHUB_EVENTC_SHORTENERS_TINYURL] = {
        .name       = "tinyurl",
        .method     = "GET",
        .url        = "http://tinyurl.com/api-create.php",
        .field_name = "url",
    },
    [GITHUB_EVENTC_SHORTENERS_ISGD] = {
        .name       = "is.gd",
        .method     = "POST",
        .url        = "http://is.gd/create.php?format=simple",
        .field_name = "url",
    },
};

static gchar *
_github_eventc_get_url(const gchar *url)
{
    if ( shortener_session == NULL )
        return g_strdup(url);

    SoupURI *uri;
    SoupMessage *msg;
    gchar *escaped_url;
    gchar *data;
    gchar *short_url = NULL;

    uri = soup_uri_new(shortener->url);
    msg = soup_message_new_from_uri(shortener->method, uri);
    escaped_url = soup_uri_encode(url, NULL);
    data = g_strdup_printf("%s=%s", shortener->field_name, escaped_url);
    g_free(escaped_url);
    soup_message_set_request(msg, "application/x-www-form-urlencoded", SOUP_MEMORY_TAKE, data, strlen(data));
    soup_session_send_message(shortener_session, msg);

    if ( shortener->parse != NULL )
        short_url = shortener->parse(msg);
    else if ( SOUP_STATUS_IS_SUCCESSFUL(msg->status_code) )
        short_url = g_strndup(msg->response_body->data, msg->response_body->length);

    if ( short_url == NULL )
    {
        g_warning("Failed to shorten URL '%s'", url);
        short_url = g_strdup(url);
    }

    soup_uri_free(uri);
    g_object_unref(msg);

    return short_url;
}

static void
_github_eventc_gateway_server_callback(SoupServer *server, SoupMessage *msg, const char *path, GHashTable *query, SoupClientContext *client, gpointer connection)
{
    if ( msg->method != SOUP_METHOD_POST )
    {
        g_warning("Non-POST request from %s", soup_message_headers_get_one(msg->request_headers, "User-Agent"));
        soup_message_set_status(msg, SOUP_STATUS_NOT_IMPLEMENTED);
        return;
    }

    const gchar *query_token = NULL;
    const gchar *project = NULL;
    if ( query != NULL )
    {
        query_token = g_hash_table_lookup(query, "token");
        project = g_hash_table_lookup(query, "project");
    }

    if ( ( token != NULL ) && ( ( query_token == NULL ) || ( g_strcmp0(token, query_token) != 0 ) ) )
    {
        g_warning("Unauthorized request from %s", soup_message_headers_get_one(msg->request_headers, "User-Agent"));
        soup_message_set_status(msg, SOUP_STATUS_UNAUTHORIZED);
        return;
    }

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
        eventd_event_add_data(event, g_strdup("pusher-name"), json_object_dup_string_member(pusher, "name"));
        //eventd_event_add_data(event, g_strdup("author-username"), json_object_dup_string_member(pusher, "username"));
        //eventd_event_add_data(event, g_strdup("author-email"), json_object_dup_string_member(pusher, "email"));
        eventd_event_add_data(event, g_strdup("size"), g_strdup_printf("%u", json_array_get_length(commits)));
        eventd_event_add_data(event, g_strdup("url"), _github_eventc_get_url(json_object_get_string_member(root, "compare")));

        eventd_event_add_data(event, g_strdup("repository-name"), json_object_dup_string_member(repository, "name"));
        eventd_event_add_data(event, g_strdup("branch"), g_strdup(json_object_get_string_member(root, "ref") + strlen("refs/heads/")));
        if ( project != NULL )
            eventd_event_add_data(event, g_strdup("project"), g_strdup(project));

        eventc_connection_event(connection, event, NULL);
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
            eventd_event_add_data(event, g_strdup("url"), _github_eventc_get_url(json_object_get_string_member(commit, "url")));

            eventd_event_add_data(event, g_strdup("repository-name"), json_object_dup_string_member(repository, "name"));
            eventd_event_add_data(event, g_strdup("branch"), g_strdup(json_object_get_string_member(root, "ref") + strlen("refs/heads/")));
            if ( project != NULL )
                eventd_event_add_data(event, g_strdup("project"), g_strdup(project));

            eventc_connection_event(connection, event, NULL);
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
    gchar *shortener_name = NULL;


    int retval = 0;
    GError *error = NULL;
    GOptionContext *option_context = NULL;

#if ! GLIB_CHECK_VERSION(2,35,1)
    g_type_init();
#endif /* ! GLIB_CHECK_VERSION(2,35,1) */

    GOptionEntry entries[] =
    {
        { "port",           'p', 0, G_OPTION_ARG_INT,      &port,           "Port to listen to (defaults to 0, random)",                  "<port>" },
        { "token",          't', 0, G_OPTION_ARG_STRING,   &token,          "Token to check in the client query",                         "<token>" },
        { "cert-file",      'c', 0, G_OPTION_ARG_FILENAME, &cert_file,      "Path to the certificate file",                               "<path>" },
        { "key-file",       'k', 0, G_OPTION_ARG_FILENAME, &key_file,       "Path to the key file (defaults to cert-file)",               "<path>" },
        { "host",           'h', 0, G_OPTION_ARG_STRING,   &host,           "eventd host to connect to",                                  "<host>" },
        { "merge-thresold", 'm', 0, G_OPTION_ARG_INT,      &merge_thresold, "Number of commits to start merging (defaults to 5)",         "<thresold>" },
        { "commit-id-size",  0,  0, G_OPTION_ARG_INT,      &commit_id_size, "Number of chars to limmit the commit id to (defaults to 7)", "<limit>" },
        { "shortener",      's', 0, G_OPTION_ARG_STRING,   &shortener_name, "Shortener service name (\"list\" for a list)",               "<name>" },
        { "version",        'V', 0, G_OPTION_ARG_NONE,     &print_version,  "Print version",                                              NULL },
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

    if ( shortener_name != NULL )
    {
        gint i;
        if ( g_strcmp0(shortener_name, "list") == 0 )
        {
            g_printf("Supported shorteners:\n");
            for ( i = 1 ; i < GITHUB_EVENTC_SHORTENERS_SIZE ; ++i )
                g_printf("    %s\n", shorteners[i].name);
            goto end;
        }
        else
        {
            for ( i = 1 ; i < GITHUB_EVENTC_SHORTENERS_SIZE ; ++i )
            {
                if ( g_strcmp0(shortener_name, shorteners[i].name) == 0 )
                    shortener = &shorteners[i];
            }
            if ( shortener == NULL )
            {
                g_printf("Unknown shortener: %s", shortener_name);
                retval = 3;
                goto end;
            }
        }
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
            retval = 2;
        }
        else
        {
            if ( shortener != NULL )
                shortener_session = g_object_new(SOUP_TYPE_SESSION, SOUP_SESSION_ADD_FEATURE_BY_TYPE, SOUP_TYPE_CONTENT_DECODER, SOUP_SESSION_USER_AGENT, PACKAGE_NAME " ", NULL);

            soup_server_add_handler(server, NULL, _github_eventc_gateway_server_callback, client, NULL);
            soup_server_run_async(server);
            g_main_loop_run(loop);
            g_main_loop_unref(loop);
            soup_server_quit(server);

            if ( shortener_session != NULL )
                g_object_unref(shortener_session);
        }
    }

    g_object_unref(client);

end:
    g_free(shortener_name);
    g_free(host);
    g_free(key_file);
    g_free(cert_file);

    return retval;
}
