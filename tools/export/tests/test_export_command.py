"""export.py: the commands of an export, the lines and exit codes the editor reads, cancel and run."""

import os
import signal
import subprocess
import sys
import threading
import time
from pathlib import Path

import pytest

from export_support import TARGET, FakeGame, write

import export

SCRIPT = Path(export.__file__)
LIMIT_SECONDS = 20.0
posix_only = pytest.mark.skipif(os.name == "nt", reason="the fake programs are scripts with a shebang")


def wait_for(condition, what: str):
    deadline = time.monotonic() + LIMIT_SECONDS
    while not (result := condition()):
        assert time.monotonic() < deadline, f"timed out waiting for {what}"
        time.sleep(0.02)
    return result


def lines_of(capsys) -> list[str]:
    return capsys.readouterr().out.splitlines()


class FakeRunner:
    """Takes the commands of the steps and runs nothing; `codes` gives the exit code of the commands in order (then 0)."""

    def __init__(self, *codes: int):
        self.codes = list(codes)
        self.calls = []

    def __call__(self, command, cwd, env, cancel):
        self.calls.append((command, cwd))
        return self.codes.pop(0) if self.codes else 0


def build_arguments(game: FakeGame, *more: str, config: str = "debug") -> list[str]:
    return ["build", "--project", str(game.project), "--target", TARGET, "--config", config, *more]


# --- commands ----------------------------------------------------------------------------------------------


@pytest.mark.parametrize("system, config, preset", [
    ("Darwin", "debug", "macos-debug"),
    ("Darwin", "release", "macos-release"),
    ("Windows", "release", "windows-clang-release"),
])
def test_a_dry_run_names_the_commands_of_the_host_and_writes_nothing(tmp_path, capsys, system, config, preset):
    project = tmp_path / "project"
    project.mkdir()

    code = export.main(["build", "--project", str(project), "--target", TARGET, "--config", config, "--dry-run"], system=system)

    output = lines_of(capsys)
    commands = [line.removeprefix("$ ") for line in output if line.startswith("$ ")]
    build_dir = str(project.resolve() / "build" / "export" / preset)
    assert code == 0
    assert len(commands) == 2
    assert f"cmake --preset {preset} -B {build_dir} -DENGINE_BUILD_TESTS=OFF" in commands[0]
    assert f"cmake --build {build_dir} --target {TARGET}" in commands[1]
    assert all(("vs_env.py" in command) == (system == "Windows") for command in commands)
    assert output[-1] == f"== export ok: {project.resolve() / 'dist' / f'{TARGET}-{config}'}"
    assert list(project.iterdir()) == []


def test_another_host_needs_a_preset(tmp_path, capsys):
    game = FakeGame(tmp_path)

    assert export.main(build_arguments(game, "--dry-run"), system="Linux") == 1
    assert lines_of(capsys)[-1].startswith("== export failed: no export preset for Linux")

    assert export.main(build_arguments(game, "--dry-run", "--preset", "mine"), system="Linux") == 0
    assert any("--preset mine " in line for line in lines_of(capsys))


def test_a_build_runs_its_steps_in_the_project_and_ends_with_the_package(tmp_path, capsys):
    game = FakeGame(tmp_path, preset="macos-debug")
    runner = FakeRunner()

    code = export.main(build_arguments(game), runner=runner, system="Darwin")

    out = game.project.resolve() / "dist" / f"{TARGET}-debug"
    output = lines_of(capsys)
    assert code == 0
    assert [line for line in output if line.startswith("== ")] == [
        "== step 1/3: configure", "== step 2/3: build", "== step 3/3: package", f"== export ok: {out}"]
    assert output[-1] == f"== export ok: {out}"
    assert [command[:2] for command, _ in runner.calls] == [["cmake", "--preset"], ["cmake", "--build"]]
    assert {cwd for _, cwd in runner.calls} == {game.project.resolve()}
    assert (out / "bagi-package.txt").is_file()


@pytest.mark.parametrize("codes, step, runs", [((1,), "configure", 1), ((0, 2), "build", 2)])
def test_a_failed_step_ends_the_export_and_is_named(tmp_path, capsys, codes, step, runs):
    game = FakeGame(tmp_path, preset="macos-debug")
    runner = FakeRunner(*codes)

    code = export.main(build_arguments(game), runner=runner, system="Darwin")

    assert code == 1
    assert lines_of(capsys)[-1] == f"== export failed: {step} failed with exit code {codes[-1]}"
    assert len(runner.calls) == runs
    assert not (game.project / "dist").exists()


