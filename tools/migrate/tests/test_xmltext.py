"""The lossless XML model: a byte-for-byte round trip of the repository's own config files and of crafted files, and the exact text
every edit operation gives."""

from pathlib import Path

import pytest

import xmltext
from xmltext import XmlTextError

ENGINE_ROOT = Path(__file__).resolve().parents[3]
SKIPPED_FOLDERS = {"build", ".git", "node_modules", ".venv", "external", "engine", "dist", "dist-single"}


def _config_files():
    """Every *.xml under a `config` folder of the engine repository and, when the game root is above `engine/`, of the game."""
    found = []
    for root in (ENGINE_ROOT, ENGINE_ROOT.parent):
        if root is ENGINE_ROOT.parent and not (root / "config").is_dir():
            continue
        for path in sorted(root.rglob("*.xml")):
            parts = path.relative_to(root).parts
            if "config" not in parts[:-1]:
                continue
            if any(p in SKIPPED_FOLDERS or p.startswith("build") for p in parts[:-1]):
                continue
            found.append(path)
    return found


@pytest.mark.parametrize("path", _config_files(), ids=lambda p: str(p.relative_to(ENGINE_ROOT.parent)))
def test_round_trip_of_the_config_files_of_the_repositories(path):
    data = path.read_bytes()
    assert xmltext.parse_bytes(data).to_bytes() == data


@pytest.mark.parametrize("path", _config_files(), ids=lambda p: str(p.relative_to(ENGINE_ROOT.parent)))
def test_an_edit_and_its_undo_on_the_root_leave_the_bytes_of_the_config_files(path):
    data = path.read_bytes()
    doc = xmltext.parse_bytes(data)
    doc.root.set("zzz", "1")
    assert doc.to_bytes() != data
    doc.root.remove("zzz")
    assert doc.to_bytes() == data


def test_there_are_files_to_round_trip():
    assert any(p.name == "SceneConfig.xml" for p in _config_files())


# ---- crafted files: everything a round trip must keep -------------------------------------------------------------------------

ROUND_TRIPS = {
    "greater than in a value": '<a b="x > y" c=\'1 > 0\'><d e="&gt;" /></a>',
    "comment with dashes and brackets": "<a><!-- a - b -> c > d <e/> --><b/></a>",
    "cdata": "<a><![CDATA[ <not> & an <element> ]]><b/></a>",
    "prolog and epilog": '<?xml version="1.0" encoding="UTF-8"?>\n<!-- head -->\n<?pi data?>\n<a/>\n<!-- tail -->\n',
    "doctype with a subset": '<?xml version="1.0"?>\n<!DOCTYPE a [ <!ENTITY e "x>y"> ]>\n<a>&e;</a>\n',
    "single quotes and spaces around equals": "<a b = 'x'  c =\"y\"\td\n=\n'z'/>",
    "entities kept as written": '<a t="&quot;q&quot; &amp; &#65; &#x42; &apos;"/>',
    "crlf": '<?xml version="1.0"?>\r\n<a>\r\n  <b c="1"\r\n     d="2" />\r\n</a>\r\n',
    "no final newline": "<a><b/></a>",
    "bom": "﻿<?xml version=\"1.0\"?>\n<a/>\n",
    "tabs": "<a>\n\t<b>\n\t\t<c\t d=\"1\"\t/>\n\t</b>\n</a>\n",
    "text content and mixed": "<a>hello <b>w</b> world &amp; more</a>",
    "end tag padding": "<a><b>x</b ></a >\n",
    "attributes on separate lines": '<a\n  b="1"\n  c="2"\n>\n</a>\n',
    "unicode": '<a t="Привет, 麻雀 🀄"><!-- 東 --></a>',
    "namespaces and dots": '<ns:a x.y="1" xml:lang="en"><ns:b/></ns:a>',
    "self closing with space": '<a><b c="1" /><d/></a>',
}


@pytest.mark.parametrize("text", ROUND_TRIPS.values(), ids=ROUND_TRIPS.keys())
def test_round_trip_of_crafted_files(text):
    assert xmltext.parse(text).to_text() == text
    assert xmltext.parse_bytes(text.encode("utf-8")).to_bytes() == text.encode("utf-8")


def test_a_file_on_disk_comes_back_with_its_bytes(tmp_path):
    data = b"\xef\xbb\xbf<a>\r\n<b x='1'/>\r\n</a>"
    path = tmp_path / "f.xml"
    path.write_bytes(data)
    assert xmltext.read(path).to_bytes() == data


