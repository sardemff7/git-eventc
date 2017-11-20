git-eventc
==========

git-eventc is a small project aiming to bridge Git repositories to eventd for commit notification.
<br />
This project is only useful in a working eventd environment.
Most people will need the eventd `im` plugin to act as an IRC commit bot.


Events
------

All events from git-eventc have the following common data:

* `repository-name`: The name of the repository
* `repository-url`: The URL of the repository
* `project-group`: The project group name (if set)
* `project`: The project name, defaults to `repository-name`
* `url`: An URL to see the change online (not for `-deletion` events)


### `scm` event category

git-eventc will provide events in the `scm` event category: `commit`, `commit-group`, `branch-creation`, `branch-deletion`, `tag-creation`, `tag-deletion`, `push`.
<br />
Here is the list of common data provided by all `scm` events:

* `pusher-name`: The name of the pusher
* `branch`: The updated branch name (not for `tag-` events, and the related `push` event)


#### `commit`

This event correspond to a single commit.
<br />
Here is the list of provided data:

* `id`: The commit id (short version, see `--help`)
* `subject`: The commit subject (first line of message)
* `message`: The commit message (with subject and footer tags stripped, only if not empty)
* `full-message`: The full commit message (verbatim)
* `author-name`: The name of the author
* `author-email`: The email of the author
* `author-username`: The username of the author (if available)
* `files`: The list (as a string) of modified files, with some basic prefix detection
    <br />
    The `post-receive` hook also detects file renames and copies if asked so.


#### `commit-group`

This event correspond to a group of commit.
It will be generated if a push is adding a number of commits above a specified threshold (see `--help`).
<br />
Here is the list of provided data:

* `size`: The number of commits in this push


#### `branch-creation` and `branch-deletion`

This event correspond to the creation/deletion of a branch.


#### `tag-creation` and `tag-deletion`

This event correspond to the creation/deletion of a tag.
<br />
Here is the list of additional data provided for `tag-creation`:

* `previous-tag`: The latest tag in this tag history tree
* If the tag is an annotated tag:
  * `subject`: The commit subject (first line of message)
  * `message`: The commit message (with subject and footer tags stripped, only if not empty)
  * `full-message`: The full commit message (verbatim)
  * `author-name`: The name of the author
  * `author-email`: The email of the author


#### `push`

This event correspond to a push.
It will be generated after a set of `commit` events, or any of other events events.
<br />
This event is useful for mirroring purpose.


### `bug-report` event category

git-eventc will provide events in the `bug-report` event category: `opening`, `closing`, `reopening`.
<br />
Here is the list of common data provided by all `bug-report` events:

* `id`: The id/number of the bug report
* `title`: The title of the report
* `author-name`: The name of the author
* `author-email`: The email of the author (if available)
* `author-username`: The username of the author (if available)
* `tags`: A list of tags/labels associated with the bug report (if available)


### `ci-build` event category

git-eventc will provide events in the `ci-build` event category: `success`, `failure`, `error`.
<br />
Here is the list of common data provided by all `ci-build` events:

* `id`: The id/number of the build
* `branch`: The branch the build is associated with
* `duration`: The duration of the build
* If the build is associated with a merge request:
  * `mr-id`: The id/number of the merge request
  * `mr-title`: The title of the merge request
  * `nm-url`: The URL of the merge request


### Example event file

(See eventd configuration for further information.)

For a `commit` event:

    # scm-commit.event
    [Event scm commit]
    Actions = scm-commit;

    # scm-commit.action
    [Action]
    Name = scm-commit
    [IMAccount freenode]
    Message = ${project-group}/^B${project}^O/^C07${branch}^O: ^C03${author-name}^O * ${id}: ${message} ^C05${url}^O ^C14${files}^0
    Recipients = #test;

For a `commit-group` event:

    # scm-commit-group.event
    [Event scm commit-group]
    Actions = scm-commit-group;

    # scm-commit-group.action
    [Action]
    Name = scm-commit-group
    [IMAccount freenode]
    Message = ${project-group}/^B${project}^O/^C07${branch}^O: ^C03${pusher-name}^O pushed ${size} commits ^C05${url}^O
    Recipients = #test;

