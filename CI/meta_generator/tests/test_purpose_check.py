"""A reflected class refuses a field of a class of a conflicting purpose: an error of the run."""

import textwrap

import pytest

VISUAL = "struct GemLook {\n    BE_CLASS(GemLook, PURPOSE=visual)\n    BE_REFLECT_FIELD int tint = 0;\n};\n"
SIM = "struct Gem {\n    BE_CLASS(Gem, PURPOSE=sim)\n    BE_REFLECT_FIELD int kind = 0;\n};\n"
COMMON = "struct Id {\n    BE_CLASS(Id)\n    BE_REFLECT_FIELD int value = 0;\n};\n"


def holder(name, purpose, field_type):
    option = f", PURPOSE={purpose}" if purpose else ""
    return textwrap.dedent(f"""\
        struct {name} {{
            BE_CLASS({name}{option})
            BE_REFLECT_FIELD {field_type} _field;
        }};
    """)


@pytest.fixture
def source(tmp_path):
    directory = tmp_path / "src"
    directory.mkdir()
    return directory


def write(source, **files):
    for name, text in files.items():
        (source / f"{name}.h").write_text(text, encoding="utf-8")


def error_lines(err):
    return [line for line in err.splitlines() if "error: reflection:" in line]


def test_a_visual_field_in_a_sim_class_is_an_error_with_the_line_of_the_class(run_cli, source):
    write(source, GemLook=VISUAL, Board=holder("Board", "sim", "GemLook"))
    code, _, err = run_cli(source)
    assert code == 1
    lines = error_lines(err)
    assert len(lines) == 1
    assert lines[0].endswith("Board.h:1: error: reflection: 'Board' (purpose sim) has a field of type 'GemLook' (purpose visual)")


@pytest.mark.parametrize("field_type", ["eastl::vector<GemLook>", "Optional<GemLook>", "ns::GemLook", "HashMap<int, GemLook>"])
def test_the_type_is_found_inside_a_template_argument(run_cli, source, field_type):
    write(source, GemLook=VISUAL, Board=holder("Board", "sim", field_type))
    code, _, err = run_cli(source)
    assert code == 1
    assert len(error_lines(err)) == 1


def test_a_common_class_with_a_part_field_is_an_error(run_cli, source):
    write(source, GemLook=VISUAL, Board=holder("Board", "", "GemLook"))
    code, _, err = run_cli(source)
    assert code == 1
    assert "'Board' (purpose common) has a field of type 'GemLook' (purpose visual)" in err


def test_the_same_purpose_is_fine(run_cli, source):
    write(source, Gem=SIM, Board=holder("Board", "sim", "Gem"))
    code, _, err = run_cli(source)
    assert code == 0
    assert error_lines(err) == []


@pytest.mark.parametrize("purpose", ["", "sim", "visual", "editor"])
def test_a_common_field_is_fine_everywhere(run_cli, source, purpose):
    write(source, Id=COMMON, Board=holder("Board", purpose, "Id"))
    code, _, err = run_cli(source)
    assert code == 0
    assert error_lines(err) == []


def test_unknown_names_and_non_class_types_are_not_checked(run_cli, source):
    write(source, Board=holder("Board", "sim", "eastl::vector<Elsewhere>"))
    code, _, err = run_cli(source)
    assert code == 0
    assert error_lines(err) == []


def test_a_name_shared_by_two_classes_is_not_checked(run_cli, source):
    write(source, A=VISUAL, B=VISUAL.replace("PURPOSE=visual", "PURPOSE=editor"), Board=holder("Board", "sim", "GemLook"))
    code, _, err = run_cli(source)
    assert code == 0
    assert error_lines(err) == []


def test_a_second_run_from_the_cache_still_fails(run_cli, source):
    write(source, GemLook=VISUAL, Board=holder("Board", "sim", "GemLook"))
    code_first, _, err_first = run_cli(source)
    code_second, out_second, err_second = run_cli(source)
    assert (code_first, code_second) == (1, 1)
    assert "0 files processed" in out_second
    assert error_lines(err_second) == error_lines(err_first)


def test_fixing_the_field_ends_the_error(run_cli, source):
    write(source, GemLook=VISUAL, Board=holder("Board", "sim", "GemLook"))
    assert run_cli(source)[0] == 1
    write(source, Board=holder("Board", "sim", "int"))
    code, _, err = run_cli(source)
    assert code == 0
    assert error_lines(err) == []
