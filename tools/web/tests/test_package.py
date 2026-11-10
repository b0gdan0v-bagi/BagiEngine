"""Tests of tools/web/package.py. Run: python -m unittest discover tools/web/tests"""

import contextlib
import gzip
import hashlib
import io
import json
import tempfile
import unittest
from pathlib import Path

from web_fixtures import CONTENT, GAME, FakeBrotli, write_build_output

import package


class PackageTest(unittest.TestCase):
    def setUp(self):
        self._temp = tempfile.TemporaryDirectory()
        self.addCleanup(self._temp.cleanup)
        self.root = Path(self._temp.name)
        self.build = write_build_output(self.root / "build")
        self.out = self.root / "dist"

    def run_package(self, brotli_module=None, **kwargs) -> dict:
        return package.package(self.build, self.out, brotli_module=brotli_module, **kwargs)

    def test_the_page_is_index_html_and_the_other_files_keep_their_names(self):
        self.run_package()
        self.assertEqual((self.out / "index.html").read_bytes(), CONTENT[f"{GAME}.html"])
        self.assertFalse((self.out / f"{GAME}.html").exists())
        for suffix in (".js", ".wasm", ".data"):
            self.assertEqual((self.out / f"{GAME}{suffix}").read_bytes(), CONTENT[f"{GAME}{suffix}"])

    def test_every_page_script_module_and_package_has_a_gzip_copy_that_unpacks_to_it(self):
        self.run_package()
        for name, source in (("index.html", CONTENT[f"{GAME}.html"]), (f"{GAME}.js", CONTENT[f"{GAME}.js"]),
                             (f"{GAME}.wasm", CONTENT[f"{GAME}.wasm"]), (f"{GAME}.data", CONTENT[f"{GAME}.data"])):
            with self.subTest(name):
                self.assertEqual(gzip.decompress((self.out / f"{name}.gz").read_bytes()), source)

    def test_gzip_copies_are_the_same_bytes_on_every_run(self):
        self.run_package()
        first = (self.out / f"{GAME}.wasm.gz").read_bytes()
        self.run_package()
        self.assertEqual((self.out / f"{GAME}.wasm.gz").read_bytes(), first)

    def test_without_brotli_there_is_no_br_copy(self):
        self.run_package(brotli_module=None)
        self.assertEqual(list(self.out.glob("*.br")), [])

    def test_with_brotli_every_compressible_file_has_a_br_copy(self):
        manifest = self.run_package(brotli_module=FakeBrotli)
        for name in ("index.html", f"{GAME}.js", f"{GAME}.wasm", f"{GAME}.data"):
            with self.subTest(name):
                self.assertTrue((self.out / f"{name}.br").is_file())
                self.assertIn("brotli", manifest["files"][name])

    def test_a_br_copy_of_an_earlier_run_does_not_outlive_a_run_without_brotli(self):
        self.run_package(brotli_module=FakeBrotli)
        self.run_package(brotli_module=None)
        self.assertEqual(list(self.out.glob("*.br")), [])

    def test_files_of_an_earlier_run_that_this_run_does_not_make_are_deleted(self):
        self.run_package(brotli_module=FakeBrotli)
        # The game has no content now; a leftover of another game and a file that is not ours
        (self.build / f"{GAME}.data").unlink()
        (self.out / "Other.wasm").write_bytes(b"other")
        (self.out / "Other.wasm.gz").write_bytes(b"other")
        (self.out / "notes.txt").write_bytes(b"not ours")
        manifest = self.run_package(brotli_module=FakeBrotli)
        self.assertEqual(sorted(path.name for path in self.out.iterdir() if "Fake" not in path.name),
                         ["index.html", "index.html.br", "index.html.gz", "manifest.json", "notes.txt"])
        self.assertFalse(any(path.name.startswith(f"{GAME}.data") for path in self.out.iterdir()))
        self.assertNotIn(f"{GAME}.data", manifest["files"])

    def test_every_file_of_the_folder_that_package_py_owns_is_in_the_manifest(self):
        self.run_package(brotli_module=FakeBrotli)
        (self.out / "Stale.js").write_bytes(b"x")
        (self.out / "Stale.data.br").write_bytes(b"x")
        manifest = self.run_package(brotli_module=FakeBrotli)
        listed = set(manifest["files"])
        for path in self.out.iterdir():
            if path.suffix in (".html", ".js", ".wasm", ".data", ".gz", ".br"):
                self.assertIn(path.name.removesuffix(".gz").removesuffix(".br"), listed, path.name)

    def test_the_real_brotli_module_round_trips_when_it_is_installed(self):
        brotli = package.load_brotli()
        if brotli is None:
            self.skipTest("the brotli module is not installed")
        self.run_package(brotli_module=brotli)
        self.assertEqual(brotli.decompress((self.out / f"{GAME}.wasm.br").read_bytes()), CONTENT[f"{GAME}.wasm"])

    def test_the_manifest_has_the_size_and_the_sha256_of_every_file(self):
        manifest = self.run_package(brotli_module=FakeBrotli)
        self.assertEqual(json.loads((self.out / "manifest.json").read_text(encoding="utf-8")), manifest)
        self.assertEqual(manifest["game"], GAME)
        self.assertEqual(sorted(manifest["files"]), ["BagiFake.data", "BagiFake.js", "BagiFake.wasm", "index.html"])
        wasm = manifest["files"][f"{GAME}.wasm"]
        self.assertEqual(wasm["size"], len(CONTENT[f"{GAME}.wasm"]))
        self.assertEqual(wasm["sha256"], hashlib.sha256(CONTENT[f"{GAME}.wasm"]).hexdigest())
        self.assertEqual(wasm["gzip"], (self.out / f"{GAME}.wasm.gz").stat().st_size)
        self.assertEqual(wasm["brotli"], (self.out / f"{GAME}.wasm.br").stat().st_size)

    def test_a_game_without_content_has_no_data_file(self):
        (self.build / f"{GAME}.data").unlink()
        manifest = self.run_package()
        self.assertNotIn(f"{GAME}.data", manifest["files"])
        self.assertFalse((self.out / f"{GAME}.data").exists())

    def test_a_script_that_loads_a_missing_package_is_an_error_that_names_the_file(self):
        # emcc writes the package name into the script (REMOTE_PACKAGE_BASE="<game>.data")
        (self.build / f"{GAME}.js").write_bytes(f'var REMOTE_PACKAGE_BASE="{GAME}.data";'.encode())
        (self.build / f"{GAME}.data").unlink()
        with self.assertRaisesRegex(package.PackageError, f"{GAME}.data not found"):
            self.run_package()

    def test_a_missing_wasm_or_script_is_an_error_that_names_the_file(self):
        for suffix in (".wasm", ".js"):
            with self.subTest(suffix):
                path = self.build / f"{GAME}{suffix}"
                saved = path.read_bytes()
                path.unlink()
                with self.assertRaisesRegex(package.PackageError, f"{GAME}{suffix}"):
                    self.run_package()
                path.write_bytes(saved)

    def test_no_page_in_the_build_folder_is_an_error(self):
        (self.build / f"{GAME}.html").unlink()
        with self.assertRaisesRegex(package.PackageError, "no \\*.html"):
            self.run_package()

    def test_two_pages_need_the_game_name(self):
        (self.build / "Other.html").write_bytes(b"<html></html>")
        (self.build / "Other.js").write_bytes(b"x")
        (self.build / "Other.wasm").write_bytes(b"y")
        with self.assertRaisesRegex(package.PackageError, "--game"):
            self.run_package()
        manifest = self.run_package(game="Other")
        self.assertEqual(manifest["game"], "Other")

    def test_an_unknown_game_is_an_error(self):
        with self.assertRaisesRegex(package.PackageError, "Nope.html"):
            self.run_package(game="Nope")

    def test_a_build_folder_that_does_not_exist_is_an_error(self):
        with self.assertRaisesRegex(package.PackageError, "not a folder"):
            package.package(self.root / "missing", self.out, brotli_module=None)

    def test_the_output_folder_must_not_be_the_build_folder_or_hold_it_or_sit_in_it(self):
        inside = self.build / "dist"
        for name, out in (("same", self.build), ("same by a path with ..", self.build / "sub" / ".."),
                          ("inside the build folder", inside), ("holding the build folder", self.root)):
            with self.subTest(name):
                with self.assertRaisesRegex(package.PackageError, "folder of its own"):
                    package.package(self.build, out, brotli_module=None)
        self.assertTrue((self.build / f"{GAME}.html").is_file())
        self.assertFalse(inside.exists())

    def test_the_manifest_says_threads_true_by_default_and_false_for_a_single_thread_build(self):
        self.assertIs(self.run_package()["threads"], True)
        self.assertIs(json.loads((self.out / "manifest.json").read_text(encoding="utf-8"))["threads"], True)
        self.assertIs(self.run_package(threads=False)["threads"], False)
        self.assertIs(json.loads((self.out / "manifest.json").read_text(encoding="utf-8"))["threads"], False)

    def test_the_single_thread_switch_of_the_command_line_writes_threads_false(self):
        with contextlib.redirect_stdout(io.StringIO()) as out:
            self.assertEqual(package.main([str(self.build), str(self.out), "--single-thread"]), 0)
        self.assertIs(json.loads((self.out / "manifest.json").read_text(encoding="utf-8"))["threads"], False)
        self.assertIn("single-thread", out.getvalue())
        with contextlib.redirect_stdout(io.StringIO()) as out:
            self.assertEqual(package.main([str(self.build), str(self.out)]), 0)
        self.assertIs(json.loads((self.out / "manifest.json").read_text(encoding="utf-8"))["threads"], True)
        self.assertIn("threaded", out.getvalue())

    def test_the_command_line_returns_0_and_1(self):
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            self.assertEqual(package.main([str(self.build), str(self.out)]), 0)
        self.assertIn(GAME, out.getvalue())
        self.assertTrue((self.out / "index.html").is_file())
        err = io.StringIO()
        with contextlib.redirect_stderr(err):
            self.assertEqual(package.main([str(self.root / "missing"), str(self.root / "other")]), 1)
        self.assertIn("package.py:", err.getvalue())


if __name__ == "__main__":
    unittest.main()
