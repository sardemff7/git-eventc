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
#if GLIB_CHECK_VERSION(2,32,0)
#include <glib-unix.h>
#endif /* GLIB_CHECK_VERSION(2,32,0) */
#endif /* G_OS_UNIX */

#include <libsoup/soup.h>
#include <json-glib/json-glib.h>

#include "libgit-eventc.h"

static gchar *token = NULL;


static guint
_git_eventc_webhook_payload_parse_github(const gchar *project, JsonObject *root)
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

            git_eventc_send_commit(
                json_object_get_string_member(commit, "id"),
                json_object_get_string_member(commit, "message"),
                json_object_get_string_member(commit, "url"),
                json_object_get_string_member(author, "name"),
                json_object_get_string_member(author, "username"),
                json_object_get_string_member(author, "email"),
                repository_name, branch, project);
        }
        g_list_free(commit_list);
    }

    return SOUP_STATUS_OK;
}

static guint
_git_eventc_webhook_payload_parse_gitorious(const gchar *project, JsonObject *root)
{
    JsonObject *repository = json_object_get_object_member(root, "repository");

    JsonArray *commits = json_object_get_array_member(root, "commits");
    guint size = json_array_get_length(commits);

    const gchar *repository_name = json_object_get_string_member(repository, "name");
    const gchar *branch = json_object_get_string_member(root, "ref");

    if ( git_eventc_is_above_threshold(size) )
    {
        gchar *url;

        url = g_strdup_printf("%s/commit/%s/diffs/%s", json_object_get_string_member(repository, "url"), json_object_get_string_member(root, "before"), json_object_get_string_member(root, "after"));
        git_eventc_send_commit_group(
            json_object_get_string_member(root, "pushed_by"),
            size,
            url,
            repository_name, branch, project);
        g_free(url);
    }
    else
    {
        GList *commit_list = json_array_get_elements(commits);
        GList *commit_;
        for ( commit_ = commit_list ; commit_ != NULL ; commit_ = g_list_next(commit_) )
        {
            JsonObject *commit = json_node_get_object(commit_->data);
            JsonObject *author = json_object_get_object_member(commit, "author");

            git_eventc_send_commit(
                json_object_get_string_member(commit, "id"),
                json_object_get_string_member(commit, "message"),
                json_object_get_string_member(commit, "url"),
                json_object_get_string_member(author, "name"),
                NULL,
                json_object_get_string_member(author, "email"),
                repository_name, branch, project);
        }
        g_list_free(commit_list);
    }

    return SOUP_STATUS_OK;
}

static void
_git_eventc_webhook_gateway_server_callback(SoupServer *server, SoupMessage *msg, const char *path, GHashTable *query, SoupClientContext *client, gpointer user_data)
{
    const gchar *user_agent = soup_message_headers_get_one(msg->request_headers, "User-Agent");

    if ( msg->method != SOUP_METHOD_POST )
    {
        g_warning("Non-POST request from %s", user_agent);
        soup_message_set_status(msg, SOUP_STATUS_NOT_IMPLEMENTED);
        return;
    }

    const gchar *query_token = NULL;
    const gchar *project = NULL;
    const gchar *service = NULL;
    if ( query != NULL )
    {
        query_token = g_hash_table_lookup(query, "token");
        project = g_hash_table_lookup(query, "project");
        service = g_hash_table_lookup(query, "service");
    }

    if ( ( token != NULL ) && ( ( query_token == NULL ) || ( g_strcmp0(token, query_token) != 0 ) ) )
    {
        g_warning("Unauthorized request from %s", user_agent);
        soup_message_set_status(msg, SOUP_STATUS_UNAUTHORIZED);
        return;
    }

    const gchar *payload = NULL;
    GHashTable *data = NULL;

    const gchar *content_type = soup_message_headers_get_one(msg->request_headers, "Content-Type");
    if ( content_type == NULL )
    {
        g_warning("Bad request (no Content-Type header) from %s", user_agent);
        soup_message_set_status(msg, SOUP_STATUS_BAD_REQUEST);
        return;
    }

    if ( g_strcmp0(content_type, "application/json") == 0 )
        payload = msg->request_body->data;
    else if ( g_strcmp0(content_type, "application/x-www-form-urlencoded") == 0 )
    {
        data = soup_form_decode(msg->request_body->data);

        if ( data == NULL )
        {
            g_warning("Bad POST (no data) from %s", user_agent);
            soup_message_set_status(msg, SOUP_STATUS_BAD_REQUEST);
            return;
        }
        payload = g_hash_table_lookup(data, "payload");
    }

    if ( payload == NULL )
    {
        g_warning("Bad POST (no payload) from %s", user_agent);
        soup_message_set_status(msg, SOUP_STATUS_BAD_REQUEST);
        return;
    }
    JsonParser *parser = json_parser_new();

    json_parser_load_from_data(parser, payload, -1, NULL);
    if ( data != NULL )
        g_hash_table_destroy(data);

    JsonNode *root_node = json_parser_get_root(parser);
    JsonObject *root = json_node_get_object(root_node);

    guint status_code = SOUP_STATUS_NOT_IMPLEMENTED;
    if ( g_str_has_prefix(user_agent, "GitHub Hookshot ") )
    {
        const gchar *event = soup_message_headers_get_one(msg->request_headers, "X-GitHub-Event");
        if ( g_strcmp0(event, "push") == 0 )
            status_code = _git_eventc_webhook_payload_parse_github(project, root);
        else if ( g_strcmp0(event, "ping") == 0 )
            status_code = SOUP_STATUS_OK;
    }
    else if ( g_strcmp0(service, "gitorious") == 0 )
        status_code = _git_eventc_webhook_payload_parse_gitorious(project, root);
    else
        g_warning("Unknown WebHook service: %s", user_agent);

    g_object_unref(parser);

    soup_message_set_status(msg, status_code);
}


int
main(int argc, char *argv[])
{
    gchar *cert_file = NULL;
    gchar *key_file = NULL;
    gint port = 0;
    gboolean print_version;

    int retval = 0;

#if ! GLIB_CHECK_VERSION(2,35,1)
    g_type_init();
#endif /* ! GLIB_CHECK_VERSION(2,35,1) */

    GOptionEntry entries[] =
    {
        { "port",           'p', 0, G_OPTION_ARG_INT,      &port,           "Port to listen to (defaults to 0, random)",                  "<port>" },
        { "token",          't', 0, G_OPTION_ARG_STRING,   &token,          "Token to check in the client query",                         "<token>" },
        { "cert-file",      'c', 0, G_OPTION_ARG_FILENAME, &cert_file,      "Path to the certificate file",                               "<path>" },
        { "key-file",       'k', 0, G_OPTION_ARG_FILENAME, &key_file,       "Path to the key file (defaults to cert-file)",               "<path>" },
        { NULL }
    };

    git_eventc_parse_options(&argc, &argv, entries, "- Git WebHook to eventd gateway", &print_version);
    if ( print_version )
    {
        g_print(PACKAGE_NAME "-webhook " PACKAGE_VERSION "\n");
        goto end;
    }

    GMainLoop *loop;
    loop = g_main_loop_new(NULL, FALSE);

    if ( git_eventc_init(loop, &retval) )
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
            soup_server_add_handler(server, NULL, _git_eventc_webhook_gateway_server_callback, NULL, NULL);
            soup_server_run_async(server);
            g_main_loop_run(loop);
            soup_server_quit(server);
        }
    }
    g_main_loop_unref(loop);

end:
    git_eventc_uninit();
    g_free(key_file);
    g_free(cert_file);

    return retval;
}
