"""Tests of tools/web/deploy_targets.py: the format of web/targets.json and what it refuses. Run: python -m unittest discover tools/web/tests"""

import json
import tempfile
import unittest
from pathlib import Path

import web_fixtures  # noqa: F401  (puts tools/web on sys.path)

import deploy_targets
from deploy_targets import TargetsError

ENGINE_DIR = Path(__file__).resolve().parents[3]

GOOD = {"game": "BagiMahjong", "preset": "web-editor", "service": "studio-mahjong", "address": "studio-mahjong.bagiz.dev"}
GOOD_SITE = {"game": "BagiDemo", "preset": "web-editor", "site": "engine.bagiz.dev", "page": "demo"}


def document(**target_changes) -> dict:
    entry = {**GOOD, **target_changes}
    return {"format": 1, "targets": {"mahjong-editor": {key: value for key, value in entry.items() if value is not None}}}


def site_document(**target_changes) -> dict:
    entry = {**GOOD_SITE, **target_changes}
    return {"format": 1, "targets": {"engine-demo": {key: value for key, value in entry.items() if value is not None}}}


class TargetsFileTestCase(unittest.TestCase):
    def setUp(self):
        self._temp = tempfile.TemporaryDirectory()
        self.addCleanup(self._temp.cleanup)
        self.project = Path(self._temp.name)

    def write(self, content, raw=False) -> Path:
        path = self.project / deploy_targets.TARGETS_FILE
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(content if raw else json.dumps(content).encode("utf-8"))
        return path

    def assertRefused(self, content, *words):
        path = self.write(content)
        with self.assertRaises(TargetsError) as caught:
            deploy_targets.load(self.project)
        self.assertIn(str(path), str(caught.exception))
        for word in words:
            self.assertIn(word, str(caught.exception))


class LoadTest(TargetsFileTestCase):
    def test_a_valid_file_gives_the_target_with_the_defaults(self):
        self.write(document())
        target = deploy_targets.load(self.project)["mahjong-editor"]
        self.assertEqual(target, deploy_targets.Target("mahjong-editor", "BagiMahjong", "web-editor", "studio-mahjong",
                                                       "studio-mahjong.bagiz.dev", "main", "engine"))

    def test_a_site_page_target_has_the_defaults_and_no_service(self):
        self.write({"format": 1, "targets": {"engine-demo": GOOD_SITE}})
        target = deploy_targets.load(self.project)["engine-demo"]
        self.assertEqual(target, deploy_targets.Target("engine-demo", "BagiDemo", "web-editor", site="engine.bagiz.dev", page="demo"))
        self.assertEqual((target.kind, target.service, target.address, target.base, target.area), ("site", None, None, "main", "engine"))

    def test_a_railway_target_is_of_the_kind_service(self):
        self.write(document())
        self.assertEqual(deploy_targets.load(self.project)["mahjong-editor"].kind, "service")

    def test_both_kinds_can_be_in_one_file(self):
        self.write({"format": 1, "targets": {"a": GOOD_SITE, "b": GOOD}})
        self.assertEqual([target.kind for target in deploy_targets.load(self.project).values()], ["site", "service"])

    def test_base_and_area_can_be_given(self):
        self.write(document(base="release/1.2", area="mahjong"))
        target = deploy_targets.load(self.project)["mahjong-editor"]
        self.assertEqual((target.base, target.area), ("release/1.2", "mahjong"))

    def test_a_single_thread_preset_and_a_host_without_dots_are_accepted(self):
        self.write(document(preset="web-editor-single-thread", address="localhost"))
        self.assertEqual(deploy_targets.load(self.project)["mahjong-editor"].address, "localhost")

    def test_find_names_the_known_targets_for_an_unknown_name(self):
        self.write(document())
        with self.assertRaises(TargetsError) as caught:
            deploy_targets.find(self.project, "nope")
        self.assertIn("nope", str(caught.exception))
        self.assertIn("mahjong-editor", str(caught.exception))


class ShippedTargetsTest(unittest.TestCase):
    def test_every_target_of_the_engines_file_names_a_preset_that_can_be_configured_and_built(self):
        presets = json.loads((ENGINE_DIR / "CMakePresets.json").read_text(encoding="utf-8"))
        configurable = {preset["name"] for preset in presets["configurePresets"] if not preset.get("hidden")}
        buildable = {preset["name"] for preset in presets["buildPresets"] if not preset.get("hidden")}
        for name, target in deploy_targets.load(ENGINE_DIR).items():
            with self.subTest(target=name):
                self.assertIn(target.preset, configurable)
                self.assertIn(target.preset, buildable)


    def test_the_engine_ships_the_demo_page_and_no_editor_service(self):
        targets = deploy_targets.load(ENGINE_DIR)
        self.assertEqual(targets["engine-demo"].kind, "site")
        self.assertEqual((targets["engine-demo"].game, targets["engine-demo"].site, targets["engine-demo"].page), ("BagiDemo", "engine.bagiz.dev", "demo"))
        self.assertNotIn("engine-editor", targets)


