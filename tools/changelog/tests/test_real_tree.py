"""The engine's own header, `changelog/` and `CHANGELOG.md` agree; the same check as ctest BagiChangelogCheck."""

import changelog
from conftest import ENGINE_ROOT


def test_the_real_log_is_clean():
    assert changelog.check_tree(ENGINE_ROOT) == []