def test_a_missing_cmake_is_named(tmp_path, capsys, monkeypatch):
    game = FakeGame(tmp_path)
    monkeypatch.setenv("PATH", str(tmp_path))

    assert export.main(build_arguments(game), system="Darwin") == 1

    assert lines_of(capsys)[-1] == "== export failed: cmake is not on PATH"


@pytest.mark.parametrize("arguments", [
    ["build", "--project", ".", "--config", "debug"],
    ["build", "--exe", "game", "--target", TARGET, "--config", "debug"],
    ["build", "--exe", "game", "--project", ".", "--config", "debug"],
    ["build", "--exe", "game"],
    ["run"],
])
def test_a_wrong_command_line_exits_with_usage(arguments):
    with pytest.raises(SystemExit) as stopped:
        export.main(arguments)

    assert stopped.value.code == 2


# --- --exe -------------------------------------------------------------------------------------------------


def built_executable(tmp_path: Path) -> tuple[Path, Path]:
    project = tmp_path / "project"
    build = tmp_path / "elsewhere" / "build"
    write(project / "CMakePresets.json")
    write(build / "CMakeCache.txt", f"# cache\nCMAKE_GENERATOR:INTERNAL=Ninja\nCMAKE_HOME_DIRECTORY:INTERNAL={project.as_posix()}\n")
    return project, write(build / "bin" / TARGET)


def test_an_executable_names_its_project_and_target_through_the_cache(tmp_path, capsys):
    project, executable = built_executable(tmp_path)

    assert export.main(["build", "--exe", str(executable), "--config", "release", "--dry-run"], system="Darwin") == 0

    output = lines_of(capsys)
    assert any(line.endswith(f"--target {TARGET}") for line in output)
    assert output[-1] == f"== export ok: {project.resolve() / 'dist' / f'{TARGET}-release'}"


@pytest.mark.parametrize("break_it, reason", [
    (lambda executable: (executable.parents[1] / "CMakeCache.txt").unlink(), "no CMakeCache.txt above"),
    (lambda executable: write(executable.parent / "bagi-package.txt"), "exported package"),
    (lambda executable: executable.unlink(), "no executable"),
])
def test_an_executable_that_is_not_of_a_build_cannot_export(tmp_path, capsys, break_it, reason):
    _, executable = built_executable(tmp_path)
    break_it(executable)

    assert export.main(["build", "--exe", str(executable), "--config", "debug", "--dry-run"], system="Darwin") == 1

    last = lines_of(capsys)[-1]
    assert last.startswith("== export failed: ") and reason in last


# --- cancel ------------------------------------------------------------------------------------------------


# A program that starts another and waits: a build tool and its compiler. `own_group` is how ninja starts a compiler
PARENT = """
import os, signal, subprocess, sys, time
if "{ignores}" == "True":
    signal.signal(signal.SIGTERM, signal.SIG_IGN)
child = subprocess.Popen([sys.executable, "-c", "import time; time.sleep(60)"], start_new_session={own_group})
with open({pids!r} + ".tmp", "w") as stream:
    stream.write(f"{{os.getpid()}} {{child.pid}}")
os.replace({pids!r} + ".tmp", {pids!r})
time.sleep(60)
"""


def parent_source(pids: Path, *, own_group=False, ignores=False) -> str:
    return PARENT.format(pids=str(pids), own_group=own_group, ignores=ignores)


def alive(pid: int) -> bool:
    if os.name == "nt":
        listing = subprocess.run(["tasklist", "/NH", "/FI", f"PID eq {pid}"], capture_output=True, text=True)
        return str(pid) in listing.stdout.split()
    state = subprocess.run(["ps", "-o", "state=", "-p", str(pid)], capture_output=True, text=True).stdout.strip()
    return bool(state) and not state.startswith("Z")


def read_pids(path: Path) -> list[int]:
    wait_for(path.is_file, "the fake processes to start")
    return [int(pid) for pid in path.read_text().split()]


@pytest.mark.parametrize("own_group, ignores", [(False, False), (True, True)],
                         ids=["a tree that obeys", "a grandchild in its own group under a parent that ignores the signal"])
def test_a_cancel_ends_the_child_and_what_it_started(tmp_path, monkeypatch, own_group, ignores):
    monkeypatch.setattr(export, "KILL_GRACE_SECONDS", 0.5)
    pids = tmp_path / "pids"
    cancel = export.Cancel()

    def cancel_once_both_run():
        read_pids(pids)
        cancel.request()

    canceller = threading.Thread(target=cancel_once_both_run)
    canceller.start()
    command = [sys.executable, "-c", parent_source(pids, own_group=own_group, ignores=ignores)]

    with pytest.raises(export.Cancelled):
        export.run_child(command, tmp_path, dict(os.environ), cancel)

    canceller.join()
    for pid in read_pids(pids):
        wait_for(lambda: not alive(pid), f"process {pid} to be gone")


