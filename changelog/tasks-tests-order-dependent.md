# Task tests independent of case order

- Version: 0.29.8
- Entry: tasks-tests-order-dependent
- Date: 2026-11-26
- Bump: patch

## Changed
- The task system tests leave no parked task behind and no longer race the worker pool, so they pass in any order (`--gtest_shuffle`) and when repeated. Test code only.
