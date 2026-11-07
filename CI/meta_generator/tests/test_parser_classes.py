"""What the parser makes of class declarations: the right ClassData, or a warning, never a silent wrong result."""

import pytest


def only(classes):
    assert len(classes) == 1
    return classes[0]


def kinds_and_lines(warnings):
    return [(w.kind, w.line) for w in warnings]


# ---------------------------------------------------------------------------
# class forms
# ---------------------------------------------------------------------------

def test_plain_struct(parse):
    classes, warnings = parse("""
        struct Foo {
            BE_CLASS(Foo)
        };
    """)
    cls = only(classes)
    assert (cls.name, cls.qualified_name, cls.full_qualified_name, cls.namespace) == ("Foo", "Foo", "Foo", "")
    assert cls.parent_class is None
    assert not cls.is_event and not cls.is_abstract and not cls.is_factory_base
    assert cls.line == 1
    assert warnings == []


def test_plain_class_with_access_specifier(parse):
    classes, warnings = parse("""
        class Foo {
            BE_CLASS(Foo)
        public:
            BE_REFLECT_FIELD int a = 0;
        };
    """)
    assert [f.name for f in only(classes).fields] == ["a"]
    assert warnings == []


def test_parent_class_is_the_first_base_without_access_namespace_or_template_arguments(parse):
    classes, _ = parse("""
        struct Foo : public ns::Base<int>, private Other {
            BE_CLASS(Foo)
        };
    """)
    assert only(classes).parent_class == "Base"


def test_event_marker(parse):
    classes, warnings = parse("""
        struct Hit : EventBase<Hit> {
            BE_EVENT(Hit)
            BE_REFLECT_FIELD int damage = 0;
        };
    """)
    cls = only(classes)
    assert cls.is_event and cls.parent_class == "EventBase"
    assert warnings == []


def test_pure_virtual_method_makes_the_class_abstract(parse):
    classes, _ = parse("""
        struct IFoo {
            BE_CLASS(IFoo)
            virtual void Run() const = 0;
        };
    """)
    assert only(classes).is_abstract


def test_final_is_not_recognised_and_warns(parse):
    classes, warnings = parse("""
        class Foo final : public Bar {
            BE_CLASS(Foo)
        };
    """)
    assert classes == []
    assert kinds_and_lines(warnings) == [("class", 2)]


def test_export_macro_is_not_recognised_and_warns(parse):
    classes, warnings = parse("""
        class MY_API Foo {
            BE_CLASS(Foo)
        };
    """)
    assert classes == []
    assert kinds_and_lines(warnings) == [("class", 2)]


def test_alignas_is_not_recognised_and_warns(parse):
    classes, warnings = parse("""
        class alignas(16) Foo {
            BE_CLASS(Foo)
        };
    """)
    assert classes == []
    assert kinds_and_lines(warnings) == [("class", 2)]


def test_attribute_before_the_name_is_not_recognised_and_warns(parse):
    classes, warnings = parse("""
        class [[nodiscard]] Foo {
            BE_CLASS(Foo)
        };
    """)
    assert classes == []
    assert kinds_and_lines(warnings) == [("class", 2)]


def test_class_template_is_reflected_by_its_name(parse):
    # The template header is no obstacle: the regex only looks at `struct Foo {`.
    classes, warnings = parse("""
        template<typename T>
        struct Foo {
            BE_CLASS(Foo)
            BE_REFLECT_FIELD T value;
        };
    """)
    assert [f.name for f in only(classes).fields] == ["value"]
    assert warnings == []


def test_template_specialization_is_not_recognised_and_warns_for_class_and_field(parse):
    classes, warnings = parse("""
        template<>
        struct Foo<int> {
            BE_CLASS(Foo)
            BE_REFLECT_FIELD int value = 0;
        };
    """)
    assert classes == []
    assert kinds_and_lines(warnings) == [("class", 3), ("field", 4)]


def test_forward_declaration_and_friend_are_not_classes(parse):
    classes, warnings = parse("""
        class Foo;
        struct Bar {
            friend class Foo;
            BE_CLASS(Bar)
        };
    """)
    assert [c.name for c in classes] == ["Bar"]
    assert warnings == []


