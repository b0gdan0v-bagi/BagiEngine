"""Tests of tools/web/package.py. Run: python -m unittest discover tools/web/tests"""

import contextlib
import gzip
import hashlib
import io
import json
import tempfile
import unittest
import zlib
from pathlib import Path

from web_fixtures import CONTENT, GAME, FakeBrotli, png_header_size, write_app_file, write_build_output

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
                         ["icons", "index.html", "index.html.br", "index.html.gz", "manifest.json", "manifest.webmanifest", "notes.txt"])
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
        self.assertEqual(sorted(manifest["files"]), ["BagiFake.data", "BagiFake.js", "BagiFake.wasm", "icons/apple-touch-icon.png",
                                                     "icons/icon-192.png", "icons/icon-512.png", "index.html", "manifest.webmanifest"])
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


class WebAppTest(unittest.TestCase):
    """manifest.webmanifest and icons/ from the game's app.json (`app_file`)."""

    def setUp(self):
        self._temp = tempfile.TemporaryDirectory()
        self.addCleanup(self._temp.cleanup)
        self.root = Path(self._temp.name)
        self.build = write_build_output(self.root / "build")
        self.out = self.root / "dist"
        self.game_dir = self.root / "game" / "web"

    def run_package(self, app_file=None, **kwargs) -> dict:
        return package.package(self.build, self.out, brotli_module=None, app_file=app_file, **kwargs)

    def web_manifest(self) -> dict:
        return json.loads((self.out / "manifest.webmanifest").read_text(encoding="utf-8"))

    def test_without_an_app_file_the_defaults_and_two_generated_icons_are_written(self):
        manifest = self.run_package()
        web = self.web_manifest()
        self.assertEqual((web["name"], web["short_name"]), (GAME, GAME))
        self.assertEqual((web["display"], web["display_override"]), ("fullscreen", ["fullscreen", "standalone"]))
        self.assertEqual((web["orientation"], web["start_url"], web["scope"], web["id"]), ("any", "./", "./", "./"))
        self.assertEqual((web["background_color"], web["theme_color"]), ("#101114", "#101114"))
        self.assertEqual([(icon["src"], icon["sizes"], icon["type"]) for icon in web["icons"]],
                         [("icons/icon-192.png", "192x192", "image/png"), ("icons/icon-512.png", "512x512", "image/png")])
        for name, size in (("icon-192.png", 192), ("icon-512.png", 512), ("apple-touch-icon.png", 512)):
            with self.subTest(name):
                self.assertEqual(png_header_size((self.out / "icons" / name).read_bytes()), (size, size))
        self.assertTrue({"manifest.webmanifest", "icons/icon-192.png", "icons/icon-512.png", "icons/apple-touch-icon.png"} <= set(manifest["files"]))

    def test_the_generated_png_is_a_valid_flat_image_with_the_same_bytes_every_time(self):
        data = package.flat_color_png(8, "#336699")
        self.assertEqual(data, package.flat_color_png(8, "#336699"))
        self.assertTrue(data.startswith(b"\x89PNG\r\n\x1a\n"))
        self.assertEqual(png_header_size(data), (8, 8))
        self.assertEqual(package.png_size(data), (8, 8))
        # IHDR: 8 bit, color type 2 (RGB); the one IDAT inflates to 8 rows of a filter byte and 8 RGB pixels
        self.assertEqual(data[24:26], b"\x08\x02")
        idat = data.index(b"IDAT")
        length = int.from_bytes(data[idat - 4:idat], "big")
        raw = zlib.decompress(data[idat + 4:idat + 4 + length])
        self.assertEqual(raw, (b"\x00" + bytes((0x33, 0x66, 0x99)) * 8) * 8)
        self.assertEqual(zlib.crc32(data[idat:idat + 4 + length]), int.from_bytes(data[idat + 4 + length:idat + 8 + length], "big"))
        self.assertTrue(data.endswith(package.png_chunk(b"IEND", b"")))

    def test_the_generated_icons_take_the_theme_color(self):
        app = write_app_file(self.game_dir, {"theme_color": "#ff0000"})
        self.run_package(app)
        self.assertEqual((self.out / "icons" / "icon-192.png").read_bytes(), package.flat_color_png(192, "#ff0000"))
        self.assertEqual(self.web_manifest()["theme_color"], "#ff0000")

    def test_an_app_file_names_the_app_and_lists_the_icons_that_are_copied(self):
        app = write_app_file(self.game_dir, {
            "name": "Mahjong", "short_name": "Mj", "orientation": "landscape", "background_color": "#0a2a1a", "theme_color": "#103020",
            "icons": [{"src": "icons/small.png", "sizes": "192x192", "purpose": "any maskable"}, {"src": "icons/big.png"}]},
            icons={"icons/small.png": 192, "icons/big.png": 512})
        manifest = self.run_package(app)
        web = self.web_manifest()
        self.assertEqual((web["name"], web["short_name"], web["orientation"]), ("Mahjong", "Mj", "landscape"))
        self.assertEqual((web["background_color"], web["theme_color"]), ("#0a2a1a", "#103020"))
        self.assertEqual(web["icons"], [{"src": "icons/small.png", "sizes": "192x192", "type": "image/png", "purpose": "any maskable"},
                                        {"src": "icons/big.png", "sizes": "512x512", "type": "image/png"}])
        self.assertEqual(sorted(path.name for path in (self.out / "icons").iterdir()), ["apple-touch-icon.png", "big.png", "small.png"])
        # the largest listed icon is the apple touch icon
        self.assertEqual((self.out / "icons" / "apple-touch-icon.png").read_bytes(), (self.game_dir / "icons" / "big.png").read_bytes())
        self.assertFalse((self.out / "icons" / "icon-192.png").exists())
        self.assertEqual(manifest["files"]["icons/big.png"]["size"], (self.game_dir / "icons" / "big.png").stat().st_size)

    def test_the_package_manifest_lists_the_web_app_files_with_size_and_sha256_and_no_compressed_copies(self):
        manifest = package.package(self.build, self.out, brotli_module=FakeBrotli)
        for name in ("manifest.webmanifest", "icons/icon-192.png", "icons/icon-512.png", "icons/apple-touch-icon.png"):
            with self.subTest(name):
                data = (self.out / name).read_bytes()
                self.assertEqual(manifest["files"][name], {"size": len(data), "sha256": hashlib.sha256(data).hexdigest()})
                self.assertFalse((self.out / (name + ".gz")).exists())
                self.assertFalse((self.out / (name + ".br")).exists())
        self.assertEqual(json.loads((self.out / "manifest.json").read_text(encoding="utf-8")), manifest)

    def test_the_package_manifest_is_still_manifest_json_and_the_web_manifest_a_file_of_its_own(self):
        self.run_package()
        self.assertNotIn("files", self.web_manifest())
        self.assertNotIn("manifest.json", json.loads((self.out / "manifest.json").read_text(encoding="utf-8"))["files"])

    def test_an_icon_of_an_earlier_run_is_removed_and_other_files_of_the_folder_stay(self):
        app = write_app_file(self.game_dir, {"icons": [{"src": "old.png"}]}, icons={"old.png": 64})
        self.run_package(app)
        self.assertTrue((self.out / "icons" / "old.png").is_file())
        (self.out / "notes.txt").write_text("not ours", encoding="utf-8")
        self.run_package()
        self.assertFalse((self.out / "icons" / "old.png").exists())
        self.assertEqual(sorted(path.name for path in (self.out / "icons").iterdir()), ["apple-touch-icon.png", "icon-192.png", "icon-512.png"])
        self.assertTrue((self.out / "notes.txt").is_file())

    def test_the_web_app_files_do_not_depend_on_threads(self):
        self.run_package(threads=False)
        self.assertEqual(self.web_manifest()["display"], "fullscreen")

    def test_a_bad_app_file_is_a_package_error_that_names_it(self):
        icon_dir = self.game_dir
        cases = {
            "not json": ("{not json", "not valid JSON"),
            "not an object": ("[1]", "JSON object"),
            "unknown orientation": ({"orientation": "sideways"}, "orientation"),
            "bad color": ({"theme_color": "red"}, "theme_color"),
            "short color": ({"background_color": "#fff"}, "background_color"),
            "name not a string": ({"name": 5}, "`name`"),
            "icons not a list": ({"icons": "a.png"}, "icons"),
            "icon without src": ({"icons": [{"sizes": "1x1"}]}, "src"),
            "missing icon": ({"icons": [{"src": "nope.png"}]}, "nope.png not found"),
            "outside the folder": ({"icons": [{"src": "../secret.png"}]}, "outside"),
            "absolute path outside": ({"icons": [{"src": str(self.root / "secret.png")}]}, "outside"),
            "not a png": ({"icons": [{"src": "text.png"}]}, "not a PNG"),
            "sizes not a string": ({"icons": [{"src": "ok.png", "sizes": 192}]}, "sizes"),
            "same file name twice": ({"icons": [{"src": "ok.png"}, {"src": "sub/ok.png"}]}, "twice"),
            "reserved name": ({"icons": [{"src": "apple-touch-icon.png"}]}, "reserved"),
        }
        for name, (content, message) in cases.items():
            with self.subTest(name):
                icon_dir.mkdir(parents=True, exist_ok=True)
                (self.root / "secret.png").write_bytes(package.flat_color_png(4, "#000000"))
                (icon_dir / "text.png").write_text("plain text", encoding="utf-8")
                (icon_dir / "ok.png").write_bytes(package.flat_color_png(4, "#000000"))
                (icon_dir / "sub").mkdir(exist_ok=True)
                (icon_dir / "sub" / "ok.png").write_bytes(package.flat_color_png(4, "#000000"))
                (icon_dir / "apple-touch-icon.png").write_bytes(package.flat_color_png(4, "#000000"))
                app = icon_dir / "app.json"
                app.write_text(content if isinstance(content, str) else json.dumps(content), encoding="utf-8")
                with self.assertRaisesRegex(package.PackageError, message):
                    self.run_package(app)

    def test_a_missing_app_file_is_a_package_error(self):
        with self.assertRaisesRegex(package.PackageError, "cannot read"):
            self.run_package(self.game_dir / "app.json")

    def test_a_bad_app_file_writes_nothing(self):
        self.game_dir.mkdir(parents=True)
        app = self.game_dir / "app.json"
        app.write_text("{not json", encoding="utf-8")
        with self.assertRaises(package.PackageError):
            self.run_package(app)
        self.assertFalse(self.out.exists())

    def test_the_command_line_takes_the_app_file(self):
        app = write_app_file(self.game_dir, {"name": "Cli Game"})
        with contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(package.main([str(self.build), str(self.out), "--app", str(app)]), 0)
        self.assertEqual(self.web_manifest()["name"], "Cli Game")
        err = io.StringIO()
        with contextlib.redirect_stderr(err):
            self.assertEqual(package.main([str(self.build), str(self.root / "other"), "--app", str(self.game_dir / "none.json")]), 1)
        self.assertIn("package.py:", err.getvalue())


if __name__ == "__main__":
    unittest.main()
