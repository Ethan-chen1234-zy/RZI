#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright (c) 2026 RAKwireless
# SPDX-License-Identifier: Apache-2.0
"""Validate RZI commit messages, DCO trailers, and PR topology."""

import argparse
import json
import re
import subprocess
from pathlib import Path


def git(*args, input_text=None):
    """Run Git without evaluating commit or event content as shell code."""
    return subprocess.check_output(
        ["git", *args], input=input_text, text=True, encoding="utf-8"
    ).strip()


def sha(value):
    if not isinstance(value, str) or not re.fullmatch(r"[0-9a-f]{40,64}", value):
        raise ValueError("event contains an invalid commit SHA")
    return value


def event_range(event):
    """Return original commit range; builds separately test the merge result."""
    if "pull_request" in event:
        pr = event["pull_request"]
        return f"{sha(pr['base']['sha'])}..{sha(pr['head']['sha'])}", False
    if "merge_group" in event:
        group = event["merge_group"]
        return f"{sha(group['base_sha'])}..{sha(group['head_sha'])}", True
    after = event.get("after")
    before = event.get("before")
    # A new branch has no previous tip; check all its commits, including root.
    # Tags and manual/reusable invocations validate their checked-out commit.
    if after and not event.get("ref", "").startswith("refs/tags/"):
        head = sha(after)
        if before and set(before) != {"0"}:
            return f"{sha(before)}..{head}", False
        return head, False
    return "HEAD^!", False


def message_errors(message, author):
    """Check the same message conventions for local use and CI."""
    errors = []
    lines = message.splitlines()
    title = lines[0] if lines else ""
    if not re.fullmatch(
        r"(?:feat|fix|docs|style|refactor|perf|test|build|ci|chore|revert)"
        r"(?:\([a-z0-9_./-]+\))?!?: [a-z0-9].*[^.]",
        title,
    ):
        errors.append("use type(scope): subject, lowercase initial, no final period")
    if len(title) > 75:
        errors.append("title exceeds 75 characters")
    if len(lines) < 2 or lines[1].strip():
        errors.append("separate the title and explanatory body with a blank line")

    trailers = git("interpret-trailers", "--parse", input_text=message).splitlines()
    signoffs = [
        line.partition(":")[2].strip().casefold()
        for line in trailers
        if line.casefold().startswith("signed-off-by:")
    ]
    if author.casefold() not in signoffs:
        errors.append("the author must have a matching Signed-off-by trailer")
    body = [line for line in lines[2:] if line.strip() and line not in trailers]
    if not body:
        errors.append("include an explanatory body beyond attribution trailers")
    for number, line in enumerate(lines[2:], 3):
        if len(line) > 75 and not (
            re.match(r"(?:Signed-off-by|Co-authored-by):", line, re.IGNORECASE)
            or re.search(r"https?://", line)
        ):
            errors.append(f"body line {number} exceeds 75 characters")
    if any(re.match(r"\s*Change-Id:", line, re.IGNORECASE) for line in lines):
        errors.append("remove Gerrit Change-Id trailers")
    return errors


def check_range(revision_range, allow_merges=False):
    commits = git("rev-list", "--reverse", revision_range).splitlines()
    if not commits:
        raise ValueError("the commit range is empty")
    failed = False
    for commit in commits:
        parents = git("show", "-s", "--format=%P", commit).split()
        if len(parents) > 1:
            if allow_merges:
                # GitHub generates integration commits for merge queues.
                # Original PR topology was checked before queue admission.
                continue
            errors = ["merge commits are forbidden; rebase the feature branch"]
        else:
            message = git("show", "-s", "--format=%B", commit)
            author = git("show", "-s", "--format=%an <%ae>", commit)
            errors = message_errors(message, author)
        for error in errors:
            print(f"{commit[:12]}: {error}")
            failed = True
    if not failed:
        print(f"Commit policy passed ({len(commits)} commits).")
    return int(failed)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("revision_range", nargs="?", help="e.g. upstream/main..HEAD")
    parser.add_argument("--event", type=Path, help="GitHub event JSON")
    parser.add_argument("--range-only", action="store_true")
    parser.add_argument("--message", type=Path, help="validate a proposed commit message")
    args = parser.parse_args()
    try:
        if args.message:
            author = git("var", "GIT_AUTHOR_IDENT").rsplit(" ", 2)[0]
            errors = message_errors(args.message.read_text(), author)
            for error in errors:
                print(error)
            return int(bool(errors))
        if args.event:
            revision_range, allow_merges = event_range(json.loads(args.event.read_text()))
        else:
            revision_range, allow_merges = args.revision_range or "HEAD^!", False
        if args.range_only:
            print(revision_range)
            return 0
        return check_range(revision_range, allow_merges)
    except (ValueError, KeyError, subprocess.CalledProcessError) as exc:
        parser.exit(2, f"Commit policy could not run: {exc}\n")


if __name__ == "__main__":
    raise SystemExit(main())
