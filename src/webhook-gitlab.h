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

#ifndef __GIT_EVENTC_WEBHOOK_GITLAB_H__
#define __GIT_EVENTC_WEBHOOK_GITLAB_H__

typedef enum {
    GIT_EVENTC_WEBHOOK_GITLAB_PARSER_PUSH,
    GIT_EVENTC_WEBHOOK_GITLAB_PARSER_TAG,
    GIT_EVENTC_WEBHOOK_GITLAB_PARSER_ISSUE,
    GIT_EVENTC_WEBHOOK_GITLAB_PARSER_MERGE_REQUEST,
    GIT_EVENTC_WEBHOOK_GITLAB_PARSER_PIPELINE,
    _GIT_EVENTC_WEBHOOK_GITLAB_PARSER_SIZE,
} GitEventcWebhookGitlabParser;

extern const gchar * const git_eventc_webhook_gitlab_parsers_events[_GIT_EVENTC_WEBHOOK_GITLAB_PARSER_SIZE];
extern const GitEventcWebhookParseFunc git_eventc_webhook_gitlab_parsers[_GIT_EVENTC_WEBHOOK_GITLAB_PARSER_SIZE];

#endif /* __GIT_EVENTC_WEBHOOK_GITLAB_H__ */
