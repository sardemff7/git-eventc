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

#include "config.h"

#include <locale.h>
#include <string.h>

#include <glib.h>
#include <glib/gstdio.h>
#include <glib-object.h>
#ifdef G_OS_UNIX
#include <glib-unix.h>
#endif /* G_OS_UNIX */

#include <libeventc.h>
#include <libsoup/soup.h>

#include "libgit-eventc.h"

const gchar * const git_eventc_ci_build_actions[GIT_EVENTC_CI_BUILD_NUM_ACTION] = {
    [GIT_EVENTC_CI_BUILD_ACTION_SUCCESS]  = "success",
    [GIT_EVENTC_CI_BUILD_ACTION_FAILURE]  = "failure",
    [GIT_EVENTC_CI_BUILD_ACTION_ERROR] = "error",
};

gsize
git_eventc_get_path_prefix_length(const gchar *a, const gchar *b, gsize max_length)
{
    const gchar *last_sep = a;
    gsize len = 0;
    while ( ( len < max_length ) && ( a[len] == b[len] ) )
    {
        if ( a[len++] == '/' )
            last_sep = a + len;
    }
    return (last_sep - a);
}

gchar *
git_eventc_get_files(GList *paths)
{
    if ( paths == NULL )
        return NULL;
    if ( g_list_next(paths) == NULL )
        return g_strdup(paths->data);

    gsize size = 1;
    const gchar *prefix = paths->data;
    gsize prefix_length = strlen(prefix), len;
    GList *path;
    for ( path = g_list_next(paths) ; path != NULL ; path = g_list_next(path), ++size )
    {
        len = git_eventc_get_path_prefix_length(prefix, path->data, prefix_length);
        prefix_length = MIN(prefix_length, len);
        if ( len < 2 )
            /* Short-circuit if we are already nothing or root ('/') only */
            break;
    }
    size += g_list_length(path);

    GString *files;
    files = g_string_sized_new(strlen(paths->data) * size);
    if ( prefix_length > 0 )
        g_string_append_c(g_string_append_len(files, prefix, prefix_length), ' ');

    GList *path_;
    for ( path_ = paths ; path_ != NULL ; path_ = g_list_next(path_) )
    {
        const gchar *path = path_->data;
        g_string_append_c(g_string_append(files, path + prefix_length), ' ');
    }
    g_list_free(paths);

    g_string_truncate(files, files->len - 1);
    return g_string_free(files, FALSE);
}

typedef struct {
    gchar       *name;
    const gchar *method;
    gchar       *url;
    gchar       *field_name;
    gchar       *prefix;
    SoupStatus   status_code;
    gchar       *header;
} GitEventcShortener;

static GitEventcShortener _git_eventc_default_shorteners_high[] = {
    {
        .name        = "git.io",
        .method      = "POST",
        .url         = "https://git.io/",
        .field_name  = "url",
        .prefix      = "https://github.com/",
        .status_code = SOUP_STATUS_CREATED,
        .header      = "Location",
    },
};

static GitEventcShortener _git_eventc_default_shorteners_low[] = {
    {
        .name        = "is.gd",
        .method      = "POST",
        .url         = "https://is.gd/create.php?format=simple",
        .field_name  = "url",
    },
};

static GitEventcShortener *_git_eventc_shorteners;

static gchar *host = NULL;
static guint merge_threshold = 5;
static guint commit_id_size = 7;
static gboolean shortener = FALSE;

static gboolean should_reconnect = TRUE;
static EventcConnection *client = NULL;
static SoupSession *shortener_session = NULL;
static guint retry_timeout = 0;
static guint retry_timeout_seconds = 1;

#define g_key_file_get_string_list_alt(kf, g, k, e) g_key_file_get_string_list(kf, g, k, NULL, e)

#define get_entry_with_code(arg_type, type_name, type, code) \
    case G_OPTION_ARG_##arg_type: \
    G_STMT_START { \
        gboolean has; \
        has = g_key_file_has_key(key_file, group, entry->long_name, error); \
        if ( *error != NULL ) \
            goto out; \
        if ( ! has ) \
            continue; \
        type v; \
        v = g_key_file_get_##type_name(key_file, group, entry->long_name, error); \
        if ( *error != NULL ) \
            goto out; \
        code; \
    } G_STMT_END; \
    break;

