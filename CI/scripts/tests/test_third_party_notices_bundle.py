"""third_party_notices_bundle.py on small trees made in a temp folder, and on the engine's own table."""

import os
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

import check_third_party_notices as checker  # noqa: E402
import third_party_notices_bundle as bundle  # noqa: E402

HEADER = "| Path | Component | Version | Licence | Licence file | In a game |\n|---|---|---|---|---|---|\n"
ENGINE_ROOT = Path(__file__).resolve().parents[3]
STB_TAIL = (
    "int code;\n\n/*\n---\nThis software is available under 2 licenses -- choose whichever you prefer.\n"
    "ALTERNATIVE A - MIT License\nPermission is hereby granted\n---\n*/\n"
)
HEADER_FILE = "// Ported code, not a licence.\n//\n// MIT License\n//\n// Permission is hereby granted, free of charge\n\nint code;\n"


def write(root: Path, relative: str, text: str) -> None:
    path = root / relative
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text)


def make_tree(root: Path, rows: str) -> None:
    write(root, "external/SDL/LICENSE.txt", "zlib licence text of SDL\n")
    write(root, "assets/fonts/A-OFL.txt", "SIL OFL text of the fonts\n")
    write(root, "src/Modules/stb/stb_image.h", STB_TAIL)
    write(root, "src/Modules/Json.cpp", HEADER_FILE)
    write(root, "external/googletest/LICENSE", "BSD text of googletest\n")
    write(root, checker.NOTICES, HEADER + rows)


ROWS = (
    "| `external/SDL` | SDL | 3 | zlib | `external/SDL/LICENSE.txt` | ships |\n"
    "| `assets/fonts/A.ttf` | Font A | 1 | OFL | `assets/fonts/A-OFL.txt` | ships |\n"
    "| `assets/fonts/B.ttf` | Font B | 1 | OFL | `assets/fonts/A-OFL.txt` | ships |\n"
    "| `src/Modules/stb/stb_image.h` | stb_image | 2 | MIT | `src/Modules/stb/stb_image.h` (end of file) | ships |\n"
    "| `src/Modules/Json.cpp` | Json | 3 | MIT | `src/Modules/Json.cpp` (its header) | ships if `X` |\n"
    "| `external/googletest` | GoogleTest | 1 | BSD | `external/googletest/LICENSE` | build only (tests) |\n"
    "| `external/Loader` | Loader | 1 | Apache | none in the folder | ships |\n"
)


def build_text(root: Path, tmp_path: Path, **kwargs) -> str:
    out = tmp_path / "out" / "THIRD_PARTY_NOTICES.txt"
    assert bundle.build(root, out, kwargs.get("game_notices"), kwargs.get("game_root")) == []
    return out.read_text()


def test_a_build_only_row_is_left_out_and_the_others_are_listed(tmp_path):
    make_tree(tmp_path, ROWS)
    text = build_text(tmp_path, tmp_path)
    assert "GoogleTest" not in text and "BSD text of googletest" not in text
    for component in ("SDL", "Font A", "Font B", "stb_image", "Json", "Loader"):
        assert f"* {component}\n" in text
    assert "included: ships if `X`" in text


def test_rows_that_share_a_licence_file_write_its_text_once(tmp_path):
    make_tree(tmp_path, ROWS)
    text = build_text(tmp_path, tmp_path)
    assert text.count("SIL OFL text of the fonts") == 1
    assert "Used by: Font A; Font B" in text


def test_a_licence_in_a_source_file_is_an_excerpt(tmp_path):
    make_tree(tmp_path, ROWS)
    text = build_text(tmp_path, tmp_path)
    assert "ALTERNATIVE A - MIT License" in text
    assert "int code;" not in text
    assert "Ported code, not a licence." in text and "Permission is hereby granted, free of charge" in text


def test_a_row_without_a_licence_file_is_listed_with_a_line_saying_so(tmp_path):
    make_tree(tmp_path, ROWS)
    text = build_text(tmp_path, tmp_path)
    assert "Used by: Loader" in text and bundle.NO_LICENCE_FILE in text