class RefusalTest(TargetsFileTestCase):
    def test_a_missing_file(self):
        with self.assertRaises(TargetsError) as caught:
            deploy_targets.load(self.project)
        self.assertIn("targets.json", str(caught.exception))

    def test_not_json_and_not_utf8(self):
        self.write(b"{not json", raw=True)
        with self.assertRaises(TargetsError):
            deploy_targets.load(self.project)
        self.write(b"\xff\xfe\x00", raw=True)
        with self.assertRaises(TargetsError):
            deploy_targets.load(self.project)

    def test_a_file_over_the_size_limit(self):
        self.write(b" " * (deploy_targets.MAX_FILE_BYTES + 1), raw=True)
        with self.assertRaises(TargetsError) as caught:
            deploy_targets.load(self.project)
        self.assertIn("bytes", str(caught.exception))

    def test_a_duplicate_key_in_one_object(self):
        text = '{"format": 1, "targets": {"a": {"game": "G", "game": "H", "preset": "web-x", "service": "s", "address": "h"}}}'
        path = self.write(text.encode("utf-8"), raw=True)
        with self.assertRaises(TargetsError) as caught:
            deploy_targets.load(self.project)
        self.assertIn("twice", str(caught.exception))
        self.assertIn(str(path), str(caught.exception))

    def test_the_top_level_and_the_format(self):
        self.assertRefused([], "object")
        self.assertRefused({"targets": document()["targets"]}, "format")
        self.assertRefused({"format": 2, "targets": document()["targets"]}, "format")
        self.assertRefused({"format": True, "targets": document()["targets"]}, "format")
        self.assertRefused({"format": 1}, "targets")
        self.assertRefused({"format": 1, "targets": {}}, "targets")
        self.assertRefused({**document(), "extra": 1}, "extra")

    def test_an_unknown_key_of_a_target_is_refused_so_a_typo_cannot_pass(self):
        self.assertRefused(document(servce="x"), "servce")

    def test_each_required_key_is_needed(self):
        for key in deploy_targets.REQUIRED_KEYS + deploy_targets.SERVICE_KEYS:
            with self.subTest(key=key):
                self.assertRefused(document(**{key: None}), key, "missing")
        for key in deploy_targets.REQUIRED_KEYS + deploy_targets.SITE_KEYS:
            with self.subTest(key=key):
                self.assertRefused(site_document(**{key: None}), key, "missing")

    def test_keys_of_both_kinds_in_one_target_are_refused_naming_them(self):
        self.assertRefused(document(site="engine.bagiz.dev"), "'service'", "'site'", "cannot be in one target")
        self.assertRefused(document(address="h.dev", page="demo"), "'address'", "'page'", "cannot be in one target")
        self.assertRefused(site_document(service="studio-x"), "'site'", "'service'", "cannot be in one target")

    def test_half_a_pair_is_refused_naming_the_missing_key(self):
        self.assertRefused(site_document(page=None), "'page'", "missing")
        self.assertRefused(site_document(site=None), "'site'", "missing")
        self.assertRefused(document(address=None), "'address'", "missing")

    def test_a_target_with_none_of_the_pairs_is_refused_naming_all_four_keys(self):
        self.assertRefused(site_document(site=None, page=None), "'service'", "'address'", "'site'", "'page'")

    def test_a_target_that_is_not_an_object_or_has_a_bad_name(self):
        self.assertRefused({"format": 1, "targets": {"a": []}}, "object")
        self.assertRefused({"format": 1, "targets": {"A": document()["targets"]["mahjong-editor"]}}, "'A'")
        self.assertRefused({"format": 1, "targets": {"-a": document()["targets"]["mahjong-editor"]}}, "'-a'")

    def test_bad_values_of_every_key(self):
        bad = {
            "game": ["--x", "a b", "Bagi-Mahjong", "", "a/b", 5, None],
            "preset": ["--x", "web", "Web-editor", "release", "web-a b", "web-../x"],
            "service": ["--x", "-x", "a b", "A", "a_b", "https://h/"],
            "address": ["https://h/", "h:80", "h/path", "H.com", "a b", "-h", "a..b", ".a", "a.", "--x"],
            "base": ["--x", "-b", "a b", "a..b", "/a", "a*"],
            "area": ["--x", "Engine", "a b", "1a", ""],
        }
        site_bad = {
            "site": ["https://engine.bagiz.dev", "engine.bagiz.dev:443", "engine.bagiz.dev/demo", "Engine.dev", "-site", "a..b", ""],
            "page": ["Demo", "de/mo", "-demo", "de mo", "../x", "", "demo/"],
        }
        for key, values in site_bad.items():
            for value in values:
                with self.subTest(key=key, value=value):
                    path = self.write({"format": 1, "targets": {"t": {**GOOD_SITE, key: value}}})
                    with self.assertRaises(TargetsError) as caught:
                        deploy_targets.load(self.project)
                    self.assertIn(key, str(caught.exception))
                    self.assertIn(str(path), str(caught.exception))
        for key, values in bad.items():
            for value in values:
                with self.subTest(key=key, value=value):
                    path = self.write({"format": 1, "targets": {"t": {**GOOD, key: value}}})
                    with self.assertRaises(TargetsError) as caught:
                        deploy_targets.load(self.project)
                    self.assertIn(key, str(caught.exception))
                    self.assertIn(str(path), str(caught.exception))


if __name__ == "__main__":
    unittest.main()