#define get_entry(arg_type, type_name, type) get_entry_with_code(arg_type, type_name, type, *((type *) entry->arg_data) = v)

static gboolean
_git_eventc_parse_config_file(GKeyFile *key_file, const gchar *group, GOptionEntry *entry, GError **error)
{
    for ( ; entry->long_name != NULL ; ++entry )
    {
        if ( entry->short_name == 'V' )
            continue;

        switch ( entry->arg )
        {
        get_entry_with_code(NONE, boolean, gboolean, *((gboolean *) entry->arg_data) = ( entry->flags & G_OPTION_FLAG_REVERSE) ? !v : v);
        get_entry(STRING, string, gchar *);
        get_entry(FILENAME, string, gchar *);
        get_entry(STRING_ARRAY, string_list_alt, gchar **);
        get_entry(FILENAME_ARRAY, string_list_alt, gchar **);
        get_entry(INT, integer, gint);
        get_entry(INT64, int64, gint64);
        get_entry(DOUBLE, double, gdouble);
        get_entry_with_code(CALLBACK, string, gchar *, ((GOptionArgFunc)entry->arg_data)(entry->long_name, v, NULL, error));
        }
    }

out:
    if ( *error == NULL )
        return TRUE;

    g_warning("Failed to parse '%s' option: %s", entry->long_name, (*error)->message);
    return FALSE;
}

#undef get_entry
#undef get_entry_with_code

static const gchar *
_git_eventc_shorteners_method_parse(const gchar *method)
{
    static const gchar *_git_eventc_shorteners_methods[] = {
        NULL, /* POST */
        NULL, /* GET */
        NULL
    };
    if ( _git_eventc_shorteners_methods[0] == NULL )
    {
        _git_eventc_shorteners_methods[0] = SOUP_METHOD_POST;
        _git_eventc_shorteners_methods[1] = SOUP_METHOD_GET;
    }
    gsize i;
    for ( i = 0 ; _git_eventc_shorteners_methods[i] != NULL ; ++i )
    {
        if ( g_ascii_strcasecmp(method, _git_eventc_shorteners_methods[i]) == 0 )
            return _git_eventc_shorteners_methods[i];
    }
    return NULL;
}

static void
_git_eventc_shorteners_add_defaults(GitEventcShortener list[], gsize offset, gsize length)
{
    gsize i;
    for ( i = 0 ; i < length ; ++i )
    {
#define dup_field(n) _git_eventc_shorteners[offset + i].n = g_strdup(list[i].n)
        dup_field(name);
        _git_eventc_shorteners[offset + i].method = _git_eventc_shorteners_method_parse(list[i].method);
        dup_field(url);
        dup_field(field_name);
        dup_field(prefix);
        _git_eventc_shorteners[offset + i].status_code = list[i].status_code;
        dup_field(header);
#undef dup_field
    }
}

static gboolean
_git_eventc_shorteners_parse_key_string(gchar **field, GKeyFile *key_file, const gchar *section, const gchar *key, GError **error)
{
    if ( ! g_key_file_has_key(key_file, section, key, error) )
        return ( *error == NULL );

    gchar *value;

    value = g_key_file_get_string(key_file, section, key, error);
    if ( value == NULL )
        return FALSE;

    *field = value;
    return TRUE;
}

static gboolean
_git_eventc_shorteners_parse_key_method(GitEventcShortener *shortener, GKeyFile *key_file, const gchar *section, GError **error)
{
    gchar *value = NULL;
    if ( ! _git_eventc_shorteners_parse_key_string(&value, key_file, section, "method", error) )
        return FALSE;
    if ( value == NULL )
        return TRUE;

    shortener->method = _git_eventc_shorteners_method_parse(value);
    if ( shortener->method == NULL )
        g_set_error(error, G_KEY_FILE_ERROR, G_KEY_FILE_ERROR_PARSE, "Wrong value for 'method': %s", value);
    g_free(value);
    return ( shortener->method != NULL );
}

