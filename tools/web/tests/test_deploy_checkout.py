"""Tests of tools/web/deploy_checkout.py over real git repositories in a temp folder: a bare `origin`, a clone of it that plays the
owner's working copy, and an engine repository that the game carries as the submodule `engine/`. Nothing leaves the temp folder: the
git config of the machine is replaced by an empty one. Run: python -m unittest discover tools/web/tests"""

import contextlib
import io
import os
import subprocess
import tempfile
import unittest
import unittest.mock
from pathlib import Path

import web_fixtures  # noqa: F401  (puts tools/web on sys.path)

import deploy_checkout

alltests_results = deploy_checkout.alltests_results

FORBIDDEN_WORDS = {"push", "pull", "merge", "reset", "switch", "branch", "rebase", "stash", "--remote"}


def git(repo: Path, *args: str) -> str:
    return subprocess.run(["git", "-C", str(repo), *args], capture_output=True, text=True, check=True).stdout.strip()


def commit(repo: Path, name: str, content: str = "") -> str:
    path = repo / name
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(content or name, encoding="utf-8")
    git(repo, "add", name)
    git(repo, "commit", "-q", "-m", name)
    return git(repo, "rev-parse", "HEAD")


class GitRecorder:
    """Passes every git call through and keeps its arguments, so a test can say which commands were never run."""

    def __init__(self):
        self.calls: list[tuple[str, ...]] = []

    def __call__(self, repo: Path, *args: str, strip: bool = True) -> str:
        self.calls.append(args)
        return alltests_results.git(repo, *args, strip=strip)


class CheckoutTestCase(unittest.TestCase):
    def setUp(self):
        temp = tempfile.TemporaryDirectory()
        self.addCleanup(temp.cleanup)
        self.root = Path(temp.name).resolve()
        empty = self.root / "empty.gitconfig"
        empty.write_text("", encoding="utf-8")
        environment = {
            "GIT_CONFIG_GLOBAL": str(empty), "GIT_CONFIG_SYSTEM": str(empty), "GIT_CONFIG_COUNT": "1",
            "GIT_CONFIG_KEY_0": "protocol.file.allow", "GIT_CONFIG_VALUE_0": "always",
            "GIT_AUTHOR_NAME": "Tester", "GIT_AUTHOR_EMAIL": "tester@example.com",
            "GIT_COMMITTER_NAME": "Tester", "GIT_COMMITTER_EMAIL": "tester@example.com",
        }
        patcher = unittest.mock.patch.dict(os.environ, environment)
        patcher.start()
        self.addCleanup(patcher.stop)

        self.engine_origin = self.root / "engine-origin"
        self.engine_origin.mkdir()
        git(self.engine_origin, "init", "-q", "-b", "main")
        self.engine_first = commit(self.engine_origin, "tools/web/package.py")

        self.origin = self.root / "origin.git"
        subprocess.run(["git", "init", "-q", "--bare", "-b", "main", str(self.origin)], check=True)
        self.seed = self.root / "seed"
        subprocess.run(["git", "clone", "-q", str(self.origin), str(self.seed)], check=True, capture_output=True)
        git(self.seed, "checkout", "-q", "-b", "main")
        commit(self.seed, ".gitignore", "build/\n")
        git(self.seed, "submodule", "add", "-q", str(self.engine_origin), "engine")
        git(self.seed, "commit", "-q", "-m", "engine")
        self.first = commit(self.seed, "game.txt", "first")
        git(self.seed, "push", "-q", "origin", "main")

        self.project = self.root / "Game"
        subprocess.run(["git", "clone", "-q", str(self.origin), str(self.project)], check=True, capture_output=True)
        self.checkout_dir = self.root / "home" / "Game" / "checkout"

    def publish(self, name: str) -> str:
        """A new commit on origin/main, made from the seed clone: the project does not know of it until it fetches."""
        sha = commit(self.seed, name)
        git(self.seed, "push", "-q", "origin", "main")
        return sha

    def prepare(self, *, dry_run: bool = False, git=alltests_results.git) -> deploy_checkout.Checkout:
        with contextlib.redirect_stdout(io.StringIO()):
            return deploy_checkout.prepare(self.project, "main", self.checkout_dir, dry_run=dry_run, git=git)


