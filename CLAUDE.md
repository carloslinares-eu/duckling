# duckling: notes for Claude

Read `PLAN.md` first. It holds the decisions, the hardware findings (C1–C6)
and the code design. The architecture diagrams are in `docs/architecture/*.puml`
(PlantUML). Keep them in sync when the design changes.

## C++ conventions
- C++20, CMake. Must build on Linux (GCC) and Windows (MinGW-w64 / MSVC).
- **Every public type, function, member and file gets a Doxygen comment.**
  - Use `/** ... */` blocks with `@brief`, `@param`, `@return`, `@throws`,
    and `@note` / `@warning` where relevant.
  - Use `///<` for trailing member comments.
  - Each file starts with `@file` and a one-line `@brief`.
  - Each source folder (namespace) has a `@namespace` or `@defgroup` block, so
    the generated docs mirror `classes.puml`.
- When a protocol detail comes from a hardware check, reference it in the
  comment, e.g. `@see PLAN.md C2`, and quote the byte layout.
- Namespaces match folders: `duckling::hidpp`, `duckling::io`, `duckling::net`,
  `duckling::core`.
- Generate docs with `doxygen Doxyfile`. They go to `build/docs/` and aren't
  committed.

## Repo layout
- `tools/`: throwaway Python test helpers (not shipped).
- `hw-logs/`: raw recordings from the hardware checks. They're also used as
  unit-test fixtures.