def test_a_missing_licence_file_of_a_row_that_ships_is_an_error(tmp_path):
    make_tree(tmp_path, ROWS)
    write(tmp_path, "external/SDL/README.md", "not a licence")
    (tmp_path / "external" / "SDL" / "LICENSE.txt").unlink()
    out = tmp_path / "out.txt"
    assert bundle.build(tmp_path, out, None, None) == ["external/SDL: the licence file external/SDL/LICENSE.txt does not exist"]
    assert not out.exists()


def test_a_licence_file_in_a_submodule_that_is_not_checked_out_is_a_note(tmp_path):
    make_tree(tmp_path, ROWS)
    (tmp_path / "external" / "SDL" / "LICENSE.txt").unlink()
    (tmp_path / "external" / "SDL").rmdir()
    (tmp_path / "external" / "SDL").mkdir()
    text = build_text(tmp_path, tmp_path)
    assert bundle.NOT_CHECKED_OUT in text


def test_a_game_row_is_added_with_paths_relative_to_the_game(tmp_path):
    engine, game = tmp_path / "engine", tmp_path / "game"
    make_tree(engine, ROWS)
    write(game, "assets/Music-LICENSE.txt", "CC0 text of the music\n")
    write(game, "THIRD_PARTY_NOTICES.md", HEADER + "| `assets/music.ogg` | Music | 1 | CC0 | `assets/Music-LICENSE.txt` | ships |\n")
    text = build_text(engine, tmp_path, game_notices=game / "THIRD_PARTY_NOTICES.md", game_root=game)
    assert "* Music\n" in text and "CC0 text of the music" in text and "zlib licence text of SDL" in text


def test_a_second_build_leaves_the_file_alone(tmp_path):
    make_tree(tmp_path, ROWS)
    out = tmp_path / "out.txt"
    assert bundle.build(tmp_path, out, None, None) == []
    os.utime(out, (1_000_000, 1_000_000))
    assert bundle.build(tmp_path, out, None, None) == []
    assert out.stat().st_mtime == 1_000_000
    write(tmp_path, "external/SDL/LICENSE.txt", "another text\n")
    assert bundle.build(tmp_path, out, None, None) == []
    assert out.stat().st_mtime != 1_000_000


def test_verify_passes_on_a_fresh_bundle_and_fails_when_a_path_or_a_text_is_cut(tmp_path):
    make_tree(tmp_path, ROWS)
    out = tmp_path / "out.txt"
    assert bundle.build(tmp_path, out, None, None) == []
    text = out.read_text()
    assert bundle.verify(tmp_path, out, None, None) == []
    out.write_text(text.replace("path: assets/fonts/B.ttf\n", ""))
    assert bundle.verify(tmp_path, out, None, None) == ["assets/fonts/B.ttf: not in out.txt"]
    out.write_text(text.replace("zlib licence text of SDL", "cut"))
    assert bundle.verify(tmp_path, out, None, None) == ["external/SDL/LICENSE.txt: the licence text is not in out.txt"]


def test_a_row_with_the_wrong_count_of_cells_is_an_error(tmp_path):
    make_tree(tmp_path, ROWS + "| `external/x` | X | 1 |\n")
    assert bundle.build(tmp_path, tmp_path / "out.txt", None, None) == ["THIRD_PARTY_NOTICES.md: external/x: 3 cells, expected 6"]


def test_the_engine_table_builds_and_every_shipped_row_and_excerpt_is_in_it(tmp_path):
    out = tmp_path / "THIRD_PARTY_NOTICES.txt"
    assert bundle.build(ENGINE_ROOT, out, None, None) == []
    assert bundle.verify(ENGINE_ROOT, out, None, None) == []
    text = out.read_text()
    rows, problems = bundle.read_table((ENGINE_ROOT / checker.NOTICES).read_text(encoding="utf-8"), ENGINE_ROOT, checker.NOTICES)
    assert problems == []
    assert any(row.build_only for row in rows)
    for row in rows:
        assert (f"path: {row.path}\n" in text) != row.build_only
        if row.excerpt and not checker.in_missing_submodule(ENGINE_ROOT, row.licence_file):
            excerpt = bundle.excerpt_of(ENGINE_ROOT / row.licence_file, row.excerpt)
            assert any(word in excerpt for word in ("Permission", "MIT", "zlib", "public domain")), row.path
            assert len(excerpt.splitlines()) <= bundle.EXCERPT_MAX_LINES
