"""check_guide.py on small repositories made in a temp folder."""

import json
import subprocess
import sys
from pathlib import Path

import check_guide as guide

SOURCE = "tools/demo/config/Scene.xml"
SOURCE_TEXT = "<Scene>\n    <Node name=\"A\"/>\n    <Node name=\"B\"/>\n    <Node name=\"C\"/>\n</Scene>\n"
INDEX_PAGE = "docs/guide/README.md"


def write(root: Path, relative: str, text: str) -> Path:
    path = root / relative
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8")
    return path


def make_repo(root: Path, index_extra: str = "") -> None:
    write(root, "README.md", "See [the guide](docs/guide/README.md).\n")
    write(root, SOURCE, SOURCE_TEXT)
    write(root, "CMakePresets.json", json.dumps({"configurePresets": [{"name": "macos-debug"}], "buildPresets": [{"name": "macos-debug"}]}))
    write(root, "tools/docs/guide_shots.json", json.dumps({"shots": [{"name": "known"}]}))
    write(root, INDEX_PAGE, "# Guide\n" + index_extra)


def messages(root: Path) -> list[str]:
    return [str(finding) for finding in guide.check(root)]


def page(root: Path, name: str, body: str, linked: bool = True) -> None:
    write(root, f"docs/guide/{name}", body)
    if linked:
        index = root / INDEX_PAGE
        index.write_text(index.read_text() + f"- [{name}]({name})\n")


def fence(marker: str, *lines: str) -> str:
    return marker + "\n```\n" + "\n".join(lines) + "\n```\n"


def test_a_clean_page_passes(tmp_path):
    make_repo(tmp_path)
    page(tmp_path, "a.md", "# A\n\n" + fence(f"<!-- source: {SOURCE} -->", '<Node name="A"/>') + "\nSee `tools/demo/config/Scene.xml`.\n")
    assert messages(tmp_path) == []


def test_no_guide_folder_is_not_a_finding(tmp_path):
    assert messages(tmp_path) == []
    result = subprocess.run([sys.executable, guide.__file__, "--root", str(tmp_path)], capture_output=True, text=True)
    assert result.returncode == 0
    assert "nothing to check" in result.stdout


def test_pictures_without_a_page_are_no_guide_yet(tmp_path):
    write(tmp_path, "docs/guide/img/orphan.png", "png")
    assert messages(tmp_path) == []


def test_main_exits_one_with_a_line_per_finding(tmp_path):
    make_repo(tmp_path)
    page(tmp_path, "a.md", "```\nx\n```\n")
    result = subprocess.run([sys.executable, guide.__file__, "--root", str(tmp_path)], capture_output=True, text=True)
    assert result.returncode == 1
    assert result.stdout.startswith("docs/guide/a.md:1: ")


def test_excerpt_segments_must_exist_in_order_and_gaps_are_allowed(tmp_path):
    make_repo(tmp_path)
    page(tmp_path, "ok.md", fence(f"<!-- source: {SOURCE} -->", "<Scene>", "...", '<Node name="C"/>'))
    page(tmp_path, "missing.md", fence(f"<!-- source: {SOURCE} -->", '<Node name="Z"/>'))
    page(tmp_path, "order.md", fence(f"<!-- source: {SOURCE} -->", '<Node name="C"/>', "...", '<Node name="A"/>'))
    found = messages(tmp_path)
    assert len(found) == 2
    assert any(line.startswith("docs/guide/missing.md:2:") for line in found)
    assert any(line.startswith("docs/guide/order.md:2:") for line in found)


def test_excerpt_of_a_missing_file_is_a_finding(tmp_path):
    make_repo(tmp_path)
    page(tmp_path, "a.md", fence("<!-- source: nope/File.xml -->", "x"))
    assert "source file does not exist" in messages(tmp_path)[0]


def test_unmarked_fence_and_empty_illustration_are_findings(tmp_path):
    make_repo(tmp_path)
    page(tmp_path, "a.md", "```\nx\n```\n\n" + fence("<!-- illustration: -->", "y") + "\n" + fence("<!-- illustration: output of a run -->", "z"))
    found = messages(tmp_path)
    assert len(found) == 2
    assert "without a marker" in found[0]
    assert "without a reason" in found[1]


def test_illustrations_are_counted(tmp_path):
    make_repo(tmp_path)
    page(tmp_path, "a.md", fence("<!-- illustration: one -->", "a") + "\n" + fence("<!-- illustration: two -->", "b"))
    findings, illustrations, present = guide.run(tmp_path)
    assert (findings, illustrations, present) == ([], 2, True)


def test_command_paths_and_presets(tmp_path):
    make_repo(tmp_path)
    write(tmp_path, "tools/x.py", "")
    command = "<!-- command -->"
    page(tmp_path, "good.md", fence(command, "python3 tools/x.py --root .", "cmake --preset macos-debug", "build/macos-debug/bin/BagiDemo", "ctest --test-dir build/macos-debug", "cmake --build --preset <p>"))
    page(tmp_path, "bad.md", fence(command, "python3 tools/missing.py", "cmake --preset nope", "ctest --test-dir build/nope"))
    found = [line for line in messages(tmp_path) if "bad.md" in line]
    assert len(found) == 3
    assert any("tools/missing.py" in line for line in found)
    assert sum("not a preset" in line for line in found) == 2
    assert not any("good.md" in line for line in messages(tmp_path))


def test_links_and_images_must_resolve(tmp_path):
    make_repo(tmp_path)
    write(tmp_path, "docs/guide/img/known.png", "png")
    page(tmp_path, "a.md", "[ok](b.md#anchor) [web](https://example.com/x) ![pic](img/known.png) [bad](gone.md) ![bad](img/gone.png)\n")
    page(tmp_path, "b.md", "text\n")
    found = messages(tmp_path)
    assert len(found) == 2
    assert all("link target does not exist" in line for line in found)


def test_backticked_paths_are_checked_with_the_skips(tmp_path):
    make_repo(tmp_path)
    page(tmp_path, "a.md", "`tools/demo/config/Scene.xml` `build/x/y.bin` `tools/<name>/x.md` `tools/*.py` `tools/gone.md` `tools/nodir/` `plain` `1/2`\n")
    found = messages(tmp_path)
    assert len(found) == 2
    assert any("tools/gone.md" in line for line in found)
    assert any("tools/nodir/" in line for line in found)


def test_a_page_the_index_does_not_link(tmp_path):
    make_repo(tmp_path)
    page(tmp_path, "lonely.md", "x\n", linked=False)
    assert messages(tmp_path) == ["docs/guide/README.md:1: the index does not link docs/guide/lonely.md"]


def test_a_picture_needs_a_manifest_row(tmp_path):
    make_repo(tmp_path)
    write(tmp_path, "docs/guide/img/known.png", "png")
    write(tmp_path, "docs/guide/img/orphan.png", "png")
    found = messages(tmp_path)
    assert len(found) == 1
    assert "orphan.png" in found[0] and "cannot be taken again" in found[0]


def test_a_claude_md_under_the_guide(tmp_path):
    make_repo(tmp_path)
    write(tmp_path, "docs/guide/sub/CLAUDE.md", "x")
    found = messages(tmp_path)
    assert any("docs/guide/sub/CLAUDE.md:1" in line and "no CLAUDE.md" in line for line in found)


def test_the_repository_readme_must_link_the_guide(tmp_path):
    make_repo(tmp_path)
    write(tmp_path, "README.md", "no link\n")
    assert messages(tmp_path) == ["README.md:1: the repository README does not link docs/guide/README.md"]
