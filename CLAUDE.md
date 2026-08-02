# CLAUDE.md

## Write from the reader's state

Everything you write here outlives the context that produced it, and the
reader gets only what their artifact carries: a comment's reader sees the
file at one commit; a commit message's reader sees the commit and its
parent, so the change itself is visible but nothing else is. Neither
reader has the conversation, the review thread, or the session that
produced the text. Reference only what your reader can see; if a
referent lives anywhere else, restate it or drop it. This applies to
examples too: an example must exhibit its pattern within its own text,
not point at an instance the reader cannot see. The rules below are
applications of this one.

## Code comments

Comments justify the current state of the code; commit messages justify the
change. Write every comment for a reader who has only ever seen this version
of the file: it should read the same whether the line was written today or
ten years ago. Never reference a value or approach that exists only in the
version you replaced ("3d, not 7d", "no longer uses X"). Mentioning an
alternative is good only when it is still live — the default a maintainer
would otherwise reach for, or sibling code elsewhere in the file — and then
name the referent explicitly rather than leaving a bare "not X".

## Writing for human readers

You can read a paragraph as cheaply as a phrase; a human cannot. Comments,
commit messages, and docs are read by people scanning, so ask first: "is
this important enough to slow every future reader down?" If it is, shape it
to be scanned — conclusion in the first few words, one idea per line, each
fact on or beside the line it governs. Serialize your conclusions, not your
reasoning; the full chain belongs in the commit message.

State the important thing directly, once. Two sentences that partly
restate each other mean you have not yet found the underlying fact — name
it, and the restatements either follow from it or vanish ("entries are
refreshed by builds" plus a "but skip failed builds" caveat collapse into
"successful builds refresh every entry"). Every connection left implicit
between sentences is integration work pushed onto the reader. Brevity is the symptom of
directness, not the goal: chase the direct statement and concision
follows.

## Commit messages

Use Conventional Commits (`type: short description`). Follow the 50/72 rule
as best effort — clarity wins over the character limits. Be succinct.

The subject covers everything the commit does; when no honest subject
can, split the commit. The body adds the why — never a restatement of
the subject or the diff, and never the process that produced the change
("I missed a callsite" is journey; "two callsites still assumed the old
semantics" is the fact). A subject alone is fine when the why is evident
from the diff.

## Pushing and pull requests

Do not push, and do not open a pull request, until a human has reviewed the
changes and explicitly vouched for them. Commit locally and report instead.
An approval covers only the push it was given for, not later changes.

## Pull request notes

A user-facing change needs a `Notes:` paragraph in the PR description: a
short explanation — ideally one sentence — of the difference the end user
will see. Its reader runs the app and never reads the code, so name no
function, RPC key, or other implementation detail. If there are no
user-facing changes, add the `notes:none` label.
