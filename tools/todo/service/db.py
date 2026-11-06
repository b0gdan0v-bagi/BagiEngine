"""The connection pool and the migrations of the todo store (Postgres through psycopg 3)."""

from pathlib import Path

from psycopg import Connection
from psycopg.rows import dict_row
from psycopg_pool import ConnectionPool

SCHEMA_DIR = Path(__file__).parent / "schema"
POOL_MAX_SIZE = 8


def connect_pool(url: str) -> ConnectionPool:
    pool = ConnectionPool(url, min_size=1, max_size=POOL_MAX_SIZE, kwargs={"row_factory": dict_row}, open=True)
    pool.wait()
    return pool


def migrate(conn: Connection) -> list[str]:
    """Run every schema/NNN_*.sql that has not run yet, in order. Returns the names that ran."""
    with conn.transaction():
        conn.execute("CREATE TABLE IF NOT EXISTS schema_migrations (name text PRIMARY KEY, at timestamptz NOT NULL DEFAULT now())")
        conn.execute("SELECT pg_advisory_xact_lock(7240311)")  # two replicas starting at once run it once
        done = {row["name"] for row in conn.execute("SELECT name FROM schema_migrations").fetchall()}
        ran = []
        for path in sorted(SCHEMA_DIR.glob("*.sql")):
            if path.name in done:
                continue
            conn.execute(path.read_text(encoding="utf-8"))
            conn.execute("INSERT INTO schema_migrations (name) VALUES (%s)", (path.name,))
            ran.append(path.name)
    return ran
