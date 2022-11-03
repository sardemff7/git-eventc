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

#ifndef __GIT_EVENTC_WEBHOOK_H__
#define __GIT_EVENTC_WEBHOOK_H__

typedef void (*GitEventcWebhookParseFunc)(GitEventcEventBase *base, JsonObject *root);

JsonNode *git_eventc_webhook_api_get(const GitEventcEventBase *base, const gchar *url);
GList *git_eventc_webhook_node_list_to_string_list(GList *list);

#define json_get_string_safe(object, member) (( ( object != NULL ) && json_object_has_member(object, member) ) ? json_object_get_string_member(object, member) : NULL)
#define json_get_string_gvariant_safe(object, member) (( ( object != NULL ) && json_object_has_member(object, member) ) ? g_variant_new_string(json_object_get_string_member(object, member)) : NULL)

#endif /* __GIT_EVENTC_WEBHOOK_GITHUB_H__ */
