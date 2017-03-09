git-eventc
==========

git-eventc is a small project aiming to bridge Git repositories to eventd for commit notification.
<br />
This project is only useful in a working eventd environment.
Most people will need the eventd `im` plugin to act as an IRC commit bot.


Events
------

git-eventc will provide events in the `scm` event group: `commit`, `commit-group`, `branch-created`, `branch-deleted`.
<br />
Here is the list of common data provided by both events:

* `url`: An URL to see the change online
* `repository-name`: The name of the repository
* `branch`: The updated branch name
* `project-group`: The project group name (if set)
* `project`: The project name, defaults to `repository-name`


### `commit`

This event correspond to a single commit.
<br />
Here is the list of provided data:

* `id`: The commit id (short version, see `--help`)
* `message`: The commit message (first line)
* `author-name`: The name of the author
* `author-email`: The email of the author
* `author-username`: The username of the author (if available)
* `files`: The list (as a string) of modified files, with some basic prefix detection
    <br />
    The `post-receive` hook also detects file renames and copies if asked so.


### `commit-group`

This event correspond to a group of commit.
It will be generated if a push is adding a number of commits above a specified threshold (see `--help`).
<br />
Here is the list of provided data:

* `pusher-name`: The name of the pusher
* `size`: The number of commits in this push


### `branch-created` and `branch-deleted`

This event correspond to the creation/deletion of a branch.
<br />
Here is the list of provided data:

* `pusher-name`: The name of the pusher
* `url`: The `branch-deleted` event will *not* have an URL


### Example event file

(See eventd configuration for further information.)

For a `commit` event:

    [Event]
    Category = scm
    Name = commit
    [IMAccount freenode]
    Message = ${project-group}/^B${project}^O/^C07${branch}^O: ^C03${author-name}^O * ${id}: ${message} ^C05${url}^O ^C14${files}^0
    Channels = #test;

For a `commit-group` event:

    [Event]
    Category = scm
    Name = commit-group
    [IMAccount freenode]
    Message = ${project-group}/^B${project}^O/^C07${branch}^O: ^C03${pusher-name}^O pushed ${size} commits ^C05${url}^O
    Channels = #test;

For a `branch-created` event:

    [Event]
    Category = scm
    Name = branch-created
    [IMAccount freenode]
    Message = ${project-group}/^B${project}^O/^C07${branch}^O: ^C03${pusher-name}^O branch created ^C05${url}^O
    Channels = #test;

For a `branch-deleted` event:

    [Event]
    Category = scm
    Name = branch-deleted
    [IMAccount freenode]
    Message = ${project-group}/^B${project}^O/^C07${branch}^O: ^C03${pusher-name}^O branch deleted ^C05${url}^O
    Channels = #test;



Executables
-----------


You can specify configuration either directly on the command-line, or in a file `~/.config/git-eventc.conf`, in the `key=value` format.
All keys must be in a `[git-eventc]` group and use the same name as their command-line argument.

### git-eventc-post-receive

git-eventc-post-receive is a Git post-receive hook.
See `--help` output for basic configuration.
<br />
You can use it directly as a post-receive hook or in a wrapper script. Please make sure `stdin` is fed correctly.

It will use configuration directly from Git.
You should configure most of them in your system configuration (`/etc/gitconfig`).
<br />
Here is the list of used values:

* `git-eventc.project-group`: used as `project-group`
* `git-eventc.project`: used as `project`, defaults to `repository-name`
* `git-eventc.branch-url`: URL template for a branch with two C-style string conversion specifier (`%s`):
    * the first one for the repository name
    * the second one for the branch name
    * Examples: `http://cgit.example.com/%s/log/?h=%s` or `http://gitweb.example.com/?p=%s.git;a=shortlog;h=refs/heads/%s`
* `git-eventc.commit-url`: URL template for a single commit with two C-style string conversion specifier (`%s`):
    * the first one for the repository name
    * the second one for the commit id
    * Examples: `http://cgit.example.com/%s/commit/?id=%s` or `http://gitweb.example.com/?p=%s.git;a=commitdiff;h=%s`
* `git-eventc.diff-url`: URL template for a diff between two commits with three C-style string conversion specifier (`%s`):
    * the first one for the repository name
    * the second and third ones for the commit ids
    * Examples: `http://cgit.example.com/%s/diff/?id2=%s&id=%s` or `http://gitweb.example.com/?p=%s.git;a=commitdiff;hp=%s;h=%s`
* `git-eventc.repository`: used as `repository-name` (not meaningful in system configuration)

It also has some basic support for Gitolite environment variables:

* `GL_USER`: used as `pusher-name`
* `GL_REPO`: used as `repository-name`


### git-eventc-webhook

git-eventc-webhook is a tiny daemon that will listen HTTP POST based hook.
These are provided by many Git host providers.
<br />
See `--help` output for its configuration.

Just run it or your server and point the WebHook to it.
You can use the proxy support of your favorite web server if you prefer.
<br />
Direct TLS/SSL support is avaible.

git-eventc-webhook will split the URL path in two:

* first part will be used as `project-group`
* second part (may contain slashes) will be used as `project`
    <br />
    The second part is optional and will default to `repository-name`

Here is the list of supported services.

* GitHub

Example URLs:

    http://example.com:8080/TestProjectGroup
    https://example.com:8080/TestProjectGroup/TestProject
    https://example.com/webhook/TestProjectGroup/TestProject (behind Apache ProxyPass)

#### Secrets

git-eventc-webhook has secret support. In your GitHub WebHook configuration, you can specify a secret.
This secret will be used to compute a signature of the hook payload, which is sent in the request header.
git-eventc-webhook will compute the signature and compare it with the one in the request.

To specify secrets, you must use a configuration file. Here is the format:

* The group name is `[webhook-secrets]`.
* Each key is a project group name
* Each value is the corresponding secret

Example:

    [webhook-secrets]
    Group1=secret
    Group2=secret
    Group3=other-secret
