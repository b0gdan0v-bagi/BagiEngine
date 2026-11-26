# A second registration of a type replaces it in its factory

- Version: 0.29.3
- Entry: factory-register-duplicate
- Date: 2026-11-26
- Bump: patch

## Changed
- `AbstractFactory::Register` of a type already registered replaces its creator and meta and keeps the type once, at its first position (it pushed the meta a second time). Adapt: nothing.
