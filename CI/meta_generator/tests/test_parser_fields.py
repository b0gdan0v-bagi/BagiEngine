"""What the parser makes of BE_REFLECT_FIELD declarations: the right (type, name), or a warning and no field."""

import pytest


def fields_of(parse, body):
    classes, warnings = parse("struct Foo {\n    BE_CLASS(Foo)\n" + body + "\n};\n")
    assert len(classes) == 1
    return [(f.name, f.type_name) for f in classes[0].fields], warnings


def kinds_and_lines(warnings):
    return [(w.kind, w.line) for w in warnings]


# ---------------------------------------------------------------------------
# types
# ---------------------------------------------------------------------------

@pytest.mark.parametrize("declaration, expected", [
    ("BE_REFLECT_FIELD int a;", ("a", "int")),
    ("BE_REFLECT_FIELD const int a = 0;", ("a", "const int")),
    ("BE_REFLECT_FIELD Foo* p = nullptr;", ("p", "Foo*")),
    ("BE_REFLECT_FIELD ns::Type<int>::Inner x;", ("x", "ns::Type<int>::Inner")),
    ("BE_REFLECT_FIELD eastl::map<A, B<C>> m;", ("m", "eastl::map<A, B<C>>")),
    ("BE_REFLECT_FIELD eastl::vector<eastl::vector<int>> v = {};", ("v", "eastl::vector<eastl::vector<int>>")),
    ("BE_REFLECT_FIELD eastl::function<int()> f;", ("f", "eastl::function<int()>")),
    ("BE_REFLECT_FIELD unsigned long long big = 0;", ("big", "unsigned long long")),
])
def test_type_and_name(parse, declaration, expected):
    fields, warnings = fields_of(parse, declaration)
    assert fields == [expected]
    assert warnings == []


# The aliases of BECore/Types: spelled by the alias the parser sees the same shapes as for the eastl:: names
@pytest.mark.parametrize("declaration, expected, primitive", [
    ("BE_REFLECT_FIELD String s;", ("s", "String"), True),
    ("BE_REFLECT_FIELD BECore::String s;", ("s", "BECore::String"), True),
    ("BE_REFLECT_FIELD FixedString<32> s;", ("s", "FixedString<32>"), True),
    ("BE_REFLECT_FIELD FixedString<32, false> s;", ("s", "FixedString<32, false>"), True),
    ("BE_REFLECT_FIELD Vector<int32_t> v;", ("v", "Vector<int32_t>"), False),
    ("BE_REFLECT_FIELD SmallVector<Vector<int32_t>, 4> v;", ("v", "SmallVector<Vector<int32_t>, 4>"), False),
    ("BE_REFLECT_FIELD FixedVector<int32_t, 4> v;", ("v", "FixedVector<int32_t, 4>"), False),
    ("BE_REFLECT_FIELD Array<int32_t, 3> a;", ("a", "Array<int32_t, 3>"), False),
    ("BE_REFLECT_FIELD HashMap<String, int32_t> m;", ("m", "HashMap<String, int32_t>"), False),
    ("BE_REFLECT_FIELD Map<String, Vector<int32_t>> m;", ("m", "Map<String, Vector<int32_t>>"), False),
    ("BE_REFLECT_FIELD FixedHashMap<String, int32_t, 8> m;", ("m", "FixedHashMap<String, int32_t, 8>"), False),
    ("BE_REFLECT_FIELD Optional<int32_t> o;", ("o", "Optional<int32_t>"), False),
    ("BE_REFLECT_FIELD Pair<int32_t, String> p;", ("p", "Pair<int32_t, String>"), False),
    ("BE_REFLECT_FIELD Vector<Pair<String, Array<float, 3>>> v;", ("v", "Vector<Pair<String, Array<float, 3>>>"), False),
])
def test_alias_type_and_name(parse, declaration, expected, primitive):
    classes, warnings = parse("struct Foo {\n    BE_CLASS(Foo)\n    " + declaration + "\n};\n")
    assert warnings == []
    assert [(f.name, f.type_name, f.is_primitive) for f in classes[0].fields] == [(*expected, primitive)]


