git-eventc
==========

git-eventc is a small project aiming to bridge Git repositories to eventd for commit notification.
<br />
This project is only useful in a working eventd environment.
Most people will need the eventd `im` plugin to act as an IRC commit bot.


Events
------

git-eventc will provide two events in the `scm` event group: `commit` and `commit-group`.
<br />
Here is the list of common data provided by both events:

* `url`: An URL to see the change online
* `repository-name`: The name of the repository
* `branch`: The updated branch name
* `project`: The project name (if set)


### `commit`

This event correspond to a single commit.
<br />
Here is the list of provided data:

* `id`: The commit id (short version, see `--help`)
* `message`: The commit message (first line)
* `author-name`: The name of the author
* `author-email`: The email of the author
* `author-username`: The username of the author (if available)


### `commit-group`

This event correspond to a group of commit.
It will be generated if a push is adding a number of commits above a specified threshold (see `--help`).
<br />
Here is the list of provided data:

* `pusher-name`: The name of the pusher
* `size`: The number of commits in this push

### Example event file

(See eventd configuration for further information.)

For a `commit` event:

    [Event]
    Category = scm
    Name = commit
    [IMAccount freenode]
    Message = ^B${project}^O: ^C03${author-name}^O ${repository-name}:^C07${branch}^O * ${id}: ${message} ^C05${url}^O
    Channels = #test;

For a `commit-group` event:

    [Event]
    Category = scm
    Name = commit-group
    [IMAccount freenode]
    Message = ^B${project}^O: ^C03${pusher-name}^O pushed ${size} commits to ${repository-name}:^C07${branch}^O ^C05${url}^O
    Channels = #test;



Executables
-----------


### git-eventc-post-receive

git-eventc-post-receive is a Git post-receive hook.
See `--help` output for basic configuration.
<br />
You can use it directly as a post-receive hook or in a wrapper script. Please make sure `stdin` is fed correctly.

It will use configuration directly from Git.
You should configure most of them in your system configuration (`/etc/gitconfig`).
<br />
Here is the list of used values:

* `git-eventc.project`: used as `project`
* `git-eventc.commit-url`: URL template for a single commit with two C-style string conversion specifier (`%s`):
    * the first one for the repository name
    * the second one for the commit id
    * Examples: `http://cgit.example.com/%s/commit/?id=%s` or `http://gitweb.example.com/?p=%s.git;a=commitdiff;h=%s`
* `git-eventc.diff-url`: URL template for a diff between two commits with three C-style string conversion specifier (`%s`):
    * the first one for the repository name
    * the second and third ones for the commit ids
    * Examples: `http://cgit.example.com/%s/diff/?id2=%s&id=%s` or `http://gitweb.example.com/?p=%s.git;a=commitdiff;hp=%s;h=%s`
* `git-eventc.repository`: used as `repository-name` (not meaningful in system configuration)

It alse has some basic support for Gitolite environment variables:

* `GL_USER`: used as `pusher-name`
* `GL_REPO`: used as `repository-name`


### git-eventc-webhook

git-eventc-webhook is a tiny daemon that will listen HTTP POST based hook.
These are provided by many Git host providers (GitHub, Gitorious).
<br />
See `--help` output for its configuration.

Just run it or your server and point the WebHook to it.
You can use the proxy support of your favorite web server if you prefer.
<br />
It supports TLS/SSL directly.

git-eventc-webhook supports several URL parameters:

* `project`: the project name, used as `project`
* `token`: the “security” token (see `--help`)
* `service`: needed for some services (see below)

Here is the list of supported services.
Some services will require you to add a `service` URL parameter (value indicated in parens), others are automatically detected.

* GitHub
* Gitorious (`gitorious`)

Example URLs:

    http://example.com:8080/?project=TestProject&token=superSecure
    https://example.com:8080/?project=TestProject&token=superSecure
    https://example.com/webhook?project=TestProject&token=superSecure (behind Apache ProxyPass)
