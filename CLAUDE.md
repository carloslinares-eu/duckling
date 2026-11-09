# duckling: notes for Claude

Read `PLAN.md` first. It holds the decisions, the hardware findings (C1–C6)
and the code design. `DEVPLAN.md` has the task list. The architecture diagrams are in `docs/architecture/*.puml`
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

## Faults and GitHub issues
- When a fault turns up during a task and gets fixed, document it as a GitHub
  issue: `gh issue create` on carloslinares-eu/duckling. Include the
  symptom, the root cause and the fix. Close it from the fixing commit
  (`Fixes #N`), or close it with a comment that links the commit.
- Labels: `bug`, plus one of `build`, `hidpp`, `io`, `net`, `router`,
  `packaging`.

## Testing on Windows
- carbon reaches the Windows PC over SSH with the `desktop-carlos` alias
  (setup: `docs/dev-setup-windows-ssh.md`).
- Run `tools/win-test.sh` (unit tests) or `tools/win-test.sh <args>` to copy
  the MinGW build over and run `duckling.exe <args>` there.
- The remote shell is Windows PowerShell 5.1. It wraps stderr output from
  native programs in `NativeCommandError` blocks. That's cosmetic, not a
  failure; check the exit code.