@pytest.mark.parametrize("type_name, inner", [
    ("eastl::vector<IntrusivePtrAtomic<ILogSink>>", "ILogSink"),
    ("Vector<IntrusivePtrAtomic<ILogSink>>", "ILogSink"),
    ("SmallVector<IntrusivePtrNonAtomic<Tests::ITest>, 4>", "Tests::ITest"),
    ("Optional<IntrusivePtrNonAtomic<Tests::ITest>>", "Tests::ITest"),
    ("Vector<int32_t>", None),
])
def test_factory_base_is_found_in_a_container_of_pointers(type_name, inner):
    from core.generator import extract_intrusive_inner_type
    assert extract_intrusive_inner_type(type_name) == inner


# ---------------------------------------------------------------------------
# initializers
# ---------------------------------------------------------------------------

@pytest.mark.parametrize("initializer", [
    "= 0",
    "{}",
    "{20, 20, 100, 255}",
    "= {20, 20, 100, 255}",
    "= a < c",
    "= a < c && d > e",
    "= x ? 1 : 2",
    '= "http://x"',
    '= "// not a comment"',
    '= "}"',
    "= f(1, 2)",
    "= eastl::pair<int, int>(1, 2)",
    "= eastl::pair<int, int>{1, 2}",
    "= []() { int x = 1; return x; }",
    "= [](int a, int b) { return a < b; }",
])
def test_initializers_do_not_change_the_field(parse, initializer):
    fields, warnings = fields_of(parse, f"    BE_REFLECT_FIELD Thing value {initializer};\n    BE_REFLECT_FIELD int after = 0;")
    assert fields == [("value", "Thing"), ("after", "int")]
    assert warnings == []


def test_declaration_over_several_lines(parse):
    fields, warnings = fields_of(parse, """
    BE_REFLECT_FIELD eastl::vector<
        int>
        v
        = {};
    """)
    assert fields == [("v", "eastl::vector< int>")]
    assert warnings == []


def test_lambda_with_semicolons_ends_at_the_outer_semicolon(parse):
    fields, warnings = fields_of(parse, """
    BE_REFLECT_FIELD eastl::function<int()> f = []() { int x = 1; return x; };
    BE_REFLECT_FIELD int after = 0;
    """)
    assert fields == [("f", "eastl::function<int()>"), ("after", "int")]
    assert warnings == []


# ---------------------------------------------------------------------------
# qualifiers and attributes
# ---------------------------------------------------------------------------

def test_mutable_is_dropped_from_the_type(parse):
    fields, _ = fields_of(parse, "    BE_REFLECT_FIELD mutable int m = 0;")
    assert fields == [("m", "int")]


def test_attribute_is_dropped_from_the_type(parse):
    fields, _ = fields_of(parse, "    BE_REFLECT_FIELD [[maybe_unused]] int a = 0;")
    assert fields == [("a", "int")]


def test_static_constexpr_stay_in_the_type(parse):
    # Pinned: a static member is not an instance field and the generated code will not compile for it;
    # the parser does not judge that, the type just carries the words.
    fields, warnings = fields_of(parse, "    BE_REFLECT_FIELD static constexpr int k = 3;")
    assert fields == [("k", "static constexpr int")]
    assert warnings == []


# ---------------------------------------------------------------------------
# what cannot be split: warning, no field
# ---------------------------------------------------------------------------

@pytest.mark.parametrize("declaration", [
    "BE_REFLECT_FIELD float _m[16];",
    "BE_REFLECT_FIELD float _m[16] = {};",
    "BE_REFLECT_FIELD int grid[4][4];",
    "BE_REFLECT_FIELD void (*callback)(int) = nullptr;",
])
def test_arrays_and_function_pointers_warn(parse, declaration):
    fields, warnings = fields_of(parse, declaration + "\n    BE_REFLECT_FIELD int ok = 0;")
    assert fields == [("ok", "int")]
    assert kinds_and_lines(warnings) == [("field", 3)]
    assert "skipped" in warnings[0].message


@pytest.mark.parametrize("declaration", [
    "BE_REFLECT_FIELD int a : 3;",
    "BE_REFLECT_FIELD unsigned a:3 = 0;",
])
def test_bitfields_warn(parse, declaration):
    fields, warnings = fields_of(parse, declaration)
    assert fields == []
    assert kinds_and_lines(warnings) == [("field", 3)]
    assert "bitfield" in warnings[0].message


