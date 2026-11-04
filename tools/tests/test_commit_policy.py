# SPDX-FileCopyrightText: Copyright (c) 2026 RAKwireless
# SPDX-License-Identifier: Apache-2.0
"""Regression tests for the merge gate, using isolated Git repositories."""

import contextlib
import importlib.util
import io
import os
import subprocess
import tempfile
import unittest
from pathlib import Path

SPEC = importlib.util.spec_from_file_location(
    "commit_policy", Path(__file__).parents[1] / "ci" / "check-commits.py"
)
POLICY = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(POLICY)
AUTHOR = "Test Author <author@example.com>"


def message(title="fix(at): preserve settings", signoff=AUTHOR):
    return (
        f"{title}\n\nPreserve runtime state when persistence fails.\n\nSigned-off-by: {signoff}\n"
    )


class CommitPolicyTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.cwd = Path.cwd()
        os.chdir(self.temp.name)
        self.addCleanup(os.chdir, self.cwd)
        self.addCleanup(self.temp.cleanup)
        self.git("init", "-q", "-b", "main")
        self.git("config", "user.name", "Test Author")
        self.git("config", "user.email", "author@example.com")
        self.git("config", "commit.gpgsign", "false")

    def git(self, *args):
        return subprocess.check_output(["git", *args], text=True).strip()

    def commit(self, text=None):
        self.git(
            "-c", "core.hooksPath=/dev/null", "commit", "--allow-empty", "-qm", text or message()
        )
        return self.git("rev-parse", "HEAD")

    def check(self, revision_range, **kwargs):
        with contextlib.redirect_stdout(io.StringIO()):
            return POLICY.check_range(revision_range, **kwargs)

    def test_valid_commit(self):
        self.assertEqual(POLICY.message_errors(message(), AUTHOR), [])

    def test_dco_must_match_author_and_be_a_trailer(self):
        self.assertTrue(POLICY.message_errors(message(signoff="Other <other@example.com>"), AUTHOR))
        text = message() + "\nThis paragraph makes the sign-off cease to be a trailer.\n"
        self.assertTrue(POLICY.message_errors(text, AUTHOR))

    def test_body_must_explain_change(self):
        text = f"fix(at): preserve settings\n\nSigned-off-by: {AUTHOR}\n"
        self.assertTrue(POLICY.message_errors(text, AUTHOR))

    def test_reject_fixup_and_nonconventional_titles(self):
        for title in ["fixup! fix(at): preserve settings", "WIP: settings", "Initial commit"]:
            with self.subTest(title=title):
                self.assertTrue(POLICY.message_errors(message(title), AUTHOR))

    def test_body_length_exceptions(self):
        long_author = f"{'Long Name ' * 12}<author@example.com>"
        text = message(signoff=long_author).replace(
            "Preserve runtime state when persistence fails.", "https://example.com/" + "a" * 100
        )
        self.assertEqual(POLICY.message_errors(text, long_author), [])
        self.assertTrue(
            POLICY.message_errors(message().replace("Preserve runtime", "x" * 80), AUTHOR)
        )

    def test_root_commit_has_no_parent_requirement(self):
        self.commit()
        self.assertEqual(self.check("HEAD^!"), 0)

    def test_checks_every_commit_in_range(self):
        base = self.commit()
        self.commit(message(signoff="Other <other@example.com>"))
        self.commit()
        self.assertEqual(self.check(f"{base}..HEAD"), 1)

    def test_merge_rejected_but_queue_integration_allowed(self):
        base = self.commit()
        self.git("switch", "-qc", "feature")
        self.commit(message("feat(at): add settings query"))
        self.git("switch", "-q", "main")
        self.commit()
        self.git(
            "-c", "core.hooksPath=/dev/null", "merge", "--no-ff", "feature", "-qm", "Merge feature"
        )
        self.assertEqual(self.check(f"{base}..HEAD"), 1)
        self.assertEqual(self.check(f"{base}..HEAD", allow_merges=True), 0)

    def test_empty_or_invalid_range_fails(self):
        tip = self.commit()
        with self.assertRaises(ValueError):
            self.check(f"{tip}..{tip}")

    def test_event_ranges(self):
        base, head = "a" * 40, "b" * 40
        pr = {"pull_request": {"base": {"sha": base}, "head": {"sha": head}}}
        self.assertEqual(POLICY.event_range(pr), (f"{base}..{head}", False))
        group = {"merge_group": {"base_sha": base, "head_sha": head}}
        self.assertEqual(POLICY.event_range(group), (f"{base}..{head}", True))
        push = {"ref": "refs/heads/main", "before": base, "after": head}
        self.assertEqual(POLICY.event_range(push), (f"{base}..{head}", False))
        push["before"] = "0" * 40
        self.assertEqual(POLICY.event_range(push), (head, False))
        push["ref"] = "refs/tags/v0.2.0"
        self.assertEqual(POLICY.event_range(push), ("HEAD^!", False))
        self.assertEqual(POLICY.event_range({}), ("HEAD^!", False))

    def test_untrusted_event_ref_cannot_become_a_git_option(self):
        with self.assertRaises(ValueError):
            POLICY.event_range(
                {"pull_request": {"base": {"sha": "--all"}, "head": {"sha": "b" * 40}}}
            )


if __name__ == "__main__":
    unittest.main()
