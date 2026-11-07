"""What the parser makes of BE_FUNCTION declarations: the right MethodData, or a warning and no method."""

import pytest


def methods_of(parse, body):
    classes, warnings = parse("struct Foo {\n    BE_CLASS(Foo)\n" + body + "\n};\n")
    assert len(classes) == 1
    return classes[0].methods, warnings


def one(parse, declaration):
    methods, warnings = methods_of(parse, "    " + declaration)
    assert len(methods) == 1, warnings
    assert warnings == []
    return methods[0]


def kinds_and_lines(warnings):
    return [(w.kind, w.line) for w in warnings]


# ---------------------------------------------------------------------------
# qualifiers
# ---------------------------------------------------------------------------

def test_plain_method(parse):
    method = one(parse, "BE_FUNCTION int32_t TakeDamage(int32_t amount);")
    assert (method.name, method.return_type) == ("TakeDamage", "int32_t")
    assert [(p.name, p.type_name) for p in method.params] == [("amount", "int32_t")]
    assert not (method.is_const or method.is_virtual or method.is_override)


def test_const_method(parse):
    assert one(parse, "BE_FUNCTION bool IsDead() const;").is_const


def test_noexcept_does_not_hide_const_and_is_not_const(parse):
    assert one(parse, "BE_FUNCTION int Get() const noexcept;").is_const
    assert not one(parse, "BE_FUNCTION void Run() noexcept;").is_const


def test_noexcept_with_a_condition_keeps_the_parameters(parse):
    method = one(parse, "BE_FUNCTION void Run(int a) const noexcept(noexcept(Helper()));")
    assert method.is_const
    assert [(p.name, p.type_name) for p in method.params] == [("a", "int")]


def test_override(parse):
    method = one(parse, "BE_FUNCTION void Draw() override;")
    assert method.is_override and not method.is_virtual
    method = one(parse, "BE_FUNCTION void Draw() const override;")
    assert method.is_override and method.is_const


def test_virtual_and_pure_virtual(parse):
    method = one(parse, "BE_FUNCTION virtual void Update() = 0;")
    assert (method.name, method.return_type, method.is_virtual) == ("Update", "void", True)
    method = one(parse, "BE_FUNCTION virtual int Get() const = 0;")
    assert method.is_const and method.is_virtual


def test_default_and_delete_keep_the_name(parse):
    methods, warnings = methods_of(parse, "    BE_FUNCTION void A() = default;\n    BE_FUNCTION void B() = delete;")
    assert [m.name for m in methods] == ["A", "B"]
    assert warnings == []


def test_static_stays_in_the_return_type(parse):
    # Pinned: a static function is no member function pointer; the parser does not judge that.
    assert one(parse, "BE_FUNCTION static int Count();").return_type == "static int"


def test_attribute_is_dropped(parse):
    method = one(parse, "BE_FUNCTION [[nodiscard]] int Get() const;")
    assert (method.return_type, method.name, method.is_const) == ("int", "Get", True)


def test_declaration_over_several_lines(parse):
    method = one(parse, "BE_FUNCTION void Run(\n        int a,\n        int b\n    ) const;")
    assert [p.name for p in method.params] == ["a", "b"]
    assert method.is_const


# ---------------------------------------------------------------------------
# return types
# ---------------------------------------------------------------------------

def test_trailing_return_type(parse):
    method = one(parse, "BE_FUNCTION auto Get(int a) const -> eastl::string;")
    assert (method.name, method.return_type, method.is_const) == ("Get", "auto", True)


def test_trailing_return_type_with_a_function_type(parse):
    method = one(parse, "BE_FUNCTION auto Get() -> std::function<void(int)>;")
    assert (method.name, method.params) == ("Get", [])


def test_function_type_in_the_return_type(parse):
    method = one(parse, "BE_FUNCTION std::function<void(int)> Make(int a) const;")
    assert (method.name, method.return_type, method.is_const) == ("Make", "std::function<void(int)>", True)
    assert [p.name for p in method.params] == ["a"]


def test_decltype_return_type(parse):
    method = one(parse, "BE_FUNCTION decltype(auto) Get(int a) const;")
    assert (method.name, method.return_type) == ("Get", "decltype(auto)")
    assert [p.name for p in method.params] == ["a"]


def test_template_method_warns(parse):
    # `&Class::Name` cannot name a template without arguments, so the generated table could not use it.
    methods, warnings = methods_of(parse, "    BE_FUNCTION template<typename T> void Run(T value);")
    assert methods == []
    assert kinds_and_lines(warnings) == [("method", 3)]


# ---------------------------------------------------------------------------
# parameters
# ---------------------------------------------------------------------------

def test_reference_and_pointer_parameters(parse):
    method = one(parse, "BE_FUNCTION void Run(const eastl::string& a, Foo* b, Foo*& c, Foo&& d);")
    assert [(p.name, p.type_name) for p in method.params] == [
        ("a", "const eastl::string&"), ("b", "Foo*"), ("c", "Foo*&"), ("d", "Foo&&")]


def test_unnamed_parameters(parse):
    method = one(parse, "BE_FUNCTION void Run(int, const Foo&, const Foo, unsigned);")
    assert [(p.name, p.type_name) for p in method.params] == [
        ("", "int"), ("", "const Foo&"), ("", "const Foo"), ("", "unsigned")]


