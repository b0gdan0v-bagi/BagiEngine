"""Tests of tools/web/deploy_status.py: the progress file of build_and_deploy.py. Run: python -m unittest discover tools/web/tests"""

import contextlib
import io
import json
import tempfile
import unittest
from pathlib import Path

import web_fixtures  # noqa: F401  (puts tools/web on sys.path)

import deploy_status

STAGES = ["fetch", "tests", "build", "package", "deploy"]
FIELDS = {"format", "target", "stages", "stage", "state", "commit", "engineCommit", "service", "address", "url", "answer", "failedTests",
          "error", "startedAt", "updatedAt"}


class StatusTest(unittest.TestCase):
    def setUp(self):
        self._temp = tempfile.TemporaryDirectory()
        self.addCleanup(self._temp.cleanup)
        self.folder = Path(self._temp.name)
        self.path = self.folder / "sub" / "status.json"
        self.ticks = 0

    def clock(self) -> str:
        self.ticks += 1
        return f"2026-01-01T00:00:{self.ticks:02d}Z"

    def make(self, stages=STAGES, path="default") -> deploy_status.Status:
        return deploy_status.Status(self.path if path == "default" else path, "mahjong-editor", stages, "studio.example.dev", "studio-mahjong",
                                    clock=self.clock)

    def read(self) -> dict:
        return json.loads(self.path.read_text(encoding="utf-8"))

    def test_the_file_holds_exactly_the_documented_fields_from_the_start(self):
        self.make()
        data = self.read()
        self.assertEqual(set(data), FIELDS)
        self.assertEqual((data["format"], data["target"], data["stages"], data["stage"], data["state"]),
                         (1, "mahjong-editor", STAGES, None, "running"))
        self.assertEqual((data["service"], data["address"], data["url"]), ("studio-mahjong", "studio.example.dev", "https://studio.example.dev"))
        self.assertEqual((data["commit"], data["engineCommit"], data["failedTests"], data["error"]), (None, None, [], None))
        self.assertIsNone(data["answer"])

    def test_each_stage_and_field_is_written_at_once(self):
        status = self.make()
        status.enter("fetch")
        self.assertEqual((self.read()["stage"], self.read()["state"]), ("fetch", "running"))
        status.set(commit="a" * 40, engineCommit="b" * 40)
        self.assertEqual((self.read()["commit"], self.read()["engineCommit"]), ("a" * 40, "b" * 40))
        status.enter("build")
        self.assertEqual(self.read()["stage"], "build")
        status.set(failedTests=["A", "B"])
        self.assertEqual(self.read()["failedTests"], ["A", "B"])

    def test_a_page_of_a_site_has_a_url_of_its_own_no_service_and_keeps_the_answer(self):
        status = deploy_status.Status(self.path, "engine-demo", STAGES, "engine.example.dev", None, clock=self.clock,
                                      url="https://engine.example.dev/demo/")
        status.enter("deploy")
        status.set(answer={"page": "demo", "bytes": 5})
        status.done()
        data = self.read()
        self.assertEqual((data["service"], data["address"], data["url"]), (None, "engine.example.dev", "https://engine.example.dev/demo/"))
        self.assertEqual((data["answer"], data["state"], data["format"]), ({"page": "demo", "bytes": 5}, "done", 1))

    def test_done_marks_the_last_stage_done_and_the_times_move(self):
        status = self.make()
        status.enter("deploy")
        started = self.read()["startedAt"]
        status.done()
        data = self.read()
        self.assertEqual((data["stage"], data["state"], data["error"]), ("deploy", "done", None))
        self.assertEqual(data["startedAt"], started)
        self.assertGreater(data["updatedAt"], started)

    def test_a_failure_keeps_the_stage_and_makes_the_error_one_line(self):
        status = self.make()
        status.enter("tests")
        status.fail("alltests is red\n  (failed: A,\tB)")
        data = self.read()
        self.assertEqual((data["stage"], data["state"], data["error"]), ("tests", "failed", "alltests is red (failed: A, B)"))

    def test_a_failure_before_any_stage_is_in_the_first_stage(self):
        status = self.make(["build", "package"])
        status.fail("no sdk")
        self.assertEqual((self.read()["stage"], self.read()["state"]), ("build", "failed"))

    def test_the_replace_leaves_no_temporary_file(self):
        status = self.make()
        status.enter("build")
        status.done()
        self.assertEqual([entry.name for entry in self.path.parent.iterdir()], ["status.json"])

    def test_without_a_path_nothing_is_written(self):
        status = self.make(path=None)
        status.enter("build")
        status.fail("x")
        self.assertEqual(list(self.folder.iterdir()), [])

    def test_a_stage_or_field_that_does_not_exist_is_a_programming_error(self):
        status = self.make(["build"])
        with self.assertRaises(ValueError):
            status.enter("deploy")
        with self.assertRaises(ValueError):
            status.set(error="x")
        with self.assertRaises(ValueError):
            self.make(["nonsense"])

    def test_no_address_gives_no_url(self):
        deploy_status.Status(self.path, None, ["build"], None, "bagi-web", clock=self.clock)
        data = self.read()
        self.assertEqual((data["target"], data["address"], data["url"]), (None, None, None))

    def test_an_unwritable_path_is_reported_once_and_does_not_raise(self):
        blocker = self.folder / "file"
        blocker.write_text("x", encoding="utf-8")
        err = io.StringIO()
        with contextlib.redirect_stderr(err):
            status = deploy_status.Status(blocker / "status.json", None, ["build"], None, None, clock=self.clock)
            status.enter("build")
            status.done()
        self.assertEqual(err.getvalue().count("cannot write the status file"), 1)


if __name__ == "__main__":
    unittest.main()