def test_enum_class_in_front_of_a_class_is_harmless(parse):
    classes, warnings = parse("""
        enum class Kind : int { A, B };
        struct Foo {
            BE_CLASS(Foo)
        };
    """)
    assert [c.name for c in classes] == ["Foo"]
    assert warnings == []


def test_marker_naming_another_class_warns_with_both_names(parse):
    classes, warnings = parse("""
        struct Foo {
            BE_CLASS(Other)
        };
    """)
    assert classes == []
    assert kinds_and_lines(warnings) == [("class", 2)]
    assert "BE_CLASS(Other)" in warnings[0].message and "'Foo'" in warnings[0].message


def test_two_classes_in_one_file(parse):
    classes, warnings = parse("""
        struct A { BE_CLASS(A) BE_REFLECT_FIELD int a = 0; };
        struct B : A { BE_CLASS(B) BE_REFLECT_FIELD int b = 0; };
    """)
    assert [(c.name, c.parent_class, [f.name for f in c.fields], c.line) for c in classes] == [
        ("A", None, ["a"], 1),
        ("B", "A", ["b"], 2),
    ]
    assert warnings == []


# ---------------------------------------------------------------------------
# namespaces
# ---------------------------------------------------------------------------

def test_nested_namespaces_with_the_compact_form(parse):
    classes, _ = parse("""
        namespace A::B {
            namespace C {
                struct Foo { BE_CLASS(Foo) };
            }
            struct Bar { BE_CLASS(Bar) };
        }
        struct Baz { BE_CLASS(Baz) };
    """)
    by_name = {c.name: c for c in classes}
    assert by_name["Foo"].full_qualified_name == "A::B::C::Foo"
    assert by_name["Bar"].full_qualified_name == "A::B::Bar"
    assert by_name["Baz"].full_qualified_name == "Baz"
    assert by_name["Foo"].namespace == "A::B::C"


def test_becore_prefix_is_stripped_from_the_qualified_name_only(parse):
    classes, _ = parse("""
        namespace BECore {
            struct Top { BE_CLASS(Top) };
            namespace UI {
                struct Button { BE_CLASS(Button) };
            }
        }
    """)
    by_name = {c.name: c for c in classes}
    assert (by_name["Top"].qualified_name, by_name["Top"].full_qualified_name) == ("Top", "BECore::Top")
    assert (by_name["Button"].qualified_name, by_name["Button"].full_qualified_name) == (
        "UI::Button", "BECore::UI::Button")


def test_namespace_is_left_again_after_its_closing_brace(parse):
    classes, _ = parse("""
        namespace A {
            struct Inner { BE_CLASS(Inner) };
        }  // namespace A
        struct Outer { BE_CLASS(Outer) };
    """)
    assert {c.name: c.namespace for c in classes} == {"Inner": "A", "Outer": ""}


def test_anonymous_namespace_adds_no_segment(parse):
    classes, _ = parse("""
        namespace A {
            namespace {
                struct Foo { BE_CLASS(Foo) };
            }
        }
    """)
    assert only(classes).namespace == "A"


def test_inline_namespace_is_a_segment_of_the_name(parse):
    # Valid C++ (`A::v1::Foo` names the class), only longer than the name a reader would use.
    classes, _ = parse("""
        namespace A {
            inline namespace v1 {
                struct Foo { BE_CLASS(Foo) };
            }
        }
    """)
    assert only(classes).full_qualified_name == "A::v1::Foo"


def test_extern_c_block_adds_no_namespace_and_does_not_unbalance_the_rest(parse):
    classes, warnings = parse("""
        namespace A {
            extern "C" {
                struct Foo { BE_CLASS(Foo) };
            }
            struct Bar { BE_CLASS(Bar) };
        }
        struct Baz { BE_CLASS(Baz) };
    """)
    assert {c.name: c.namespace for c in classes} == {"Foo": "A", "Bar": "A", "Baz": ""}
    assert warnings == []


def test_class_in_a_function_body_is_picked_up_with_the_enclosing_namespace(parse):
    # Not meaningful (generated code could not name it) but not silent either: the generated
    # file fails to compile. Pinned as the behavior of the regex parser.
    classes, warnings = parse("""
        namespace A {
            inline void Run() {
                struct Local { BE_CLASS(Local) };
            }
        }
    """)
    assert only(classes).full_qualified_name == "A::Local"
    assert warnings == []