# ---- the tree -----------------------------------------------------------------------------------------------------------------


def test_the_tree_has_names_attributes_children_lines_and_columns():
    doc = xmltext.parse('<?xml version="1.0"?>\n<root>\n  <!-- c -->\n  <a x="1" y=\'&quot;2&quot;\'>text<b/></a>\n</root>\n')
    root = doc.root
    assert root.name == "root" and (root.line, root.column) == (2, 1)
    a = root.child("a")
    assert (a.line, a.column) == (4, 3)
    assert a.items() == [("x", "1"), ("y", '"2"')]
    assert a.get_raw("y") == "&quot;2&quot;"
    assert a.get("missing") is None and a.get("missing", "d") == "d"
    assert [c.kind for c in root.children if isinstance(c, xmltext.RawNode)] == ["text", "comment", "text", "text"]
    assert a.child("b").parent is a
    assert [e.name for e in doc.iter()] == ["root", "a", "b"]
    assert [e.name for e in doc.find_all(lambda e: e.has("x"))] == ["a"]


def test_decoding_follows_a_parser():
    el = xmltext.parse('<a v="x&#10;y&#x41;&lt;&amp;&gt;&quot;&apos;&unknown;"/>').root
    assert el.get("v") == "x\nyA<&>\"'&unknown;"
    assert xmltext.parse('<a v="line\nbreak\ttab"/>').root.get("v") == "line break tab"


def test_indent_of_elements():
    doc = xmltext.parse("<?xml version=\"1.0\"?>\n<r>\n    <a>\n        <b/><c/>\n    </a>\n</r>\n")
    r, a = doc.root, doc.root.child("a")
    b, c = a.child("b"), a.child("c")
    assert (r.indent, a.indent, b.indent, c.indent) == ("", "    ", "        ", None)
    assert doc.indent_unit() == "    "
    assert xmltext.parse("<r><a/></r>").root.child("a").indent is None
    assert xmltext.parse('<?xml version="1.0"?><r/>').root.indent is None


@pytest.mark.parametrize(
    "text, message, line, column",
    [
        ("", "no root element", 1, 1),
        ("<a>", "the element <a> is not closed", 1, 1),
        ("<a></b>", "does not close <a>", 1, 4),
        ("<a>\n  <b>\n</a>", "does not close <b>", 3, 1),
        ('<a b="1" b="2"/>', "repeated", 1, 10),
        ('<a b="1"c="2"/>', "whitespace expected", 1, 9),
        ('<a b=1/>', "quoted value", 1, 6),
        ('<a b="x<y"/>', "'<' in the value", 1, 8),
        ('<a b="x/>', "is not closed", 1, 6),
        ("<a><!-- never closed </a>", "comment is not closed", 1, 4),
        ("<a><![CDATA[ x </a>", "CDATA section is not closed", 1, 4),
        ("<a/>\n<b/>", "content after the root element", 2, 1),
        ("text <a/>", "before the root element", 1, 1),
        ("<a><!DOCTYPE x></a>", "declaration inside an element", 1, 4),
        ("<a b>", "'=' expected", 1, 5),
        ("<a / >", "'>' expected after '/'", 1, 5),
        ("<a></a x>", "'>' expected in the end tag", 1, 8),
    ],
)
def test_malformed_xml_is_one_error_with_line_and_column(text, message, line, column):
    with pytest.raises(XmlTextError) as info:
        xmltext.parse(text)
    assert message in info.value.message
    assert (info.value.line, info.value.column) == (line, column)
    assert str(info.value).startswith(f"{line}:{column}: ")


def test_a_file_that_is_not_utf8_is_an_error_with_the_line():
    with pytest.raises(XmlTextError) as info:
        xmltext.parse_bytes(b"<a>\n\xff</a>")
    assert info.value.line == 2


# ---- attributes ---------------------------------------------------------------------------------------------------------------


def edit(text, fn):
    doc = xmltext.parse(text)
    fn(doc)
    return doc.to_text()


def test_set_keeps_the_place_the_quotes_and_the_rest_of_the_tag():
    text = "<a  x = 'old'\ty=\"2\"  z='3' />"
    assert edit(text, lambda d: d.root.set("x", "new")) == "<a  x = 'new'\ty=\"2\"  z='3' />"
    assert edit(text, lambda d: d.root.set("y", "5")) == "<a  x = 'old'\ty=\"5\"  z='3' />"


def test_set_to_the_same_value_leaves_the_raw_text():
    text = '<a t="&quot;q&quot; &amp; &#65;"/>'
    assert edit(text, lambda d: d.root.set("t", '"q" & A')) == text


