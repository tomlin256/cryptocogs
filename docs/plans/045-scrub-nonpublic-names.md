# Plan 045 — No names or text of non-public projects in this repository, and an audit of every push

**Status:** Proposed (2026-11-07). Waiting for approval.

Closes [#61](https://github.com/tomlin256/cryptocogs/issues/61).

## Problem

This repository is public and stands alone: its code, comments, docs, issues and commit messages describe the library only (`CLAUDE.md`).
At `1476faf`, 93 lines in 18 tracked files name a project that is not public, quote its issue numbers, point at its files or describe its code:

- six files that are not docs (workflow, CMake and C++ comments, two sanitizer canaries, one check script's header);
- twelve plan docs.

The list of files is kept privately, with the audit that finds them. Nothing audits a push, so nothing stopped any of it.

The same names are also in git history, in the text of a few public issues and issue comments, and nowhere else that was checked (release
notes, tag messages and branch names: none). Those are not fixed here (*Not here*).

## The audit

A list of forbidden names is itself a leak, so neither the list nor a check that reads it can live in this repository. The audit is a
`pre-push` hook of the author's checkout (`.git/hooks/pre-push`, never committed) that reads its list from outside the repository. For each
pushed ref it scans every text file of the commit and the message of each new commit, and it refuses the push on a finding. It is built and
tested where its list lives, so this repository gets no code and no test for it.

Rejected: a CI step. It would need the list as a repository secret, and it would run after the text is already public.

This repository cites its own issues as `#N` throughout (636 times), so a bare number cannot be told from a foreign one; the audit looks for
names, not for bare numbers.

## Decisions

- **D1. The twelve plan docs.** (a) Scrub them (recommended). (b) Leave them and exempt them from the audit. Their history is public and
  stays, because a rewrite needs a force-push, which is not done. A scrub cannot recall what was published, but it makes the tree obey the
  rule and lets the audit pass with no exemptions. (b) turns the exemption list into a list of known leaks and leaves prose that describes
  another project's code in a public repository.

## Steps

A **checkpoint** is `cmake --preset debug-macos-arm64 && cmake --build --preset debug-macos-arm64 && ctest --preset debug-macos-arm64`, all
passing, and the audit reporting nothing for the files touched. Comments and prose do not differ between compilers, so Linux and the sanitizer
trees are left to CI, which the push starts and which is read in passing.

**Tests.** None are written: no code changes. The full suite is what shows that no test pins an edited comment (`test_platform_name` runs
one of the six files and includes another).

| Step | Does | Done when |
|---|---|---|
| 0 | This plan committed and pushed; a comment on #61 gives its path. | the comment is posted |
| 1 | The six non-doc files the audit reports. Each comment is rewritten to say what is true of this repository now: a pointer to another project's copy of a rule becomes a pointer to the public copy, or goes; an attribution becomes the fact it attributed. | audit clean for the six; checkpoint green |
| 2 | The plan docs that describe another project's code, as the audit reports them (five of the twelve). | audit clean for them; checkpoint green |
| 3 | The remaining seven plan docs (design-source lines, issue and plan references). | audit clean for the whole tree; checkpoint green |
| 4 | The hook is shown to refuse a seeded name and accept a clean push in a scratch clone, then installed in this checkout. | the push of this step's commit passes it, and `git push --dry-run` of a scratch commit with a seeded name is refused |
| 5 | The *Outcome* below is written; #61 is closed by hand after its closing comment. | #61 closed |

**Rules for the plan docs** (Steps 2 and 3):

- Every decision, measurement, number and test result about this repository stays, word for word.
- A name becomes a neutral noun ("another project's", "an existing recipe") only where the reader needs to know a source existed; otherwise
  the clause goes.
- Another project's issue and plan references (links, numbers, file paths, type names) go; what the sentence says about this repository stays.
- A sentence that only describes another project's code, build or behaviour goes, and so does a section that is only that.
- The diff of each file is read before the commit, because the audit finds names and cannot find a description that has none.

## Self-review

- **A rewrite changes a fact.** The rules above keep every number; the per-file diff is read, and the checkpoint runs the suite.
- **A reminder is lost.** Two of the six files say a table is kept in step with copies elsewhere. Afterwards they name only the public
  copy. The pointer from the other side stays where that copy lives.
- **The audit finds listed names only.** A description of another project's behaviour with no name in it passes, which is why Steps 2 and 3
  are read, not just audited.
- **The hook is per checkout.** A clone without it is not audited, and `git push --no-verify` skips it. Neither is blocked.
- **History is not recalled.** The old text stays in 26 lines of commit messages (the history is 460 commits), because the fix would need a
  force-push.

## Not here

- The text of the public issues and issue comments that carry the same names. GitHub keeps the edit history of an issue visible to every
  reader, so an edit does not recall the text; whether to edit, or to delete the comments, is a separate decision, tracked privately.
- Rewriting history.

## Progress

| Step | State | Commit |
|---|---|---|
| 0 | | |
| 1 | | |
| 2 | | |
| 3 | | |
| 4 | | |
| 5 | | |

## Outcome

(written at Step 5)