# ---------------------------------------------------------------------------
# options
# ---------------------------------------------------------------------------

def test_factory_base_option(parse):
    classes, _ = parse("""
        struct IFoo {
            BE_CLASS(IFoo, FACTORY_BASE)
        };
    """)
    cls = only(classes)
    assert cls.is_factory_base and not cls.register_self and cls.element_name is None


def test_register_self_and_element_options(parse):
    classes, _ = parse("""
        struct UIElement {
            BE_CLASS(UIElement, FACTORY_BASE, REGISTER_SELF, ELEMENT=item)
        };
    """)
    cls = only(classes)
    assert (cls.is_factory_base, cls.register_self, cls.element_name) == (True, True, "item")


def test_options_are_case_insensitive_and_tolerate_spaces(parse):
    classes, _ = parse("""
        struct UIElement {
            BE_CLASS( UIElement ,  factory_base ,  element = item )
        };
    """)
    cls = only(classes)
    assert (cls.is_factory_base, cls.element_name) == (True, "item")


def test_an_unknown_option_is_ignored_without_a_warning(parse):
    # Pinned: the options are matched, not validated. A typo such as FACTORY_BAS is dropped silently.
    classes, warnings = parse("""
        struct IFoo {
            BE_CLASS(IFoo, FACTORY_BAS)
        };
    """)
    assert not only(classes).is_factory_base
    assert warnings == []


# ---------------------------------------------------------------------------
# text that looks like a class or a marker but is not
# ---------------------------------------------------------------------------

def test_marker_in_a_line_comment_is_not_a_class(parse):
    classes, warnings = parse("""
        struct Foo {
            // BE_CLASS(Foo)
            int a = 0;
        };
    """)
    assert classes == []
    assert warnings == []


def test_marker_in_a_block_comment_is_not_a_class(parse):
    classes, warnings = parse("""
        /* struct Foo {
            BE_CLASS(Foo)
        }; */
        struct Bar { BE_CLASS(Bar) };
    """)
    assert [c.name for c in classes] == ["Bar"]
    assert warnings == []


def test_marker_in_a_string_literal_is_not_a_class(parse):
    classes, warnings = parse("""
        const char* kText = "struct Foo { BE_CLASS(Foo) };";
        struct Bar { BE_CLASS(Bar) };
    """)
    assert [c.name for c in classes] == ["Bar"]
    assert warnings == []


def test_marker_in_a_raw_string_is_not_a_class(parse):
    classes, warnings = parse("""
        const char* kText = R"x(
            struct Foo { BE_CLASS(Foo) };
        )x";
        struct Bar { BE_CLASS(Bar) };
    """)
    assert [c.name for c in classes] == ["Bar"]
    assert warnings == []
    assert only(classes).line == 4


def test_a_brace_in_a_string_does_not_end_the_class(parse):
    classes, _ = parse("""
        struct Foo {
            BE_CLASS(Foo)
            BE_REFLECT_FIELD const char* a = "}";
            BE_REFLECT_FIELD int b = 0;
        };
    """)
    assert [f.name for f in only(classes).fields] == ["a", "b"]


def test_block_comment_between_the_name_and_the_brace(parse):
    classes, _ = parse("""
        struct Foo /* comment { */ {
            BE_CLASS(Foo)
        };
    """)
    assert [c.name for c in classes] == ["Foo"]


def test_if_0_block_is_still_parsed(parse):
    # Documented limitation: the parser does not run a preprocessor. A reflected class in an
    # `#if 0` block (or behind a macro that is not defined) is generated like any other.
    classes, warnings = parse("""
        #if 0
        struct Dead { BE_CLASS(Dead) };
        #endif
    """)
    assert [c.name for c in classes] == ["Dead"]
    assert warnings == []


@pytest.mark.parametrize("trailer", ["", "\n"])
def test_file_without_a_final_newline(parse, trailer):
    classes, _ = parse("struct Foo { BE_CLASS(Foo) };" + trailer)
    assert [c.name for c in classes] == ["Foo"]