def test_default_arguments_with_commas_and_braces(parse):
    method = one(parse, "BE_FUNCTION void Run(std::pair<int, int> p = std::pair<int, int>{1, 2}, int b = 3);")
    assert [(p.name, p.type_name) for p in method.params] == [("p", "std::pair<int, int>"), ("b", "int")]


def test_default_argument_with_a_braced_value_and_a_call(parse):
    method = one(parse, 'BE_FUNCTION void Run(Foo f = Foo{1, 2}, const char* s = ")", int z = Make(1, 2));')
    assert [(p.name, p.type_name) for p in method.params] == [("f", "Foo"), ("s", "const char*"), ("z", "int")]


def test_parameter_that_is_a_function_type(parse):
    method = one(parse, "BE_FUNCTION void Run(eastl::function<void(int)> callback, int b);")
    assert [p.name for p in method.params] == ["callback", "b"]


# ---------------------------------------------------------------------------
# names
# ---------------------------------------------------------------------------

def test_call_operator_is_named_operator_call(parse):
    method = one(parse, "BE_FUNCTION bool operator()(int x) const;")
    assert (method.name, method.return_type, method.is_const) == ("operator()", "bool", True)
    assert [p.name for p in method.params] == ["x"]


@pytest.mark.parametrize("declaration", [
    "BE_FUNCTION bool operator==(const Foo& other) const;",
    "BE_FUNCTION bool operator<(const Foo& other) const;",
    "BE_FUNCTION Foo& operator=(const Foo& other);",
])
def test_other_operators_warn(parse, declaration):
    methods, warnings = methods_of(parse, "    " + declaration)
    assert methods == []
    assert kinds_and_lines(warnings) == [("method", 3)]


def test_constructor_without_a_return_type_warns(parse):
    methods, warnings = methods_of(parse, "    BE_FUNCTION Foo();")
    assert methods == []
    assert kinds_and_lines(warnings) == [("method", 3)]


def test_longer_name_ending_in_the_marker_is_not_a_marker(parse):
    methods, warnings = methods_of(parse, "    MY_BE_FUNCTION void Hidden();\n    BE_FUNCTION_X void Hidden2();\n    BE_FUNCTION void Shown();")
    assert [m.name for m in methods] == ["Shown"]
    assert warnings == []


# ---------------------------------------------------------------------------
# inline bodies (the shape of IWidget::DrawScreen)
# ---------------------------------------------------------------------------

def test_inline_body_followed_by_another_marker_gives_two_methods(parse):
    classes, warnings = parse("""
        struct IWidget {
            BE_CLASS(IWidget)
            BE_FUNCTION virtual bool Initialize(IDeserializer& deserializer) = 0;

            BE_FUNCTION virtual void Update() = 0;
            BE_FUNCTION virtual void DrawScreen() {}
            BE_FUNCTION virtual void Draw() = 0;
        };
    """)
    methods = classes[0].methods
    assert [m.name for m in methods] == ["Initialize", "Update", "DrawScreen", "Draw"]
    assert methods[2].params == [] and methods[2].return_type == "void"
    assert [m.line for m in methods] == [3, 5, 6, 7]
    assert warnings == []


def test_inline_bodies_with_nested_braces_strings_and_a_trailing_semicolon(parse):
    methods, warnings = methods_of(parse, """
    BE_FUNCTION int A() const { if (x) { return 1; } return 2; };
    BE_FUNCTION void B() { const char* s = "}"; Step(); }
    BE_FUNCTION void C();
    """)
    assert [(m.name, m.is_const) for m in methods] == [("A", True), ("B", False), ("C", False)]
    assert warnings == []


def test_inline_body_does_not_confuse_a_following_field(parse):
    classes, warnings = parse("""
        struct Foo {
            BE_CLASS(Foo)
            BE_FUNCTION void Run() { Step(); }
            BE_REFLECT_FIELD int a = 0;
        };
    """)
    assert [m.name for m in classes[0].methods] == ["Run"]
    assert [f.name for f in classes[0].fields] == ["a"]
    assert warnings == []


def test_braced_default_argument_is_not_an_inline_body(parse):
    method = one(parse, "BE_FUNCTION void Run(Foo f = Foo{1, 2});")
    assert method.name == "Run"


# ---------------------------------------------------------------------------
# broken declarations
# ---------------------------------------------------------------------------

def test_missing_semicolon_swallowing_the_next_marker_warns_once(parse):
    methods, warnings = methods_of(parse, "    BE_FUNCTION void A()\n    BE_FUNCTION void B();")
    assert methods == []
    assert kinds_and_lines(warnings) == [("method", 3)]


def test_marker_at_the_end_without_a_declaration_warns(parse):
    methods, warnings = methods_of(parse, "    BE_FUNCTION void A()")
    assert methods == []
    assert kinds_and_lines(warnings) == [("method", 3)]


def test_method_in_a_nested_class_warns(parse):
    classes, warnings = parse("""
        struct Outer {
            BE_CLASS(Outer)
            struct Inner {
                BE_FUNCTION void Go();
            };
        };
    """)
    assert kinds_and_lines(warnings) == [("method", 4)]
    assert "'Inner'" in warnings[0].message