def script_with_fake_cmake(tmp_path: Path, cmake_source: str, *more: str, **popen) -> tuple[subprocess.Popen, FakeGame]:
    """The real script over a fake game, with a `cmake` on PATH that runs `cmake_source`."""
    game = FakeGame(tmp_path)
    tools = tmp_path / "tools"
    cmake = write(tools / "cmake", f"#!{sys.executable}\n{cmake_source}")
    cmake.chmod(0o755)
    env = dict(os.environ, PATH=os.pathsep.join([str(tools), os.environ["PATH"]]))
    command = [sys.executable, str(SCRIPT), *build_arguments(game, "--preset", "preset", *more)]
    return subprocess.Popen(command, env=env, stdout=subprocess.PIPE, text=True, **popen), game


@posix_only
@pytest.mark.parametrize("how", ["line", "end of input", "terminate", "interrupt"])
def test_the_script_gives_up_when_told_to(tmp_path, how):
    pids = tmp_path / "pids"
    flag = ["--cancel-on-stdin-close"] if how in ("line", "end of input") else []
    script, game = script_with_fake_cmake(tmp_path, parent_source(pids), *flag, stdin=subprocess.PIPE)
    try:
        started = read_pids(pids)
        if how == "line":
            script.stdin.write("cancel\n")
            script.stdin.flush()
        elif how == "end of input":
            script.stdin.close()
        else:
            script.send_signal(signal.SIGTERM if how == "terminate" else signal.SIGINT)

        script.wait(timeout=LIMIT_SECONDS)

        assert script.returncode == 3
        assert script.stdout.read().splitlines()[-1] == "== export cancelled"
        for pid in started:
            wait_for(lambda: not alive(pid), f"process {pid} to be gone")
        assert not (game.project / "dist").exists()
    finally:
        script.kill()
        script.wait()


@posix_only
def test_a_run_by_hand_with_no_input_is_not_cancelled(tmp_path):
    script, game = script_with_fake_cmake(tmp_path, "print('fake cmake', flush=True)", stdin=subprocess.DEVNULL)

    output, _ = script.communicate(timeout=LIMIT_SECONDS)

    lines = output.splitlines()
    assert script.returncode == 0, output
    assert lines[-1].startswith("== export ok: ")
    # What the script says before a child comes before what the child says, also through a pipe
    assert lines.index("== step 1/3: configure") < lines.index("fake cmake") < lines.index("== step 2/3: build")


# --- run ---------------------------------------------------------------------------------------------------


def test_run_refuses_a_folder_that_is_no_package(tmp_path, capsys):
    write(tmp_path / TARGET)

    assert export.main(["run", "--dir", str(tmp_path)]) == 1

    assert lines_of(capsys)[-1].startswith("== export failed: ")


@pytest.mark.parametrize("marker", ["Missing\n", "../Game\n", "\n"])
def test_run_starts_only_a_file_of_the_package(tmp_path, capsys, marker):
    write(tmp_path / TARGET)
    write(tmp_path / "package" / "bagi-package.txt", marker)

    assert export.main(["run", "--dir", str(tmp_path / "package")]) == 1

    assert "names no executable" in lines_of(capsys)[-1]


@posix_only
def test_run_starts_the_game_in_its_folder_and_does_not_wait_for_it(tmp_path):
    package = tmp_path / "package"
    report = tmp_path / "report"
    game = write(package / TARGET, f"""#!{sys.executable}
import os, sys, time
with open({str(report)!r} + ".tmp", "w") as stream:
    stream.write("\\n".join([str(os.getpid()), os.getcwd(), *sys.argv[1:]]))
os.replace({str(report)!r} + ".tmp", {str(report)!r})
print("the game talks")
time.sleep(60)
""")
    game.chmod(0o755)
    write(package / "bagi-package.txt", TARGET + "\n")

    # Returns while the game still runs, and the game's output is not the script's
    result = subprocess.run([sys.executable, str(SCRIPT), "run", "--dir", str(package)], capture_output=True, text=True, timeout=LIMIT_SECONDS)

    wait_for(report.is_file, "the game to start")
    pid, cwd, *arguments = report.read_text().splitlines()
    try:
        assert result.returncode == 0, result.stdout
        assert result.stdout.splitlines() == [f"== export ok: {package.resolve()}"]
        assert Path(cwd) == package.resolve()
        assert arguments == ["--game"]
        assert alive(int(pid))
    finally:
        os.kill(int(pid), signal.SIGKILL)