@pytest.mark.parametrize("declaration", [
    "BE_REFLECT_FIELD int a, b;",
    "BE_REFLECT_FIELD int a = 1, b = 2;",
    "BE_REFLECT_FIELD int* a = nullptr, *b = nullptr;",
    "BE_REFLECT_FIELD int a{1}, b{2};",
])
def test_several_declarators_warn_and_give_no_wrong_field(parse, declaration):
    fields, warnings = fields_of(parse, declaration)
    assert fields == []
    assert kinds_and_lines(warnings) == [("field", 3)]
    assert "several declarators" in warnings[0].message


@pytest.mark.parametrize("declaration", [
    "BE_REFLECT_FIELD int a = f(1, 2);",
    "BE_REFLECT_FIELD eastl::pair<int, int> p;",
    "BE_REFLECT_FIELD eastl::pair<int, int> p = eastl::pair<int, int>(1, 2);",
    "BE_REFLECT_FIELD Color c{1, 2, 3, 4};",
])
def test_commas_inside_brackets_are_not_declarators(parse, declaration):
    fields, warnings = fields_of(parse, declaration)
    assert len(fields) == 1
    assert warnings == []


def test_field_without_a_semicolon_warns(parse):
    fields, warnings = fields_of(parse, "    BE_REFLECT_FIELD int a = 0")
    assert fields == []
    assert kinds_and_lines(warnings) == [("field", 3)]
    assert "no ';'" in warnings[0].message


def test_field_without_a_type_warns(parse):
    fields, warnings = fields_of(parse, "    BE_REFLECT_FIELD lonely;")
    assert fields == []
    assert kinds_and_lines(warnings) == [("field", 3)]


# ---------------------------------------------------------------------------
# where the marker sits
# ---------------------------------------------------------------------------

def test_field_in_a_nested_class_warns(parse):
    classes, warnings = parse("""
        struct Outer {
            BE_CLASS(Outer)
            struct Inner {
                BE_REFLECT_FIELD int x = 0;
            };
            BE_REFLECT_FIELD int y = 0;
        };
    """)
    assert kinds_and_lines(warnings) == [("field", 4)]
    assert "'Inner'" in warnings[0].message


def test_field_in_a_template_class_is_read(parse):
    classes, warnings = parse("""
        template<typename T>
        struct Box {
            BE_CLASS(Box)
            BE_REFLECT_FIELD T value;
            BE_REFLECT_FIELD eastl::vector<T> many;
        };
    """)
    assert [(f.name, f.type_name) for f in classes[0].fields] == [
        ("value", "T"), ("many", "eastl::vector<T>")]
    assert warnings == []


def test_longer_name_ending_in_the_marker_is_not_a_marker(parse):
    classes, warnings = parse("""
        struct Foo {
            BE_CLASS(Foo)
            MY_BE_REFLECT_FIELD int hidden = 0;
            BE_REFLECT_FIELD_X int hidden2 = 0;
            BE_REFLECT_FIELD int shown = 0;
        };
    """)
    assert [f.name for f in classes[0].fields] == ["shown"]
    assert warnings == []


def test_enum_fields_are_flagged(tmp_path):
    from core.parser import create_parser
    header = tmp_path / "E.h"
    header.write_text("struct Foo {\n BE_CLASS(Foo)\n BE_REFLECT_FIELD Mode m = Mode::A;\n"
                      " BE_REFLECT_FIELD ns::Mode n = ns::Mode::A;\n BE_REFLECT_FIELD int i = 0;\n};\n",
                      encoding="utf-8")
    classes, _ = create_parser([], {"Mode"}).parse_file(header)
    assert [(f.name, f.is_enum) for f in classes[0].fields] == [("m", True), ("n", True), ("i", False)]


def test_field_line_is_the_line_of_the_marker(parse):
    classes, _ = parse("""
        // header
        struct Foo {
            BE_CLASS(Foo)

            BE_REFLECT_FIELD int a = 0;
        };
    """)
    assert classes[0].line == 2
    assert classes[0].fields[0].line == 5