static gboolean
_git_eventc_shorteners_parse_key_status_code(GitEventcShortener *shortener, GKeyFile *key_file, const gchar *section, GError **error)
{
    if ( ! g_key_file_has_key(key_file, section, "status-code", error) )
        return ( *error == NULL );

    gint value;

    value = g_key_file_get_integer(key_file, section, "status-code", error);
    if ( *error != NULL )
        return FALSE;

    if ( ( ! SOUP_STATUS_IS_INFORMATIONAL(value) ) && ( ! SOUP_STATUS_IS_SUCCESSFUL(value) ) && ( ! SOUP_STATUS_IS_SUCCESSFUL(value) ) )
    {
        g_set_error(error, G_KEY_FILE_ERROR, G_KEY_FILE_ERROR_PARSE, "Wrong value for 'status-code': %d", value);
        return FALSE;
    }

    shortener->status_code = value;
    return TRUE;
}

#define get_str_field_with_name(n, k) G_STMT_START { \
        if ( ! _git_eventc_shorteners_parse_key_string(&shortener->n, key_file, section, k, error) ) \
            return FALSE; \
    } G_STMT_END
#define get_str_field(n) get_str_field_with_name(n, #n)

static gboolean
_git_eventc_shorteners_parse_section(GitEventcShortener *shortener, GKeyFile *key_file, const gchar *section, GError **error)
{
    if ( ! _git_eventc_shorteners_parse_key_method(shortener, key_file, section, error) )
        return FALSE;
    get_str_field(url);
    get_str_field_with_name(field_name, "field-name");
    get_str_field(prefix);
    if ( ! _git_eventc_shorteners_parse_key_status_code(shortener, key_file, section, error) )
        return FALSE;
    get_str_field(header);
    shortener->name = g_strdup(section + strlen("shortener "));
    return TRUE;
}

#undef get_str_field
#undef get_str_field_with_name

static gboolean
_git_eventc_shorteners_parse(GKeyFile *key_file, GError **error)
{
    gsize hl = G_N_ELEMENTS(_git_eventc_default_shorteners_high), l = 0, ll = G_N_ELEMENTS(_git_eventc_default_shorteners_low);
    gchar **sections = NULL, **section, **list = NULL;

    if ( key_file != NULL )
        sections = g_key_file_get_groups(key_file, &l);
    if ( sections != NULL )
    {
        list = g_newa(gchar *, l);
        for ( section = sections, l = 0 ; *section != NULL ; ++section )
        {
            if ( g_str_has_prefix(*section, "shortener ") )
                list[l++] = *section;
            else
                g_free(*section);
        }
        list[l] = NULL;
        g_free(sections);
    }

    _git_eventc_shorteners = g_new0(GitEventcShortener, hl + l + ll + 1);

    if ( l == 0 )
        l = hl;
    else
    for ( section = list, l = hl ; *section != NULL ; ++section, ++l )
    {
        if ( ! _git_eventc_shorteners_parse_section(&_git_eventc_shorteners[l], key_file, *section, error) )
            return FALSE;
        g_free(*section);
    }

    _git_eventc_shorteners_add_defaults(_git_eventc_default_shorteners_high, 0, hl);
    _git_eventc_shorteners_add_defaults(_git_eventc_default_shorteners_low, l, ll);

    return TRUE;
}

