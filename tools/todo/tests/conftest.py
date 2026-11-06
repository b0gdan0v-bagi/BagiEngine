"""
Fixtures of the todo tests.

The service needs a real Postgres: `TODO_TEST_DATABASE_URL` names one (it is wiped), otherwise a
`postgres:17-alpine` container is started for the session through docker. Without either, the
tests that need a database are skipped; the pure ones (markdown, CLI guards) still run.
"""

import os
import shutil
import subprocess
import sys
import time
import uuid
from pathlib import Path

import pytest

TOOL_DIR = Path(__file__).resolve().parents[1]
FIXTURES = Path(__file__).parent / "fixtures"
sys.path.insert(0, str(TOOL_DIR))
sys.path.insert(0, str(Path(__file__).parent))

TOKEN = "test-token"
WEBHOOK_SECRET = "test-secret"
POSTGRES_IMAGE = "postgres:17-alpine"


def _start_container() -> tuple[str | None, str | None]:
    if shutil.which("docker") is None:
        return None, None
    name = f"todo-test-{uuid.uuid4().hex[:8]}"
    started = subprocess.run(
        ["docker", "run", "-d", "--rm", "--name", name, "-e", "POSTGRES_PASSWORD=test", "-p", "127.0.0.1::5432", POSTGRES_IMAGE],
        capture_output=True,
        text=True,
    )
    if started.returncode != 0:
        return None, None
    port = subprocess.run(["docker", "port", name, "5432/tcp"], capture_output=True, text=True).stdout.strip().rsplit(":", 1)[-1]
    url = f"postgresql://postgres:test@127.0.0.1:{port}/postgres"
    import psycopg

    for _ in range(120):
        try:
            with psycopg.connect(url, connect_timeout=2) as conn:
                conn.execute("SELECT 1")
            return url, name
        except psycopg.OperationalError:
            time.sleep(0.5)
    subprocess.run(["docker", "rm", "-f", name], capture_output=True)
    return None, None


@pytest.fixture(scope="session")
def database_url():
    url = os.environ.get("TODO_TEST_DATABASE_URL")
    name = None
    if not url:
        try:
            import psycopg  # noqa: F401
        except ImportError:
            pytest.skip("psycopg is not installed")
        url, name = _start_container()
    if not url:
        pytest.skip("no Postgres: set TODO_TEST_DATABASE_URL or start docker")
    yield url
    if name:
        subprocess.run(["docker", "rm", "-f", name], capture_output=True)


@pytest.fixture
def clean_database(database_url):
    import psycopg

    with psycopg.connect(database_url, autocommit=True) as conn:
        conn.execute("DROP SCHEMA public CASCADE")
        conn.execute("CREATE SCHEMA public")
    return database_url


@pytest.fixture
def fake_vikunja():
    from fake_vikunja import FakeVikunja

    fake = FakeVikunja()
    fake.start()
    yield fake
    fake.stop()


def make_client(database_url: str, vikunja=None, public_url: str = ""):
    from fastapi.testclient import TestClient

    from service.app import Settings, create_app

    settings = Settings(
        database_url=database_url,
        api_token=TOKEN,
        vikunja_url=vikunja.url if vikunja else "",
        vikunja_token=vikunja.token if vikunja else "",
        webhook_secret=WEBHOOK_SECRET,
        public_url=public_url,
        reconcile_seconds=0,
    )
    client = TestClient(create_app(settings, start_mirror=False))
    client.headers.update({"Authorization": f"Bearer {TOKEN}", "X-Todo-Actor": "test"})
    return client


@pytest.fixture
def api(clean_database):
    """The service with no mirror, its lifespan running."""
    with make_client(clean_database) as client:
        client.put("/api/v1/areas/engine", json={"title": "Engine", "stages": [{"number": 1, "title": "Core"}]}).raise_for_status()
        yield client


@pytest.fixture
def mirrored(clean_database, fake_vikunja):
    """The service with the mirror on the fake Vikunja (no worker thread: tests call the mirror)."""
    with make_client(clean_database, fake_vikunja) as client:
        client.put(
            "/api/v1/areas/engine", json={"title": "Engine", "vikunjaProject": "BagiEngine", "stages": [{"number": 1, "title": "Core"}]}
        ).raise_for_status()
        client.put("/api/v1/areas/chess", json={"title": "Chess", "vikunjaProject": "BagiEngine-Chess"}).raise_for_status()
        yield client, client.app.state.todo["mirror"], fake_vikunja
