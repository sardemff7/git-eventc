/*
 * git-eventc-webhook - WebHook to eventd server for various Git hosting providers
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
#include <glib-unix.h>
#endif /* G_OS_UNIX */

#ifdef ENABLE_SYSTEMD
#ifdef HAVE_SYS_SOCKET_H
#include <sys/socket.h>
#endif /* HAVE_SYS_SOCKET_H */
#include <systemd/sd-daemon.h>
#define SYSTEMD_SOCKETS_HELP ", -1 (= none) if systemd sockets are detected"
#else /* ! ENABLE_SYSTEMD */
#define SYSTEMD_SOCKETS_HELP
#endif /* ! ENABLE_SYSTEMD */

#include <libsoup/soup.h>
#include <json-glib/json-glib.h>

#include "libgit-eventc.h"

static GHashTable *secrets = NULL;

static void
_git_eventc_webhook_node_list_to_string_list(GList *list)
{
    /* Retrieve the actual strings */
    GList *node;
    for ( node = list ; node != NULL ; node = g_list_next(node) )
        node->data = (gpointer) json_node_get_string(node->data);
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

static guint
_git_eventc_webhook_payload_parse_github(const gchar **project, JsonObject *root)
{
    JsonObject *repository = json_object_get_object_member(root, "repository");

    JsonArray *commits = json_object_get_array_member(root, "commits");
    guint size = json_array_get_length(commits);

    const gchar *repository_name = json_object_get_string_member(repository, "name");
    const gchar *branch = json_object_get_string_member(root, "ref") + strlen("refs/heads/");

    if ( git_eventc_is_above_threshold(size) )
    {
        JsonObject *pusher = json_object_get_object_member(root, "pusher");

        git_eventc_send_commit_group(
            json_object_get_string_member(pusher, "name"),
            size,
            json_object_get_string_member(root, "compare"),
            repository_name, branch, project);
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
            files = _git_eventc_webhook_payload_get_files_github(commit);

            git_eventc_send_commit(
                json_object_get_string_member(commit, "id"),
                json_object_get_string_member(commit, "message"),
                json_object_get_string_member(commit, "url"),
                json_object_get_string_member(author, "name"),
                json_object_get_string_member(author, "username"),
                json_object_get_string_member(author, "email"),
                repository_name, branch,
                files,
                project);

            g_free(files);
        }
        g_list_free(commit_list);
    }

    return SOUP_STATUS_OK;
}

static void
_git_eventc_webhook_gateway_server_callback(SoupServer *server, SoupMessage *msg, const char *path, GHashTable *query, SoupClientContext *client, gpointer user_data)
{
    const gchar *user_agent = soup_message_headers_get_one(msg->request_headers, "User-Agent");

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
        g_warning("Bad request (no Content-Type header) from %s", user_agent);
        goto cleanup;
    }

    project = g_strsplit(path+1, "/", 2);
    if ( project[0] == NULL )
    {
        g_warning("Bad request (no project in path) from %s", user_agent);
        goto cleanup;
    }

    const gchar *signature;
    signature = soup_message_headers_get_one(msg->request_headers, "X-Hub-Signature");
    if ( signature != NULL )
    {
        status_code = SOUP_STATUS_UNAUTHORIZED;

        const gchar *secret = g_hash_table_lookup(secrets, project[0]);

        if ( secret == NULL )
        {
            g_warning("Cannot verify signature of request from %s", user_agent);
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

    const gchar *payload = NULL;
    if ( g_strcmp0(content_type, "application/json") == 0 )
        payload = msg->request_body->data;
    else if ( g_strcmp0(content_type, "application/x-www-form-urlencoded") == 0 )
    {
        data = soup_form_decode(msg->request_body->data);

        if ( data == NULL )
        {
            g_warning("Bad POST (no data) from %s", user_agent);
            goto cleanup;
        }
        payload = g_hash_table_lookup(data, "payload");
    }

    if ( payload == NULL )
    {
        g_warning("Bad POST (no payload) from %s", user_agent);
        goto cleanup;
    }
    JsonParser *parser = json_parser_new();

    json_parser_load_from_data(parser, payload, -1, NULL);

    JsonNode *root_node = json_parser_get_root(parser);
    JsonObject *root = json_node_get_object(root_node);

    status_code = SOUP_STATUS_NOT_IMPLEMENTED;
    if ( g_str_has_prefix(user_agent, "GitHub-Hookshot/") )
    {
        const gchar *event = soup_message_headers_get_one(msg->request_headers, "X-GitHub-Event");
        if ( g_strcmp0(event, "push") == 0 )
            status_code = _git_eventc_webhook_payload_parse_github(project, root);
        else if ( g_strcmp0(event, "ping") == 0 )
            status_code = SOUP_STATUS_OK;
    }
    else
        g_warning("Unknown WebHook service: %s", user_agent);

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

    if ( ! git_eventc_parse_options(&argc, &argv, entries, "- Git WebHook to eventd gateway", _git_eventc_webhook_extra_key_file_parsing, &print_version) )
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
