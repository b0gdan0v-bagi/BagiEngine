"""check_third_party_notices.py on small trees made in a temp folder."""

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

import check_third_party_notices as notices  # noqa: E402

HEADER = "| Path | Component | Version | Licence | Licence file | In a game |\n|---|---|---|---|---|---|\n"


def make_tree(root: Path, rows: str) -> None:
    (root / "external" / "SDL").mkdir(parents=True)
    (root / "external" / "SDL" / "LICENSE.txt").write_text("zlib")
    (root / "src" / "Modules" / "BECore").mkdir(parents=True)
    (root / "src" / "Modules" / "BECore" / "CLAUDE.md").write_text("own")
    (root / "src" / "Modules" / "fmt").mkdir(parents=True)
    (root / "src" / "Modules" / "fmt" / "LICENSE").write_text("MIT")
    (root / "assets" / "fonts").mkdir(parents=True)
    (root / "assets" / "fonts" / "A.ttf").write_bytes(b"\0")
    (root / "assets" / "fonts" / "A-OFL.txt").write_text("OFL")
    (root / notices.NOTICES).write_text(HEADER + rows)


FULL = (
    "| `external/SDL` | SDL | 3 | zlib | `external/SDL/LICENSE.txt` | ships |\n"
    "| `src/Modules/fmt` | fmt | 11 | MIT | `src/Modules/fmt/LICENSE` | ships |\n"
    "| `assets/fonts/A.ttf` | A | 1 | OFL | `assets/fonts/A-OFL.txt` | ships |\n"
)


def test_a_tree_with_every_row_passes(tmp_path):
    make_tree(tmp_path, FULL)
    assert notices.check(tmp_path) == []


def test_a_new_external_folder_without_a_row_is_named(tmp_path):
    make_tree(tmp_path, FULL)
    (tmp_path / "external" / "zlib").mkdir()
    assert notices.check(tmp_path) == ["external/zlib: no row in THIRD_PARTY_NOTICES.md"]


def test_a_module_without_its_own_claude_md_is_third_party(tmp_path):
    make_tree(tmp_path, FULL)
    (tmp_path / "src" / "Modules" / "glm").mkdir()
    assert notices.check(tmp_path) == ["src/Modules/glm: no row in THIRD_PARTY_NOTICES.md"]


def test_a_font_and_a_vendored_source_file_need_rows(tmp_path):
    make_tree(tmp_path, FULL)
    (tmp_path / "assets" / "fonts" / "B.otf").write_bytes(b"\0")
    (tmp_path / "src" / "Widgets").mkdir(parents=True)
    (tmp_path / "src" / "Widgets" / "Icons.h").write_text("// from upstream\n// Vendored: do not edit by hand.\n")
    assert notices.check(tmp_path) == ["assets/fonts/B.otf: no row in THIRD_PARTY_NOTICES.md", "src/Widgets/Icons.h: no row in THIRD_PARTY_NOTICES.md"]


def test_a_row_whose_path_or_licence_file_is_gone_is_named(tmp_path):
    make_tree(tmp_path, FULL + "| `external/gone` | Gone | 1 | MIT | `external/gone/LICENSE` | ships |\n")
    (tmp_path / "src" / "Modules" / "fmt" / "LICENSE").unlink()
    assert notices.check(tmp_path) == [
        "src/Modules/fmt: the licence file src/Modules/fmt/LICENSE does not exist",
        "external/gone: the row's path does not exist",
        "external/gone: the licence file external/gone/LICENSE does not exist",
    ]


def test_a_row_without_a_licence_file_is_allowed(tmp_path):
    make_tree(tmp_path, FULL + "| `src/Modules/BECore` | own | - | - | none: see notes | - |\n")
    assert notices.check(tmp_path) == []