class PrepareTest(CheckoutTestCase):
    def test_the_new_commit_of_origin_is_what_the_checkout_holds_and_it_is_detached(self):
        newest = self.publish("second.txt")
        fetched = self.prepare()
        self.assertEqual((fetched.commit, git(self.checkout_dir, "rev-parse", "HEAD")), (newest, newest))
        self.assertEqual(git(self.checkout_dir, "rev-parse", "--abbrev-ref", "HEAD"), "HEAD")
        self.assertTrue((self.checkout_dir / "second.txt").is_file())
        self.assertEqual((fetched.path, fetched.engine_dir), (self.checkout_dir, self.checkout_dir / "engine"))
        self.assertEqual(fetched.engine_commit, self.engine_first)

    def test_the_working_copy_of_the_project_is_as_before(self):
        (self.project / "game.txt").write_text("my edit", encoding="utf-8")
        (self.project / "scratch.txt").write_text("untracked", encoding="utf-8")
        git(self.project, "add", "game.txt")
        (self.project / "game.txt").write_text("my edit, then more", encoding="utf-8")
        before = (git(self.project, "status", "--porcelain"), git(self.project, "rev-parse", "main", "HEAD"), git(self.project, "diff"),
                  git(self.project, "diff", "--cached"), git(self.project, "branch", "--list"))
        self.publish("second.txt")
        self.prepare()
        after = (git(self.project, "status", "--porcelain"), git(self.project, "rev-parse", "main", "HEAD"), git(self.project, "diff"),
                 git(self.project, "diff", "--cached"), git(self.project, "branch", "--list"))
        self.assertEqual(after, before)
        self.assertEqual((self.project / "game.txt").read_text(encoding="utf-8"), "my edit, then more")
        self.assertFalse((self.project / "second.txt").exists())

    def test_the_submodule_is_at_the_commit_the_superproject_records_not_the_newest(self):
        newer = commit(self.engine_origin, "tools/web/other.py")
        fetched = self.prepare()
        self.assertNotEqual(newer, self.engine_first)
        self.assertEqual(git(self.checkout_dir / "engine", "rev-parse", "HEAD"), self.engine_first)
        self.assertEqual(fetched.engine_commit, self.engine_first)

    def test_a_second_run_moves_the_same_folder_and_keeps_ignored_files_only(self):
        self.prepare()
        (self.checkout_dir / "build").mkdir()
        (self.checkout_dir / "build" / "cache.txt").write_text("kept", encoding="utf-8")
        (self.checkout_dir / "stray.txt").write_text("removed", encoding="utf-8")
        (self.checkout_dir / "game.txt").write_text("edited", encoding="utf-8")
        newest = self.publish("second.txt")
        self.assertEqual(self.prepare().commit, newest)
        self.assertEqual(git(self.checkout_dir, "rev-parse", "HEAD"), newest)
        self.assertEqual((self.checkout_dir / "game.txt").read_text(encoding="utf-8"), "first")
        self.assertFalse((self.checkout_dir / "stray.txt").exists())
        self.assertTrue((self.checkout_dir / "build" / "cache.txt").is_file())
        self.assertEqual(git(self.project, "worktree", "list").count("\n"), 1)

    def test_a_folder_that_is_not_a_worktree_of_the_project_is_refused_and_kept(self):
        self.checkout_dir.mkdir(parents=True)
        (self.checkout_dir / "mine.txt").write_text("somebody's file", encoding="utf-8")
        with self.assertRaisesRegex(deploy_checkout.CheckoutError, "checkout"):
            self.prepare()
        self.assertEqual((self.checkout_dir / "mine.txt").read_text(encoding="utf-8"), "somebody's file")

    def test_a_worktree_of_another_repository_is_refused_and_kept(self):
        other = self.root / "other"
        subprocess.run(["git", "clone", "-q", str(self.origin), str(other)], check=True, capture_output=True)
        self.checkout_dir.parent.mkdir(parents=True)
        other.rename(self.checkout_dir)
        (self.checkout_dir / "mine.txt").write_text("work", encoding="utf-8")
        with self.assertRaisesRegex(deploy_checkout.CheckoutError, "not a worktree"):
            self.prepare()
        self.assertTrue((self.checkout_dir / "mine.txt").is_file())

    def test_the_engine_itself_is_its_own_engine_folder(self):
        self.assertEqual(deploy_checkout.engine_dir_of(self.engine_origin), self.engine_origin)
        self.assertEqual(deploy_checkout.engine_dir_of(self.project), self.project)

    def test_a_missing_branch_is_an_error_that_names_git_not_a_traceback(self):
        with self.assertRaises(deploy_checkout.CheckoutError):
            with contextlib.redirect_stdout(io.StringIO()):
                deploy_checkout.prepare(self.project, "nope", self.checkout_dir, dry_run=False)
        self.assertFalse(self.checkout_dir.exists())


