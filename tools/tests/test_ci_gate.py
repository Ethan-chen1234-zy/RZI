# SPDX-FileCopyrightText: Copyright (c) 2026 RAKwireless
# SPDX-License-Identifier: Apache-2.0
"""Exercise the actual aggregate-gate program embedded in the CI workflow."""

import json
import os
import subprocess
import sys
import textwrap
import unittest
from pathlib import Path


class AggregateGateTests(unittest.TestCase):
    def run_gate(self, results):
        workflow = Path(__file__).parents[2] / ".github/workflows/ci.yml"
        job = workflow.read_text().split("\n  required:\n", 1)[1]
        program = job.split("python3 - <<'PY'\n", 1)[1].split("\n          PY", 1)[0]
        return subprocess.run(
            [sys.executable, "-c", textwrap.dedent(program)],
            env={**os.environ, "RESULTS": json.dumps(results)},
            capture_output=True,
            text=True,
            check=False,
        ).returncode

    def test_all_success(self):
        self.assertEqual(self.run_gate({"build": {"result": "success"}}), 0)

    def test_failure_cancellation_and_skip_block_merge(self):
        for result in ["failure", "cancelled", "skipped"]:
            with self.subTest(result=result):
                jobs = {"build": {"result": "success"}, "tests": {"result": result}}
                self.assertNotEqual(self.run_gate(jobs), 0)

    def test_missing_results_fail_closed(self):
        self.assertNotEqual(self.run_gate({}), 0)
        self.assertNotEqual(self.run_gate({"tests": {}}), 0)


if __name__ == "__main__":
    unittest.main()