gboolean
git_eventc_parse_options(gint *argc, gchar ***argv, const gchar *group, GOptionEntry *extra_entries, const gchar *description, GitEventcKeyFileFunc extra_parsing, gboolean *print_version)
{
    setlocale(LC_ALL, "");

    *print_version = FALSE;

    const gchar *config_file;
    gchar *config_file_ = NULL;
    GKeyFile *key_file = NULL;
    gboolean ret = FALSE;
    GError *error = NULL;
    config_file = config_file_ = g_build_filename(g_get_user_config_dir(), PACKAGE_NAME ".conf", NULL);
    if ( ! g_file_test(config_file, G_FILE_TEST_IS_REGULAR) )
    {
        /* System mode */
        g_setenv("XDG_RUNTIME_DIR", "/run", TRUE);
        config_file = SYSCONFDIR G_DIR_SEPARATOR_S PACKAGE_NAME ".conf";
    }
    if ( g_file_test(config_file, G_FILE_TEST_IS_REGULAR) )
    {
        key_file = g_key_file_new();
        if ( ! g_key_file_load_from_file(key_file, config_file, G_KEY_FILE_NONE, &error) )
        {
            g_warning("Could not parse config file '%s': %s", config_file, error->message);
            goto out;
        }
    }

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

    if ( key_file != NULL )
    {
        if ( g_key_file_has_group(key_file, PACKAGE_NAME) )
        {
            if ( ! _git_eventc_parse_config_file(key_file, PACKAGE_NAME, entries, &error) )
            {
                g_warning("Config file parsing failed: %s\n", error->message);
                goto out;
            }
        }
        if ( ( extra_entries != NULL ) && g_key_file_has_group(key_file, group) )
        {
            if ( ! _git_eventc_parse_config_file(key_file, group, extra_entries, &error) )
            {
                g_warning("Config file parsing failed: %s\n", error->message);
                goto out;
            }
        }
        if ( extra_parsing != NULL )
        {
            if ( ! extra_parsing(key_file, &error) )
            {
                g_warning("Config file parsing failed: %s\n", error->message);
                goto out;
            }
        }
    }
    if ( ! _git_eventc_shorteners_parse(key_file, &error) )
    {
        g_warning("Config file parsing failed: %s\n", error->message);
        goto out;
    }

    option_context = g_option_context_new(description);

    if ( extra_entries != NULL )
        g_option_context_add_main_entries(option_context, extra_entries, NULL);
    g_option_context_add_main_entries(option_context, entries, NULL);

    if ( ! g_option_context_parse(option_context, argc, argv, &error) )
    {
        g_warning("Option parsing failed: %s\n", error->message);
        goto out;
    }
    g_option_context_free(option_context);

    ret = TRUE;

out:
    if ( key_file != NULL )
        g_key_file_unref(key_file);
    g_free(config_file_);
    g_clear_error(&error);
    return ret;

}

static gboolean
_git_eventc_reconnect(gpointer user_data)
{
    GMainLoop *loop = user_data;
    GError *error = NULL;

    if ( ! eventc_connection_connect_sync(client, &error) )
    {
        g_warning("Couldn't connect to eventd: %s", error->message);
        g_error_free(error);
        if ( retry_timeout_seconds >= 1300 )
            g_main_loop_quit(loop);
        else
            retry_timeout = g_timeout_add_seconds(retry_timeout_seconds << 2, _git_eventc_reconnect, client);
    }
    else
    {
        retry_timeout = 0;
        retry_timeout_seconds = 1;
    }
    return FALSE;
}

static void
_git_eventc_disconnected(EventcConnection *client, gpointer user_data)
{
    GMainLoop *loop = user_data;

    if ( ! should_reconnect )
    {
        g_main_loop_quit(user_data);
        return;
    }

    if ( retry_timeout != 0 )
        return;
    retry_timeout = g_timeout_add_seconds(retry_timeout_seconds, _git_eventc_reconnect, loop);
}

#ifdef G_OS_UNIX
static gboolean
_git_eventc_stop(gpointer user_data)
{
    g_main_loop_quit(user_data);
    return FALSE;
}
#endif /* G_OS_UNIX */

