"""export.py serve: a web package on this machine, from its line to its cancel."""

import http.client
import os
import queue
import signal
import socket
import subprocess
import sys
import threading
from pathlib import Path
from urllib.parse import urlsplit

import pytest

from export_support import TARGET, write

import export

SCRIPT = Path(export.__file__)
LIMIT_SECONDS = 20.0
LINE = "== serve: "
ISOLATION = ("cross-origin-opener-policy", "cross-origin-embedder-policy")


def web_package(folder: Path, threads: bool = True) -> Path:
    write(folder / "bagi-package.txt", "index.html\n")
    write(folder / "index.html", "the page")
    write(folder / "manifest.json", f'{{"threads": {"true" if threads else "false"}}}')
    return folder


def get(url: str) -> tuple[int, dict, bytes]:
    address = urlsplit(url)
    connection = http.client.HTTPConnection(address.hostname, address.port, timeout=LIMIT_SECONDS)
    try:
        connection.request("GET", address.path)
        answer = connection.getresponse()
        return answer.status, {name.lower(): value for name, value in answer.getheaders()}, answer.read()
    finally:
        connection.close()


@pytest.mark.parametrize("threads", [True, False])
def test_the_page_is_served_with_the_headers_of_its_kind_until_the_cancel(tmp_path, capsys, monkeypatch, threads):
    # What a deploy sets in the environment is not for a page served to oneself
    monkeypatch.setenv("BASIC_AUTH_USER", "someone")
    monkeypatch.setenv("BASIC_AUTH_PASSWORD", "something")
    seen = []

    def opener(url):
        seen.append((url, get(url)))
        signal.raise_signal(signal.SIGTERM)
        return True

    code = export.main(["serve", "--dir", str(web_package(tmp_path / "page", threads)), "--open"], opener=opener)

    (url, (status, headers, body)), = seen
    assert code == 3
    assert capsys.readouterr().out.splitlines() == [LINE + url, "== export cancelled"]
    assert url.startswith("http://127.0.0.1:") and urlsplit(url).port != 0
    assert (status, body) == (200, b"the page")
    assert all((name in headers) == threads for name in ISOLATION)
    with pytest.raises(OSError):
        get(url)


@pytest.mark.parametrize("answer, said", [(False, "no browser opened"), (OSError("no display"), "cannot open a browser (no display)")])
def test_a_browser_that_does_not_open_is_a_line_and_the_page_stays_served(tmp_path, capsys, answer, said):
    def opener(url):
        signal.raise_signal(signal.SIGTERM)
        if isinstance(answer, Exception):
            raise answer
        return answer

    assert export.main(["serve", "--dir", str(web_package(tmp_path)), "--open"], opener=opener) == 3

    output = capsys.readouterr().out.splitlines()
    assert output[1].startswith(said) and output[1].endswith("yourself")
    assert output[-1] == "== export cancelled"


def lines_of_a_child(script: subprocess.Popen) -> queue.Queue:
    """The script's output lines as they come, so that waiting for one has a time limit."""
    lines = queue.Queue()
    threading.Thread(target=lambda: [lines.put(line.rstrip("\n")) for line in script.stdout], daemon=True).start()
    return lines


def test_the_server_ends_with_its_input_and_frees_the_port(tmp_path):
    package = web_package(tmp_path / "page")
    script = subprocess.Popen([sys.executable, str(SCRIPT), "serve", "--dir", str(package), "--cancel-on-stdin-close"],
                              stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True)
    try:
        lines = lines_of_a_child(script)
        first = lines.get(timeout=LIMIT_SECONDS)
        url = first.removeprefix(LINE)
        assert first.startswith(LINE) and get(url)[0] == 200

        script.stdin.close()

        assert script.wait(timeout=LIMIT_SECONDS) == 3
        assert lines.get(timeout=LIMIT_SECONDS) == "== export cancelled"
        with socket.socket() as taken:
            taken.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)  # the closed connection of the request may linger
            taken.bind(("127.0.0.1", urlsplit(url).port))
    finally:
        script.kill()
        script.wait()


def desktop_package(folder: Path) -> Path:
    write(folder / TARGET)
    write(folder / "bagi-package.txt", TARGET + "\n")
    return folder


def lost_page(folder: Path) -> Path:
    (web_package(folder) / "index.html").unlink()
    return folder


@pytest.mark.parametrize("make, reason", [
    (lambda folder: write(folder / "index.html").parent, "is no exported package"),
    (desktop_package, "is no web package: a desktop one starts with `export.py run --dir`"),
    (lost_page, "is no web package"),
])
def test_only_a_web_package_is_served(tmp_path, capsys, make, reason):
    assert export.main(["serve", "--dir", str(make(tmp_path / "page"))]) == 1

    output = capsys.readouterr().out.splitlines()
    assert len(output) == 1 and output[0].startswith("== export failed: ") and reason in output[0]


@pytest.mark.skipif(os.name == "nt", reason="Windows lets the server's SO_REUSEADDR take a port that is listened on")
def test_a_port_that_is_taken_is_named(tmp_path, capsys):
    with socket.socket() as other:
        other.bind(("127.0.0.1", 0))
        other.listen()
        port = other.getsockname()[1]

        assert export.main(["serve", "--dir", str(web_package(tmp_path)), "--port", str(port)]) == 1

    assert capsys.readouterr().out.splitlines()[-1].startswith(f"== export failed: cannot serve on port {port}: ")