For a `branch-creation` event:

    # scm-branch-creation.event
    [Event scm branch-creation]
    Actions = scm-branch-creation;

    # scm-branch-creation.action
    [Action]
    Name = scm-branch-creation
    [IMAccount freenode]
    Message = ${project-group}/^B${project}^O/^C07${branch}^O: ^C03${pusher-name}^O branch created ^C05${url}^O
    Recipients = #test;

For a `branch-deletion` event:

    # scm-branch-deletion.event
    [Event scm branch-deletion]
    Actions = scm-branch-deletion;

    # scm-branch-deletion.action
    [Action]
    Name = scm-branch-deletion
    [IMAccount freenode]
    Message = ${project-group}/^B${project}^O/^C07${branch}^O: ^C03${pusher-name}^O branch deleted ^C05${url}^O
    Recipients = #test;



Executables
-----------


You can specify configuration either directly on the command-line, or in a file `~/.config/git-eventc.conf`, in the `key=value` format.
All keys must be in a `[git-eventc]` section and use the same name as their command-line argument.

### git-eventc-post-receive

git-eventc-post-receive is a Git post-receive hook.
See `--help` output for basic configuration.
<br />
You can use it directly as a post-receive hook or in a wrapper script. Please make sure `stdin` is fed correctly.

It will use configuration directly from Git.
You should configure most of them in your system configuration (`/etc/gitconfig`).
<br />
Configuration value names are prefixed by `git-eventc.`. Here is the list of used values:

* `project-group`: used as `project-group`
* `project`: used as `project`, defaults to `repository-name`
* `repository`: used as `repository-name` (not meaningful in system configuration)
* Several URL template strings:
  all of them have the `${project-group}` and `${repository-name}` tokens.
    * `repository-url`: URL template for the repository:
        * Examples: `http://cgit.example.com/${repository-name}` or `http://gitweb.example.com/?p=${repository-name}.git`
    * `branch-url`: URL template for a branch, available token:
        * `${branch}`: the name of the branch
        * Examples: `http://cgit.example.com/${repository-name}/log/?h=${branch}` or `http://gitweb.example.com/?p=${repository-name}.git;a=shortlog;h=refs/heads/${branch}`
    * `commit-url`: URL template for a single commit, available token:
        * `${commit}`: the commit id
        * Examples: `http://cgit.example.com/${repository-name}/commit/?id=${commit}` or `http://gitweb.example.com/?p=${repository-name}.git;a=commitdiff;h=${commit}`
    * `tag-url`: URL template for a tag, available token:
        * `${tag}`: the tag name
        * Examples: `http://cgit.example.com/${repository-name}/tag/?id=${tag}` or `http://gitweb.example.com/?p=${repository-name}.git;a=tag;h=${tag}`
    * `diff-url`: URL template for a diff between two commits, available tokens:
        * `${old-commit}`: the old commit id
        * `${new-commit}`: the new commit id
        * Examples: `http://cgit.example.com/${repository-name}/diff/?id2=${old-commit}&id=${new-commit}` or `http://gitweb.example.com/?p=${repository-name}.git;a=commitdiff;hp=${old-commit};h=${new-commit}`

It also has support for Gitolite environment variables:

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
* Travis CI

Example URLs:

    http://example.com:8080/TestProjectGroup
    https://example.com:8080/TestProjectGroup/TestProject
    https://example.com/webhook/TestProjectGroup/TestProject (behind Apache ProxyPass)

#### Travis CI

git-eventc-webhook can produce events for Travis CI builds.
These events will be of the `ci-build` category, and are: `success`, `failure`, `error`.

They share the base set of data of the `scm` events.
Builds triggered from a Pull Request will have the following additional data:

* `pr-number`: The number of the Pull Request
* `pr-title`: The title of the Pull Request
* `pr-url`: The URL of the Pull Request

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