gboolean
git_eventc_init(GMainLoop *loop, gint *retval)
{
#ifdef GIT_EVENTC_DEBUG
    g_setenv("G_MESSAGES_DEBUG", "all", FALSE);
#endif /* GIT_EVENTC_DEBUG */

#ifdef G_OS_UNIX
    g_unix_signal_add(SIGTERM, _git_eventc_stop, loop);
    g_unix_signal_add(SIGINT, _git_eventc_stop, loop);
#endif /* G_OS_UNIX */

    GError *error = NULL;
    client = eventc_connection_new(host, &error);
    if ( client == NULL )
    {
        g_warning("Couldn't resolve hostname: %s", error->message);
        g_error_free(error);
        *retval = 1;
        return FALSE;
    }

    if ( ! eventc_connection_connect_sync(client, &error) )
    {
        g_warning("Couldn't connect to eventd: %s", error->message);
        g_error_free(error);
        g_object_unref(client);
        client = NULL;
        *retval = 1;
        return FALSE;
    }
    g_signal_connect(client, "disconnected", G_CALLBACK(_git_eventc_disconnected), loop);

#ifdef GIT_EVENTC_DEBUG
#define bstring(b) ((b) ? "true" : "false")
    g_debug("Configuration:"
        "\n    Merge threshold: %d"
        "\n    Commit id size: %d"
        "\n    Use shortener: %s"
        "\n",
        merge_threshold,
        commit_id_size,
        bstring(shortener)
    );
#endif /* GIT_EVENTC_DEBUG */

    return TRUE;
}

void
git_eventc_disconnect(void)
{
    should_reconnect = FALSE;
    eventc_connection_close(client, NULL);
}