class NeverRunTest(CheckoutTestCase):
    def test_no_command_pushes_pulls_merges_resets_switches_or_cleans_ignored_files(self):
        recorder = GitRecorder()
        self.prepare(git=recorder)
        self.publish("second.txt")
        self.prepare(git=recorder)
        self.assertGreater(len(recorder.calls), 10)
        for args in recorder.calls:
            with self.subTest(args=args):
                self.assertFalse(FORBIDDEN_WORDS & set(args), args)
                if "clean" in args:
                    flags = [word for word in args[args.index("clean") + 1:] if word.startswith("-")]
                    self.assertEqual(flags, ["-fdq"])

    def test_the_only_network_command_fetches_one_branch_from_origin(self):
        recorder = GitRecorder()
        self.prepare(git=recorder)
        fetches = [args for args in recorder.calls if "fetch" in args]
        self.assertEqual(fetches, [("fetch", "--quiet", "origin", "+refs/heads/main:refs/remotes/origin/main")])

    def test_a_dry_run_runs_no_git_command_and_makes_no_folder(self):
        recorder = GitRecorder()
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            fetched = deploy_checkout.prepare(self.project, "main", self.checkout_dir, dry_run=True, git=recorder)
        self.assertEqual(recorder.calls, [])
        self.assertFalse(self.checkout_dir.exists())
        self.assertIn("+ git -C", out.getvalue())
        self.assertIn("fetch --quiet origin", out.getvalue())
        self.assertIn("<origin/main>", out.getvalue())
        self.assertEqual(fetched.commit, "<origin/main>")


class HomeAndLockTest(CheckoutTestCase):
    def test_the_default_checkout_is_under_the_home_of_the_repository_and_the_variable_moves_it(self):
        with unittest.mock.patch.dict(os.environ, {deploy_checkout.HOME_VARIABLE: str(self.root / "elsewhere")}):
            self.assertEqual(deploy_checkout.default_checkout_dir(self.project), self.root / "elsewhere" / "Game" / "checkout")

    def test_a_folder_that_is_no_repository_has_no_default_checkout(self):
        with self.assertRaises(deploy_checkout.CheckoutError):
            deploy_checkout.default_checkout_dir(self.root)

    def test_the_lock_refuses_a_second_holder_and_is_released(self):
        with deploy_checkout.lock(self.checkout_dir):
            with self.assertRaisesRegex(deploy_checkout.CheckoutError, "another alltests run"):
                with deploy_checkout.lock(self.checkout_dir):
                    pass
        self.assertFalse(deploy_checkout.lock_path(self.checkout_dir).exists())
        with deploy_checkout.lock(self.checkout_dir):
            pass


if __name__ == "__main__":
    unittest.main()
