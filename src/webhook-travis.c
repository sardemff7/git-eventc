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
#include <errno.h>

#include <glib.h>
#include <glib-object.h>

#include <json-glib/json-glib.h>

#include "libgit-eventc.h"
#include "webhook-travis.h"

void
git_eventc_webhook_payload_parse_travis(const gchar **project, JsonObject *root)
{
    const gchar *action = json_object_get_string_member(root, "state");

    const gchar *number_str = json_object_get_string_member(root, "number");
    gchar *e;
    guint64 number;
    JsonObject *repository = json_object_get_object_member(root, "repository");

    const gchar *branch = json_object_get_string_member(root, "branch");
    guint64 duration = json_object_get_int_member(root, "duration");
    const gchar *repository_name = json_object_get_string_member(repository, "name");
    const gchar *repository_url = json_object_get_string_member(repository, "url");

    errno = 0;
    number = g_ascii_strtoull(number_str, &e, 10);
    if ( ( number_str == e ) || ( *e != '\0' ) || ( errno != 0 ) )
        return;

    gchar *url;
    url = git_eventc_get_url_const(json_object_get_string_member(root, "build_url"));

    if ( json_object_get_boolean_member(root, "pull_request") )
        git_eventc_send_ci_build_for_pull_request(action, number, branch, duration,
            json_object_get_int_member(root, "pull_request_number"),
            json_object_get_string_member(root, "pull_request_title"),
            git_eventc_get_url_const(json_object_get_string_member(root, "compare_url")),
            url, repository_name, repository_url, project);
    else
        git_eventc_send_ci_build(action, number, branch, duration, url, repository_name, repository_url, project);
}