void
git_eventc_uninit(void)
{
    if ( shortener_session != NULL )
        g_object_unref(shortener_session);

    GitEventcShortener *shortener;
    for ( shortener = _git_eventc_shorteners ; shortener->name != NULL ; ++shortener )
    {
        g_free(shortener->name);
        g_free(shortener->url);
        g_free(shortener->field_name);
        g_free(shortener->prefix);
        g_free(shortener->header);
    }
    g_free(_git_eventc_shorteners);

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
_git_eventc_get_url(gchar *url, gboolean copy)
{
    if ( ( ! shortener ) || ( url == NULL ) || ( *url == '\0') )
        return copy ? g_strdup(url) : url;

    if ( shortener_session == NULL )
        shortener_session = g_object_new(SOUP_TYPE_SESSION, SOUP_SESSION_ADD_FEATURE_BY_TYPE, SOUP_TYPE_CONTENT_DECODER, SOUP_SESSION_USER_AGENT, PACKAGE_NAME " ", NULL);

    SoupURI *uri;
    SoupMessage *msg;
    gchar *escaped_url = NULL;
    gchar *data;
    gchar *short_url = NULL;

    GitEventcShortener *shortener;
    for ( shortener = _git_eventc_shorteners ; ( shortener->name != NULL ) && ( short_url == NULL ) ; ++shortener )
    {
        if ( ( shortener->prefix != NULL ) && ( ! g_str_has_prefix(url, shortener->prefix) ) )
            continue;

        uri = soup_uri_new(shortener->url);
        msg = soup_message_new_from_uri(shortener->method, uri);
        if ( escaped_url == NULL )
            escaped_url = soup_uri_encode(url, NULL);
        data = g_strdup_printf("%s=%s", shortener->field_name, escaped_url);
        soup_message_set_request(msg, "application/x-www-form-urlencoded", SOUP_MEMORY_TAKE, data, strlen(data));
        soup_session_send_message(shortener_session, msg);

        if ( ( ( shortener->status_code != SOUP_STATUS_NONE ) && ( shortener->status_code == msg->status_code ) )
             || ( ( shortener->status_code == SOUP_STATUS_NONE ) && SOUP_STATUS_IS_SUCCESSFUL(msg->status_code) ) )
        {
            if ( shortener->header != NULL )
                short_url = g_strdup(soup_message_headers_get_one(msg->response_headers, shortener->header));
            else
                short_url = g_strndup(msg->response_body->data, msg->response_body->length);
        }

        soup_uri_free(uri);
        g_object_unref(msg);
    }
    g_free(escaped_url);

    if ( short_url == NULL )
    {
        g_warning("Failed to shorten URL '%s'", url);
        short_url = copy ? g_strdup(url) : url;
    }
    else if ( ! copy )
        g_free(url);

    return short_url;
}

gchar *
git_eventc_get_url_const(const gchar *url)
{
    return _git_eventc_get_url((gchar *) url, TRUE);
}

gchar *
git_eventc_get_url(gchar *url)
{
    return _git_eventc_get_url((gchar *) url, FALSE);
}

static void
_git_eventc_event_take_data_string(EventdEvent *event, const gchar *name, gchar *value)
{
    eventd_event_add_data_string(event, g_strdup(name), value);
}

static void
_git_eventc_event_take_data_string_null(EventdEvent *event, const gchar *name, gchar *value)
{
    if ( value == NULL )
        return;
    _git_eventc_event_take_data_string(event, name, value);
}

static void
_git_eventc_event_add_data_string(EventdEvent *event, const gchar *name, const gchar *value)
{
    _git_eventc_event_take_data_string(event, name, g_strdup(value));
}

static void
_git_eventc_event_add_data_string_fallback(EventdEvent *event, const gchar *name, const gchar *value, const gchar *fallback)
{
    if ( value == NULL )
        value = fallback;
    _git_eventc_event_add_data_string(event, name, value);
}

static void
_git_eventc_event_add_data_string_null(EventdEvent *event, const gchar *name, const gchar *value)
{
    _git_eventc_event_take_data_string_null(event, name, g_strdup(value));
}

static void
_git_eventc_parse_message(const gchar *base_message, gchar **subject, gchar **message)
{
    *subject = NULL;
    *message = NULL;
    if ( base_message == NULL )
        return;

    gsize l = strlen(base_message);
    const gchar *new_line = g_utf8_strchr(base_message, l, '\n');
    if ( new_line != NULL )
    {
        const gchar *start = new_line, *end = base_message + l;

        /* We strip extra new lines */
        while ( g_utf8_get_char(start) == '\n' )
            start = g_utf8_next_char(start);

        /* Then we strip footer tags */
        gboolean all_tags = TRUE;
        const gchar *pe, *e;
        for ( e = end, pe = e ; all_tags && ( pe > start ) ; pe = e, e = g_utf8_strrchr(start, pe - start, '\n') )
        {
            const gchar *line;
            if ( e == NULL )
                line = e = start;
            else
                line = g_utf8_next_char(e);

            const gchar *t = line;
            gunichar c;

            if ( line == pe )
            {
                /* We found two \n in a row, so a group of tags */
                end = e;
                continue;
            }

            for ( c = g_utf8_get_char(t) ; g_unichar_isalnum(c) || ( c == '-' ) || ( c == '_' ) ; c = g_utf8_get_char(t) )
                t = g_utf8_next_char(t);
            if ( g_utf8_get_char(t) != ':' )
            {
                /* A line not starting with a tag, check for some other tags */
                const gchar * const lazy_tags[] = {
                    /* GitHub closing tags */
                    "close", "closes", "closed",
                    "fix", "fixes", "fixed",
                    "resolve", "resolves", "resolved",
                };

                gboolean ok = FALSE;
                gsize i;
                for ( i = 0 ; ( ! ok ) && ( i < G_N_ELEMENTS(lazy_tags) ) ; ++i )
                    ok = ( g_ascii_strncasecmp(line, lazy_tags[i], t - line) == 0 );
                if ( ! ok )
                    all_tags = FALSE;
            }
        }

        *subject = g_strndup(base_message, ( new_line - base_message ));
        if ( ! all_tags )
            *message = g_strndup(start, ( end - start ));
    }
    else
        *subject = g_strdup(base_message);
}

static void
_git_eventc_send_event(EventdEvent *event, gchar *url, const gchar *repository_name, const gchar *repository_url, const gchar **project)
{
    _git_eventc_event_take_data_string_null(event, "url", url);

    _git_eventc_event_add_data_string(event, "repository-name", repository_name);
    _git_eventc_event_add_data_string_null(event, "repository-url", repository_url);

    _git_eventc_event_add_data_string_null(event, "project-group", project[0]);
    _git_eventc_event_add_data_string_fallback(event, "project", project[1], repository_name);

    eventc_connection_send_event(client, event, NULL);
    eventd_event_unref(event);
}

static void
_git_eventc_send_branch(gboolean created, const gchar *pusher_name, gchar *url, const gchar *repository_name, const gchar *repository_url, const gchar *branch, const gchar **project)
{
    EventdEvent *event;

    event = eventd_event_new("scm", created ? "branch-creation" : "branch-deletion");

    _git_eventc_event_add_data_string(event, "pusher-name", pusher_name);
    _git_eventc_event_add_data_string(event, "branch", branch);

    _git_eventc_send_event(event, url,  repository_name, repository_url, project);
}

void
git_eventc_send_branch_creation(const gchar *pusher_name, gchar *url, const gchar *repository_name, const gchar *repository_url, const gchar *branch, const gchar **project)
{
    _git_eventc_send_branch(TRUE, pusher_name, url, repository_name, repository_url, branch, project);
}

void
git_eventc_send_branch_deletion(const gchar *pusher_name, const gchar *repository_name, const gchar *repository_url, const gchar *branch, const gchar **project)
{
    _git_eventc_send_branch(FALSE, pusher_name, NULL, repository_name, repository_url, branch, project);
}

static void
_git_eventc_send_tag(gboolean created, const gchar *pusher_name, gchar *url, const gchar *repository_name, const gchar *repository_url, const gchar *tag, const gchar *author_name, const gchar *author_email, const gchar *base_message, const gchar *previous_tag, const gchar **project)
{
    EventdEvent *event;

    event = eventd_event_new("scm", created ? "tag-creation" : "tag-deletion");

    gchar *subject, *message;
    _git_eventc_parse_message(base_message, &subject, &message);

    _git_eventc_event_take_data_string_null(event, "subject", subject);
    _git_eventc_event_take_data_string_null(event, "message", message);
    _git_eventc_event_add_data_string_null(event, "full-message", base_message);

    _git_eventc_event_add_data_string(event, "pusher-name", pusher_name);
    _git_eventc_event_add_data_string_null(event, "author-name", author_name);
    _git_eventc_event_add_data_string_null(event, "author-email", author_email);

    _git_eventc_event_add_data_string(event, "tag", tag);
    _git_eventc_event_add_data_string_null(event, "previous-tag", previous_tag);

    _git_eventc_send_event(event, url,  repository_name, repository_url, project);
}

void
git_eventc_send_tag_creation(const gchar *pusher_name, gchar *url, const gchar *repository_name, const gchar *repository_url, const gchar *tag, const gchar *author_name, const gchar *author_email, const gchar *message, const gchar *previous_tag, const gchar **project)
{
    _git_eventc_send_tag(TRUE, pusher_name, url, repository_name, repository_url, tag, author_name, author_email, message, previous_tag, project);
}

void
git_eventc_send_tag_deletion(const gchar *pusher_name, const gchar *repository_name, const gchar *repository_url, const gchar *tag, const gchar **project)
{
    _git_eventc_send_tag(FALSE, pusher_name, NULL, repository_name, repository_url, tag, NULL, NULL, NULL, NULL, project);
}

void
git_eventc_send_commit_group(const gchar *pusher_name, guint size, gchar *url, const gchar *repository_name, const gchar *repository_url, const gchar *branch, const gchar **project)
{
    EventdEvent *event;

    event = eventd_event_new("scm", "commit-group");

    _git_eventc_event_add_data_string(event, "pusher-name", pusher_name);
    eventd_event_add_data(event, g_strdup("size"), g_variant_new_uint64(size));

    _git_eventc_event_add_data_string(event, "branch", branch);

    _git_eventc_send_event(event, url,  repository_name, repository_url, project);
}

void
git_eventc_send_commit(const gchar *id, const gchar *base_message, gchar *url, const gchar *pusher_name, const gchar *author_name, const gchar *author_username, const gchar *author_email, const gchar *repository_name, const gchar *repository_url, const gchar *branch, const gchar *files, const gchar **project)
{
#ifdef GIT_EVENTC_DEBUG
    g_debug("Send commit:"
        "\nID: %s"
        "\nMessage: %s"
        "\nURL: %s"
        "\nPusher name: %s"
        "\nAuthor name: %s"
        "\nAuthor username: %s"
        "\nAuthor email: %s"
        "\nRepository: %s"
        "\nRepository URL: %s"
        "\nBranch: %s"
        "\nFiles: %s"
        "\nProject: %s / %s",
        id,
        base_message,
        url,
        pusher_name,
        author_name,
        author_username,
        author_email,
        repository_name,
        repository_url,
        branch,
        files,
        project[0], project[1]);
#endif /* GIT_EVENTC_DEBUG */

    gchar *subject, *message;
    _git_eventc_parse_message(base_message, &subject, &message);

    EventdEvent *event;

    event = eventd_event_new("scm", "commit");

    eventd_event_add_data_string(event, g_strdup("id"), g_strndup(id, commit_id_size));
    _git_eventc_event_take_data_string(event, "subject", subject);
    _git_eventc_event_take_data_string_null(event, "message", message);
    _git_eventc_event_add_data_string(event, "full-message", base_message);

    _git_eventc_event_add_data_string(event, "pusher-name", pusher_name);
    _git_eventc_event_add_data_string(event, "author-name", author_name);
    _git_eventc_event_add_data_string(event, "author-email", author_email);
    _git_eventc_event_add_data_string_null(event, "author-username", author_username);

    _git_eventc_event_add_data_string(event, "branch", branch);

    _git_eventc_event_add_data_string_null(event, "files", files);

    _git_eventc_send_event(event, url,  repository_name, repository_url, project);
}

void
git_eventc_send_push(gchar *url, const gchar *pusher_name, const gchar *repository_name, const gchar *repository_url, const gchar *branch, const gchar **project)
{
    EventdEvent *event;

    event = eventd_event_new("scm", "push");

    _git_eventc_event_add_data_string(event, "pusher-name", pusher_name);
    _git_eventc_event_add_data_string_null(event, "branch", branch);

    _git_eventc_send_event(event, url,  repository_name, repository_url, project);
}

void
git_eventc_send_bugreport(const gchar *action, guint64 id, const gchar *title, gchar *url, const gchar *author_name, const gchar *author_username, const gchar *author_email, GVariant *tags, const gchar *repository_name, const gchar *repository_url, const gchar **project)
{
    EventdEvent *event;

    event = eventd_event_new("bug-report", action);

    eventd_event_add_data(event, g_strdup("id"), g_variant_new_uint64(id));
    _git_eventc_event_add_data_string(event, "title", title);
    _git_eventc_event_add_data_string_null(event, "author-name", author_name);
    _git_eventc_event_add_data_string_null(event, "author-email", author_email);
    _git_eventc_event_add_data_string_null(event, "author-username", author_username);
    if ( tags != NULL )
        eventd_event_add_data(event, g_strdup("tags"), tags);

    _git_eventc_send_event(event, url,  repository_name, repository_url, project);
}

void
git_eventc_send_ci_build(const gchar *action, guint64 id, const gchar *branch, guint64 duration, gchar *url, const gchar *repository_name, const gchar *repository_url, const gchar **project)
{
    EventdEvent *event;

    event = eventd_event_new("ci-build", action);

    eventd_event_add_data(event, g_strdup("id"), g_variant_new_uint64(id));
    _git_eventc_event_add_data_string(event, g_strdup("branch"), branch);
    eventd_event_add_data(event, g_strdup("duration"), g_variant_new_uint64(duration));

    _git_eventc_send_event(event, url,  repository_name, repository_url, project);
}

void
git_eventc_send_ci_build_for_merge_request(const gchar *action, guint64 id, const gchar *branch, guint64 duration, guint64 mr_id, const gchar *mr_title, gchar *mr_url, gchar *url, const gchar *repository_name, const gchar *repository_url, const gchar **project)
{
    EventdEvent *event;

    event = eventd_event_new("ci-build", action);

    eventd_event_add_data(event, g_strdup("id"), g_variant_new_uint64(id));
    _git_eventc_event_add_data_string(event, g_strdup("branch"), branch);
    eventd_event_add_data(event, g_strdup("duration"), g_variant_new_uint64(duration));

    eventd_event_add_data(event, g_strdup("mr-id"), g_variant_new_uint64(mr_id));
    _git_eventc_event_add_data_string(event, g_strdup("mr-title"), mr_title);
    _git_eventc_event_take_data_string(event, g_strdup("mr-url"), mr_url);

    _git_eventc_send_event(event, url,  repository_name, repository_url, project);
}
