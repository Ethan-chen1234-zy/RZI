# SPDX-FileCopyrightText: Copyright (c) 2026 RAKwireless
# SPDX-License-Identifier: Apache-2.0
"""Protect the boundary between the maintainer review exception and CI."""

import json
import unittest
from pathlib import Path

RULESETS = Path(__file__).parents[2] / ".github/rulesets"


def load(name):
    return json.loads((RULESETS / name).read_text())


class RulesetTests(unittest.TestCase):
    def test_maintainer_cannot_bypass_ci_or_branch_protection(self):
        baseline = load("main.json")
        self.assertEqual(baseline["enforcement"], "active")
        self.assertEqual(baseline["bypass_actors"], [])
        rules = {rule["type"]: rule.get("parameters", {}) for rule in baseline["rules"]}
        for rule in ["deletion", "non_fast_forward", "pull_request"]:
            self.assertIn(rule, rules)
        self.assertNotIn("required_linear_history", rules)
        pr = rules["pull_request"]
        self.assertEqual(pr["required_approving_review_count"], 0)
        self.assertFalse(pr["require_code_owner_review"])
        self.assertFalse(pr["require_last_push_approval"])
        self.assertTrue(pr["required_review_thread_resolution"])
        self.assertEqual(pr["allowed_merge_methods"], ["merge", "rebase", "squash"])
        checks = rules["required_status_checks"]
        self.assertTrue(checks["strict_required_status_checks_policy"])
        self.assertIn(
            {"context": "CI required", "integration_id": 15368}, checks["required_status_checks"]
        )

    def test_review_exception_is_limited_to_one_user_and_pr_merges(self):
        reviews = load("reviews.json")
        self.assertEqual(reviews["enforcement"], "active")
        self.assertEqual(reviews["conditions"], load("main.json")["conditions"])
        self.assertEqual(
            reviews["bypass_actors"],
            [{"actor_id": 144674500, "actor_type": "User", "bypass_mode": "pull_request"}],
        )
        self.assertEqual(len(reviews["rules"]), 1)
        self.assertEqual(reviews["rules"][0]["type"], "pull_request")
        params = reviews["rules"][0]["parameters"]
        self.assertEqual(params["required_approving_review_count"], 1)
        self.assertTrue(params["require_code_owner_review"])
        self.assertTrue(params["require_last_push_approval"])
        self.assertTrue(params["dismiss_stale_reviews_on_push"])
        self.assertEqual(params["allowed_merge_methods"], ["merge", "rebase", "squash"])

    def test_release_tags_have_no_review_bypass(self):
        tags = load("release-tags.json")
        self.assertEqual(tags["enforcement"], "active")
        self.assertEqual(tags["bypass_actors"], [])
        self.assertEqual({rule["type"] for rule in tags["rules"]}, {"deletion", "update"})


if __name__ == "__main__":
    unittest.main()