def test_set_escapes_what_it_writes():
    assert edit("<a t='x'/>", lambda d: d.root.set("t", "it's <&> \"q\"")) == "<a t='it&apos;s &lt;&amp;> \"q\"'/>"
    assert edit('<a t="x"/>', lambda d: d.root.set("t", 'it\'s <&> "q"\n\t')) == '<a t="it\'s &lt;&amp;> &quot;q&quot;&#10;&#9;"/>'
    assert edit("<a/>", lambda d: d.root.set("t", "a&b")) == '<a t="a&amp;b"/>'


def test_new_attribute_goes_last_on_the_same_line():
    assert edit('<a x="1" y="2">t</a>', lambda d: d.root.set("z", "3")) == '<a x="1" y="2" z="3">t</a>'
    assert edit('<a x="1" />', lambda d: d.root.set("z", "3")) == '<a x="1" z="3" />'
    assert edit("<a>t</a>", lambda d: d.root.set("z", "3")) == '<a z="3">t</a>'
    assert edit("<a/>", lambda d: d.root.set("z", "3")) == '<a z="3"/>'


def test_new_attribute_of_a_multi_line_tag_goes_on_a_line_of_its_own():
    text = '<r>\n  <scene\n      type="Scene"\n      name="Main">\n    <n/>\n  </scene>\n</r>\n'
    expected = '<r>\n  <scene\n      type="Scene"\n      name="Main"\n      contentVersion="2">\n    <n/>\n  </scene>\n</r>\n'
    assert edit(text, lambda d: d.root.child("scene").set("contentVersion", "2")) == expected
    # the style of the file: CRLF stays, a tab indentation stays, the tag closed on its own line keeps that
    crlf = '<a\r\n\tx="1"\r\n/>\r\n'
    assert edit(crlf, lambda d: d.root.set("y", "2")) == '<a\r\n\tx="1"\r\n\ty="2"\r\n/>\r\n'
    mixed = '<node name="Menu"\n      layer="10">\n</node>\n'
    assert edit(mixed, lambda d: d.root.set("k", "v")) == '<node name="Menu"\n      layer="10"\n      k="v">\n</node>\n'


def test_remove_an_attribute_takes_its_whitespace_with_it():
    text = '<a x="1" y="2" z="3"/>'
    assert edit(text, lambda d: d.root.remove("y")) == '<a x="1" z="3"/>'
    assert edit(text, lambda d: d.root.remove("x")) == '<a y="2" z="3"/>'
    assert edit(text, lambda d: d.root.remove("z")) == '<a x="1" y="2"/>'
    assert edit('<a x="1" />', lambda d: d.root.remove("x")) == "<a />"
    multi = '<a\n  x="1"\n  y="2"\n  z="3">\n</a>'
    assert edit(multi, lambda d: d.root.remove("y")) == '<a\n  x="1"\n  z="3">\n</a>'
    assert edit(multi, lambda d: d.root.remove("x")) == '<a\n  y="2"\n  z="3">\n</a>'
    assert edit(text, lambda d: d.root.remove("nope")) == text


def test_remove_reports_whether_there_was_an_attribute():
    doc = xmltext.parse('<a x="1"/>')
    assert doc.root.remove("x") is True and doc.root.remove("x") is False


def test_set_then_remove_then_set_again_puts_the_attribute_last():
    assert edit('<a x="1" y="2"/>', lambda d: (d.root.remove("x"), d.root.set("x", "9"))) == '<a y="2" x="9"/>'


def test_set_type_changes_the_element_type_in_place():
    text = '<element type="UIButton" name="Play" text="Go" />'
    assert edit(text, lambda d: d.root.set("type", "UIElement")) == '<element type="UIElement" name="Play" text="Go" />'


def test_rename_changes_both_tags():
    assert edit("<a x='1'>t</a >", lambda d: d.root.rename("b")) == "<b x='1'>t</b >"


def test_set_wants_a_string():
    with pytest.raises(TypeError):
        xmltext.parse("<a/>").root.set("x", 5)


# ---- children -----------------------------------------------------------------------------------------------------------------

NESTED = '<?xml version="1.0"?>\n<r>\n  <p>\n    <a/>\n    <b/>\n  </p>\n</r>\n'


