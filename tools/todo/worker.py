#!/usr/bin/env python3
"""The worker command line of the todo store: downloaded from the service and run by bagitodo_cli.py next to this file."""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import bagitodo_cli  # noqa: E402

sys.exit(bagitodo_cli.run("worker.py", __file__))
