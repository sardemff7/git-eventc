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
#include <glib/gstdio.h>
#include <glib-object.h>
#ifdef G_OS_UNIX
#include <glib-unix.h>
#endif /* G_OS_UNIX */

#ifdef ENABLE_SYSTEMD
#include <sys/socket.h>
#include <systemd/sd-daemon.h>
#define SYSTEMD_SOCKETS_HELP ", -1 (= none) if systemd sockets are detected"
#else /* ! ENABLE_SYSTEMD */
#define SYSTEMD_SOCKETS_HELP
#endif /* ! ENABLE_SYSTEMD */

#include <libsoup/soup.h>
#include <json-glib/json-glib.h>

#include "libgit-eventc.h"
#include "webhook-github.h"
#include "webhook-gitlab.h"
#include "webhook-travis.h"

typedef enum {
    GIT_EVENTC_WEBHOOK_SERVICE_UNKNOWN = 0,
    GIT_EVENTC_WEBHOOK_SERVICE_GITHUB,
    GIT_EVENTC_WEBHOOK_SERVICE_GITLAB,
    GIT_EVENTC_WEBHOOK_SERVICE_TRAVIS,
} GitEventcWebhookService;

typedef struct {
    gchar **project;
    JsonParser *parser;
    void (*func)(const gchar **project, JsonObject *root);
} GitEventcWebhookParseData;

static GHashTable *secrets = NULL;