def test_add_child_takes_the_indentation_of_its_siblings():
    got = edit(NESTED, lambda d: d.root.child("p").add_child('<c x="1"/>'))
    assert got == '<?xml version="1.0"?>\n<r>\n  <p>\n    <a/>\n    <b/>\n    <c x="1"/>\n  </p>\n</r>\n'
    got = edit(NESTED, lambda d: d.root.child("p").add_child("<c/>", 0))
    assert got == '<?xml version="1.0"?>\n<r>\n  <p>\n    <c/>\n    <a/>\n    <b/>\n  </p>\n</r>\n'
    got = edit(NESTED, lambda d: d.root.child("p").add_child("<c/>", 1))
    assert got == '<?xml version="1.0"?>\n<r>\n  <p>\n    <a/>\n    <c/>\n    <b/>\n  </p>\n</r>\n'
    got = edit(NESTED, lambda d: d.root.child("p").add_child("<c/>", 2))
    assert got == '<?xml version="1.0"?>\n<r>\n  <p>\n    <a/>\n    <b/>\n    <c/>\n  </p>\n</r>\n'


def test_add_child_to_a_parent_without_children_goes_one_level_deeper():
    text = '<r>\n    <p>\n    </p>\n</r>\n'
    assert edit(text, lambda d: d.root.child("p").add_child("<c/>")) == "<r>\n    <p>\n        <c/>\n    </p>\n</r>\n"
    text = "<r>\n\t<p></p>\n</r>\n"
    assert edit(text, lambda d: d.root.child("p").add_child("<c/>")) == "<r>\n\t<p>\n\t\t<c/>\n\t</p>\n</r>\n"
    assert edit("<r>\n  <p>text</p>\n</r>\n", lambda d: d.root.child("p").add_child("<c/>")) == "<r>\n  <p>text\n    <c/>\n  </p>\n</r>\n"


def test_add_child_opens_a_self_closing_parent():
    text = '<r>\n  <p a="1" />\n</r>\n'
    assert edit(text, lambda d: d.root.child("p").add_child('<c/>')) == '<r>\n  <p a="1">\n    <c/>\n  </p>\n</r>\n'
    assert edit("<r/>", lambda d: d.root.add_child("<c/>")) == "<r>\n  <c/>\n</r>"


def test_add_child_goes_after_a_trailing_comment_and_keeps_the_comments():
    text = "<p>\n  <a/>\n  <!-- last -->\n</p>\n"
    assert edit(text, lambda d: d.root.add_child("<b/>")) == "<p>\n  <a/>\n  <!-- last -->\n  <b/>\n</p>\n"
    text = "<p>\n  <!-- only -->\n</p>\n"
    assert edit(text, lambda d: d.root.add_child("<b/>")) == "<p>\n  <!-- only -->\n  <b/>\n</p>\n"


def test_add_child_to_an_inline_parent_stays_inline():
    assert edit("<p><a/></p>", lambda d: d.root.add_child("<b/>")) == "<p><a/><b/></p>"
    assert edit("<p><a/></p>", lambda d: d.root.add_child("<b/>", 0)) == "<p><b/><a/></p>"


def test_add_child_uses_the_line_ending_and_the_unit_of_the_file():
    text = "<r>\r\n\t<p>\r\n\t</p>\r\n</r>\r\n"
    assert edit(text, lambda d: d.root.child("p").add_child("<c/>")) == "<r>\r\n\t<p>\r\n\t\t<c/>\r\n\t</p>\r\n</r>\r\n"


def test_add_child_expands_a_compact_fragment_and_indents_a_multi_line_one():
    text = "<r>\n  <p>\n    <a/>\n  </p>\n</r>\n"
    got = edit(text, lambda d: d.root.child("p").add_child('<e t="1"><cs><c a="1"/><c a="2"/></cs></e>'))
    assert got == (
        '<r>\n  <p>\n    <a/>\n    <e t="1">\n      <cs>\n        <c a="1"/>\n        <c a="2"/>\n      </cs>\n    </e>\n  </p>\n</r>\n'
    )
    got = edit(text, lambda d: d.root.child("p").add_child('<e>\n  <c/>\n</e>', 0))
    assert got == "<r>\n  <p>\n    <e>\n      <c/>\n    </e>\n    <a/>\n  </p>\n</r>\n"
    got = edit(text, lambda d: d.root.child("p").add_child("<e><c/></e>", expand=False))
    assert got == "<r>\n  <p>\n    <a/>\n    <e><c/></e>\n  </p>\n</r>\n"


def test_add_child_returns_the_attached_element():
    doc = xmltext.parse("<r>\n  <a/>\n</r>")
    c = doc.root.add_child('<c x="1"/>')
    assert c.parent is doc.root and c.get("x") == "1" and c.line is None
    c.set("y", "2")
    assert doc.to_text() == '<r>\n  <a/>\n  <c x="1" y="2"/>\n</r>'