JsonNode *
git_eventc_webhook_api_get(const gchar *url)
{
    g_return_val_if_fail(url != NULL, NULL);

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

GList *
git_eventc_webhook_node_list_to_string_list(GList *list)
{
    /* Retrieve the actual strings */
    GList *node;
    for ( node = list ; node != NULL ; node = g_list_next(node) )
        node->data = (gpointer) json_node_get_string(node->data);

    return list;
}

static gboolean
_git_eventc_webhook_parse_callback(gpointer user_data)
{
    GitEventcWebhookParseData *data = user_data;

    JsonNode *root_node = json_parser_get_root(data->parser);
    JsonObject *root = json_node_get_object(root_node);

    data->func((const gchar **) data->project, root);

    g_strfreev(data->project);
    g_object_unref(data->parser);

    g_slice_free(GitEventcWebhookParseData, data);

    return FALSE;
}

static void
_git_eventc_webhook_gateway_server_callback(SoupServer *server, SoupMessage *msg, const char *path, GHashTable *query, SoupClientContext *client, gpointer user_data)
{
    const gchar *user_agent = soup_message_headers_get_one(msg->request_headers, "User-Agent");
    if ( user_agent == NULL )
        user_agent = "";

    gchar **project = NULL;
    GHmac *hmac = NULL;
    GHashTable *data = NULL;

    guint status_code = SOUP_STATUS_NOT_IMPLEMENTED;

    if ( msg->method != SOUP_METHOD_POST )
    {
        g_warning("Non-POST request from %s", user_agent);
        goto cleanup;
    }

    status_code = SOUP_STATUS_BAD_REQUEST;

    const gchar *content_type = soup_message_headers_get_one(msg->request_headers, "Content-Type");
    if ( content_type == NULL )
    {
        g_warning("Bad request from %s: no Content-Type header", user_agent);
        goto cleanup;
    }

    project = g_strsplit(path+1, "/", 2);
    if ( project[0] == NULL )
    {
        g_warning("Bad request from %s: no project group in path '%s'", user_agent, path);
        goto cleanup;
    }

    GitEventcWebhookService service = GIT_EVENTC_WEBHOOK_SERVICE_UNKNOWN;
    if ( g_str_has_prefix(user_agent, "GitHub-Hookshot/") )
        service = GIT_EVENTC_WEBHOOK_SERVICE_GITHUB;
    else if ( g_str_has_prefix(user_agent, "Travis CI ") )
        service = GIT_EVENTC_WEBHOOK_SERVICE_TRAVIS;
    else if ( soup_message_headers_get_one(msg->request_headers, "X-Gitlab-Event") != NULL )
        service = GIT_EVENTC_WEBHOOK_SERVICE_GITLAB;
    else
    {
        g_warning("Unknown WebHook service: %s", user_agent);
        goto cleanup;
    }

    if ( secrets != NULL )
    {
        status_code = SOUP_STATUS_UNAUTHORIZED;

        const gchar *secret = NULL;

        if ( project[1] != NULL )
            secret = g_hash_table_lookup(secrets, project[1]);
        if ( secret == NULL )
            secret = g_hash_table_lookup(secrets, project[0]);

        if ( secret == NULL )
        {
            g_warning("Signature mandatory but not secret for project group %s (%s)", project[0], user_agent);
            goto cleanup;
        }

        if ( *secret != '\0' )
        {
            switch ( service )
            {
            case GIT_EVENTC_WEBHOOK_SERVICE_GITHUB:
            {
                const gchar *signature;
                signature = soup_message_headers_get_one(msg->request_headers, "X-Hub-Signature");
                if ( signature == NULL )
                {
                    g_warning("Signature mandatory but not found %s", user_agent);
                    goto cleanup;
                }

                if ( ! g_str_has_prefix(signature, "sha1=") )
                {
                    g_warning("Signature of request from %s does not match", user_agent);
                    goto cleanup;
                }
                signature += strlen("sha1=");

                hmac = g_hmac_new(G_CHECKSUM_SHA1, (const guchar *) secret, strlen(secret));
                g_hmac_update(hmac, (const guchar *) msg->request_body->data, msg->request_body->length);

                if ( g_ascii_strcasecmp(signature, g_hmac_get_string(hmac)) != 0 )
                {
                    g_warning("Signature of request from %s does not match %s != %s", user_agent, signature, g_hmac_get_string(hmac));
                    goto cleanup;
                }
            }
            break;
            case GIT_EVENTC_WEBHOOK_SERVICE_GITLAB:
            {
                const gchar *query_secret = soup_message_headers_get_one(msg->request_headers, "X-Gitlab-Token");
                if ( query_secret == NULL )
                {
                    g_warning("No secret in query (%s)", user_agent);
                    goto cleanup;
                }
                if ( g_strcmp0(secret, query_secret) != 0 )
                {
                    g_warning("Wrong secret in query (%s): %s != %s", user_agent, secret, query_secret);
                    goto cleanup;
                }
            }
            break;
            case GIT_EVENTC_WEBHOOK_SERVICE_TRAVIS:
            {
                /* We do not have nice TLS Signature support in GLib/GIO
                 * so we just use URL query "secret" */
                const gchar *query_secret = g_hash_table_lookup(query, "secret");
                if ( query_secret == NULL )
                {
                    g_warning("No secret in query (%s)", user_agent);
                    goto cleanup;
                }
                if ( g_strcmp0(secret, query_secret) != 0 )
                {
                    g_warning("Wrong secret in query (%s): %s != %s", user_agent, secret, query_secret);
                    goto cleanup;
                }
            }
            break;
            case GIT_EVENTC_WEBHOOK_SERVICE_UNKNOWN:
                g_return_if_reached();
            }
        }
    }

    status_code = SOUP_STATUS_BAD_REQUEST;
    const gchar *payload = NULL;
    if ( g_strcmp0(content_type, "application/json") == 0 )
        payload = msg->request_body->data;
    else if ( g_strcmp0(content_type, "application/x-www-form-urlencoded") == 0 )
    {
        data = soup_form_decode(msg->request_body->data);

        if ( data == NULL )
        {
            g_warning("Bad POST from %s: no data", user_agent);
            goto cleanup;
        }
        payload = g_hash_table_lookup(data, "payload");
    }

    if ( payload == NULL )
    {
        g_warning("Bad POST from %s: no payload", user_agent);
        goto cleanup;
    }
    JsonParser *parser = json_parser_new();

    GError *error = NULL;
    if ( ! json_parser_load_from_data(parser, payload, -1, &error) )
    {
        g_warning("Could not parse JSON: %s", error->message);
        g_clear_error(&error);
        goto cleanup;
    }

    JsonNode *root = json_parser_get_root(parser);
    if ( root == NULL )
    {
        g_warning("Bad POST from %s: Empty payload", user_agent);
        goto cleanup;
    }

    GitEventcWebhookParseData parse_data = {
        .project = project,
        .parser = parser,
    };

    status_code = SOUP_STATUS_NOT_IMPLEMENTED;
    switch ( service )
    {
    case GIT_EVENTC_WEBHOOK_SERVICE_GITHUB:
    {
        const gchar *event = soup_message_headers_get_one(msg->request_headers, "X-GitHub-Event");
        if ( g_strcmp0(event, "push") == 0 )
        {
            parse_data.func = git_eventc_webhook_payload_parse_github_push;
            status_code = SOUP_STATUS_OK;
        }
        else if ( g_strcmp0(event, "issues") == 0 )
        {
            parse_data.func = git_eventc_webhook_payload_parse_github_issues;
            status_code = SOUP_STATUS_OK;
        }
        else if ( g_strcmp0(event, "pull_request") == 0 )
        {
            parse_data.func = git_eventc_webhook_payload_parse_github_pull_request;
            status_code = SOUP_STATUS_OK;
        }
        else if ( g_strcmp0(event, "ping") == 0 )
            status_code = SOUP_STATUS_OK;
    }
    break;
    case GIT_EVENTC_WEBHOOK_SERVICE_GITLAB:
    {
        const gchar *event = soup_message_headers_get_one(msg->request_headers, "X-Gitlab-Event");
        if ( g_strcmp0(event, "Push Hook") == 0 )
        {
            parse_data.func = git_eventc_webhook_payload_parse_gitlab_branch;
            status_code = SOUP_STATUS_OK;
        }
        else if ( g_strcmp0(event, "Tag Push Hook") == 0 )
        {
            parse_data.func = git_eventc_webhook_payload_parse_gitlab_tag;
            status_code = SOUP_STATUS_OK;
        }
        else if ( g_strcmp0(event, "Issue Hook") == 0 )
        {
            parse_data.func = git_eventc_webhook_payload_parse_gitlab_issue;
            status_code = SOUP_STATUS_OK;
        }
        else if ( g_strcmp0(event, "Merge Request Hook") == 0 )
        {
            parse_data.func = git_eventc_webhook_payload_parse_gitlab_merge_request;
            status_code = SOUP_STATUS_OK;
        }
    }
    break;
    case GIT_EVENTC_WEBHOOK_SERVICE_TRAVIS:
        parse_data.func = git_eventc_webhook_payload_parse_travis;
        status_code = SOUP_STATUS_OK;
    break;
    case GIT_EVENTC_WEBHOOK_SERVICE_UNKNOWN:
        g_return_if_reached();
    }

    if ( parse_data.func != NULL )
    {
        project = NULL;
        g_idle_add(_git_eventc_webhook_parse_callback, g_slice_dup(GitEventcWebhookParseData, &parse_data));
    }
    else
        g_object_unref(parser);

cleanup:
    if ( data != NULL )
        g_hash_table_unref(data);
    if ( hmac != NULL )
        g_hmac_unref(hmac);
    g_strfreev(project);
    soup_message_set_status(msg, status_code);
}

SoupServer *
_git_eventc_webhook_soup_server_init(gint port, const gchar *cert_file, const gchar *key_file, int *retval)
{
    GError *error = NULL;
    SoupServer *server;

    server = soup_server_new(SOUP_SERVER_SSL_CERT_FILE, cert_file, SOUP_SERVER_SSL_KEY_FILE, ( ( key_file == NULL ) ? cert_file : key_file ), NULL);
    if ( server == NULL )
        goto error;

    /* FIXME: We should be using soup_server_set_ssl_cert_file instead of the constructor property
     * but the symbol was forgotten in the 2.48 release */
#if 0
    if ( cert_file != NULL )
    {
        soup_server_set_ssl_cert_file(server, cert_file, ( ( key_file == NULL ) ? cert_file : key_file ), &error);
        g_warning("Couldn't set SSL/TLS certificate: %s", error->message);
        goto error;
    }
#endif /* 0: see FIXME */


    soup_server_add_handler(server, NULL, _git_eventc_webhook_gateway_server_callback, NULL, NULL);

    SoupServerListenOptions options = 0;
    if ( cert_file != NULL )
        options |= SOUP_SERVER_LISTEN_HTTPS;

#ifdef ENABLE_SYSTEMD
    gint systemd_fds;
    systemd_fds = sd_listen_fds(TRUE);
    if ( systemd_fds < 0 )
    {
        g_warning("Failed to acquire systemd sockets: %s", g_strerror(-systemd_fds));
        goto error;
    }
    if ( ( systemd_fds > 0 ) && ( port == 0 ) )
        port = -1;

    gint fd;
    for ( fd = SD_LISTEN_FDS_START ; fd < SD_LISTEN_FDS_START + systemd_fds ; ++fd )
    {
        gint r;
        r = sd_is_socket(fd, AF_UNSPEC, SOCK_STREAM, 1);
        if ( r < 0 )
        {
            g_warning("Failed to verify systemd socket type: %s", g_strerror(-r));
            goto error;
        }

        if ( r == 0 )
            continue;

        if ( ! soup_server_listen_fd(server, fd, options, &error) )
        {
            g_warning("Failed to take a socket from systemd: %s", error->message);
            goto error;
        }
    }
#endif /* ENABLE_SYSTEMD */

    if ( port == -1 )
        return server;

    if ( ! soup_server_listen_all(server, port, options, &error) )
    {
        g_warning("Couldn't listen on port %d: %s", port,  error->message);
        goto error;
    }

    return server;

error:
    g_clear_error(&error);
    if ( server == NULL )
        g_warning("Couldn't create the server");
    else
        g_object_unref(server);

    *retval = 2;
    return NULL;
}

static gboolean
_git_eventc_webhook_extra_key_file_parsing(GKeyFile *key_file, GError **error)
{
    const gchar *group = "webhook-secrets";
    if ( ! g_key_file_has_group(key_file, group) )
        return TRUE;

    gchar **projects;
    projects = g_key_file_get_keys(key_file, group, NULL, error);
    if ( projects == NULL )
        return FALSE;

    secrets = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
    gchar **project;
    for ( project = projects ; *project != NULL ; ++project )
    {
        gchar *secret;
        secret = g_key_file_get_string(key_file, group, *project, error);
        if ( *error != NULL )
            return FALSE;
        g_hash_table_insert(secrets, *project, secret);
    }
    g_free(projects);

    return TRUE;
}

int
main(int argc, char *argv[])
{
    gchar *tls_cert_file = NULL;
    gchar *tls_key_file = NULL;
    gint port = 0;
    gboolean print_version;

    int retval = 1;

    GOptionEntry entries[] =
    {
        { "port",           'p', 0, G_OPTION_ARG_INT,      &port,           "Port to listen to (defaults to 0, random" SYSTEMD_SOCKETS_HELP ")", "<port>" },
        { "cert-file",      'c', 0, G_OPTION_ARG_FILENAME, &tls_cert_file,  "Path to the certificate file",                                      "<path>" },
        { "key-file",       'k', 0, G_OPTION_ARG_FILENAME, &tls_key_file,   "Path to the key file (defaults to cert-file)",                      "<path>" },
        { NULL }
    };

    if ( ! git_eventc_parse_options(&argc, &argv, "webhook", entries, "- Git WebHook to eventd gateway", _git_eventc_webhook_extra_key_file_parsing, &print_version) )
        goto end;
    if ( print_version )
    {
        g_print(PACKAGE_NAME "-webhook " PACKAGE_VERSION "\n");
        retval = 0;
        goto end;
    }

    GMainLoop *loop;
    loop = g_main_loop_new(NULL, FALSE);

    if ( git_eventc_init(loop, &retval) )
    {
        SoupServer *server;
        server = _git_eventc_webhook_soup_server_init(port, tls_cert_file, tls_key_file, &retval);
        if ( server != NULL )
        {
            g_main_loop_run(loop);
            g_object_unref(server);
            retval = 0;
        }
        else
            retval = 3;
    }
    else
        retval = 2;
    g_main_loop_unref(loop);

end:
    git_eventc_uninit();
    g_free(tls_key_file);
    g_free(tls_cert_file);

    return retval;
}