def test_add_child_rejects_bad_fragments_and_positions():
    doc = xmltext.parse("<r><a/></r>")
    with pytest.raises(XmlTextError):
        doc.root.add_child("<a/><b/>")
    with pytest.raises(XmlTextError):
        doc.root.add_child("text")
    with pytest.raises(IndexError):
        doc.root.add_child("<b/>", 2)


def test_remove_child_takes_its_line_with_it():
    text = "<p>\n  <a/>\n  <b/>\n  <c/>\n</p>\n"
    assert edit(text, lambda d: d.root.remove_child(d.root.child("b"))) == "<p>\n  <a/>\n  <c/>\n</p>\n"
    assert edit(text, lambda d: d.root.remove_child(d.root.child("a"))) == "<p>\n  <b/>\n  <c/>\n</p>\n"
    assert edit(text, lambda d: d.root.remove_child(d.root.child("c"))) == "<p>\n  <a/>\n  <b/>\n</p>\n"
    assert edit("<p><a/><b/></p>", lambda d: d.root.remove_child(d.root.child("a"))) == "<p><b/></p>"
    assert edit("<p>\n  <!-- k -->\n  <a/>\n</p>\n", lambda d: d.root.remove_child(d.root.child("a"))) == "<p>\n  <!-- k -->\n</p>\n"


def test_removing_the_only_child_leaves_the_closing_indentation():
    got = edit("<r>\n  <p>\n    <a/>\n  </p>\n</r>\n", lambda d: d.root.child("p").remove_child(d.root.child("p").child("a")))
    assert got == "<r>\n  <p>\n  </p>\n</r>\n"


def test_replace_children_replaces_everything_and_lays_out_the_new_ones():
    text = "<r>\n  <p>\n    <!-- old -->\n    <a/>\n    text\n  </p>\n</r>\n"
    got = edit(text, lambda d: d.root.child("p").replace_children(['<x n="1"/>', '<y><z/></y>']))
    assert got == '<r>\n  <p>\n    <x n="1"/>\n    <y>\n      <z/>\n    </y>\n  </p>\n</r>\n'
    got = edit(text, lambda d: d.root.child("p").replace_children([]))
    assert got == "<r>\n  <p />\n</r>\n"
    got = edit('<r>\n  <p k="1"/>\n</r>\n', lambda d: d.root.child("p").replace_children(["<x/>"]))
    assert got == '<r>\n  <p k="1">\n    <x/>\n  </p>\n</r>\n'


def test_a_removed_child_is_detached_and_iteration_is_a_snapshot():
    doc = xmltext.parse("<r>\n  <a/>\n  <b/>\n</r>")
    seen = []
    for e in doc.iter():
        seen.append(e.name)
        if e.name == "r":
            doc.root.remove_child(doc.root.child("a"))
    assert seen == ["r", "a", "b"]
    assert doc.to_text() == "<r>\n  <b/>\n</r>"


def test_a_whole_edit_session_on_a_scene_shaped_file():
    """The shape of the engine's scenes: a comment, a multi-line scene tag, long one-line elements."""
    text = (
        '<?xml version="1.0"?>\n<SceneManager>\n  <scenes>\n    <!-- The first screen. -->\n    <scene\n      type="Scene"\n'
        '      name="MainMenu">\n      <nodes>\n        <node name="Menu">\n          <element type="UIButton" name="Play" text="Play" />\n'
        "        </node>\n      </nodes>\n    </scene>\n  </scenes>\n</SceneManager>\n"
    )

    def work(doc):
        doc.root.set("contentVersion", "2")
        button = doc.find_all(lambda e: e.get("type") == "UIButton")[0]
        button.set("type", "UIElement")
        button.remove("text")
        button.add_child("<components><component type=\"UITextComponent\" text=\"Play\"/></components>")

    assert edit(text, work) == (
        '<?xml version="1.0"?>\n<SceneManager contentVersion="2">\n  <scenes>\n    <!-- The first screen. -->\n    <scene\n      type="Scene"\n'
        '      name="MainMenu">\n      <nodes>\n        <node name="Menu">\n          <element type="UIElement" name="Play">\n'
        '            <components>\n              <component type="UITextComponent" text="Play"/>\n            </components>\n'
        "          </element>\n        </node>\n      </nodes>\n    </scene>\n  </scenes>\n</SceneManager>\n"
    )
