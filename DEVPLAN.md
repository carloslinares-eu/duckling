# duckling: development plan

Small tasks, grouped by milestone (see PLAN.md, "Milestones"). Each task is
sized for one Claude Code session or less.

**Columns**
- **Effort**: the recommended Claude reasoning effort (`/effort` or `--effort`):
  - `low`: mechanical work.
  - `medium`: normal coding.
  - `high`: tricky logic or cross-platform work.
  - `xhigh`: protocol or concurrency logic where a mistake strands devices.
- **Agents**: how many Claude agents to run at the same time. **1** means
  the main session only. A number above 1 means independent subtasks that can
  run in parallel, for example in separate git worktrees.
- **Skills**: what the agent needs. That's domain knowledge, plus the Claude
  Code skills to run after the task (`/code-review`, `/simplify`,
  `/security-review`, `/run`).
- **HW**: whether the task needs the real devices or the Windows PC
  (✋ = the user does physical steps).

General rules:
- Every C++ task follows `CLAUDE.md` (Doxygen on everything, C++20).
- Every task that writes code ends with `/code-review medium`.
- Run `/code-review high` on hidpp, router and net tasks.
- Before writing new protocol code, read the related `hw-logs/` recording and
  reuse its exact bytes.

---

## M0: Project scaffolding
| # | Task | Effort | Agents | Skills | HW | Depends |
|---|---|---|---|---|---|---|
| 0.1 ✅ | Install the toolchain on carbon: cmake, ninja, mingw-w64, doxygen, plantuml. Render the `.puml` files and fix any syntax errors | low | 1 | Debian packaging, PlantUML | – | – |
| 0.2 ✅ | CMake skeleton: `src/main.cpp` (prints version), FetchContent for hidapi 0.14 / toml++ / doctest, `tests/` target, static-link flags | medium | 1 | CMake, FetchContent, static linking | – | 0.1 |
| 0.3 ✅ | MinGW-w64 toolchain file; cross-build the hello-world `.exe` and run it on Windows | medium | 1 | CMake toolchains, MinGW | Windows | 0.2 |
| 0.4 ✅ | GitHub Actions: build and test on Linux and Windows, Doxygen warnings as a report, upload artifacts | medium | 1 | GitHub Actions, `gh` CLI | – | 0.2 |
| 0.5 ✅ | README.md (what it does, build, install placeholder) and `.gitignore` | low | 1 | technical writing | – | 0.2 |

## M1: CLI tool (`status`, `switch`, `sniff`), with no daemon yet
| # | Task | Effort | Agents | Skills | HW | Depends |
|---|---|---|---|---|---|---|
| 1.1 | `hidpp::Report`: encode and decode short/long reports. Unit tests use lines from `hw-logs/` | medium | 1 | HID++ 2.0 framing, doctest | – | 0.2 |
| 1.2 | `hidpp::EventParser`: 0x41 connect notices and the CHANGE_HOST event. Tests use every C2/C3/C5 log line | high | 1 | HID++ notifications, Bolt receiver | – | 1.1 |
| 1.3 | `io::Receiver`: enumerate 046D:C548 0xFF00/0x0002, open, read with timeout, `request()` with swId 0xD matching, write mutex | high | 2 *(Linux and Windows backends checked in parallel)* | hidapi, hidraw, Windows HID, threading | carbon + Windows | 1.1 |
| 1.4 | `hidpp::Features`: ping, getFeatureIndex, getHostInfo, setCurrentHost, HOSTS_INFO names, device name | high | 1 | HID++ features 0x0000/0x0005/0x1814/0x1815 | carbon | 1.3 |
| 1.5 | `duckling status`: receiver, devices, own channel, host-name table | medium | 1 | CLI design | carbon + Windows | 1.4 |
| 1.6 | `duckling switch <dev> <ch\|hostname>`, checks the ack | medium | 1 | – | ✋ carbon + Windows | 1.4 |
| 1.7 | `duckling sniff` (replaces tools/hidpp-sniff.py, adds labels) | low | 1 | – | ✋ carbon | 1.2, 1.3 |
| 1.8 | M1 acceptance: repeat C1, C5 and C6 using only `duckling`. Record the results in PLAN.md | medium | 1 (+1 Windows session) | `/run`, hardware test procedure | ✋ both | 1.5–1.7 |

## M2: Daemon, networking, and keyboard → mouse following
| # | Task | Effort | Agents | Skills | HW | Depends |
|---|---|---|---|---|---|---|
| 2.1 | Vendored HMAC-SHA256, checked against the RFC 4231 test vectors | medium | 1 | cryptography basics | – | 0.2 |
| 2.2 | `net::Message` wire format: serialize/parse, HMAC, length checks. Fuzz-style tests on malformed input | high | 1 | binary protocols, defensive parsing | – | 2.1 |
| 2.3 | `net::ReplayGuard` (timestamp window, seq per host) | medium | 1 | – | – | 2.2 |
| 2.4 | `net::UdpSocket`: BSD and Winsock, broadcast, recv timeout | medium | 2 *(Linux and Windows in parallel)* | sockets, Winsock | LAN | 0.3 |
| 2.5 | `core::Config` + `StateCache` (TOML, JSON state), `duckling gen-secret` | low | 1 | toml++ | – | 0.2 |
| 2.6 | `core::EventQueue` + `Clock`, HID worker thread, Net worker thread (HELLO every 2 s) | high | 1 | C++ concurrency, condition variables | – | 1.3, 2.4 |
| 2.7 | `core::Router`, part 1: peers table, keyboard-switch → mouse follow, liveness check, loop guard. Tests with a fake clock | xhigh | 1 | state machines, PLAN decisions 7–8 | – | 2.6 |
| 2.8 | `duckling run` wires everything together. Logging | medium | 1 | – | – | 2.7, 2.5 |
| 2.9 | Security pass on the net layer | high | 1 | `/security-review` | – | 2.8 |
| 2.10 | M2 acceptance: press keyboard Easy-Switch on both computers, plus the dead-target test (C3) | medium | 1 (+1 Windows session) | `/run` | ✋ both | 2.8 |

## M3: Mouse → keyboard following (ARRIVED)
| # | Task | Effort | Agents | Skills | HW | Depends |
|---|---|---|---|---|---|---|
| 3.1 | ARRIVED send on DeviceOnline (×3, 100 ms apart), de-duplication | medium | 1 | – | – | 2.8 |
| 3.2 | `Router`, part 2: pending window, ARRIVED → keyboard follow, power-off timeout, power-cycle clear. Tests | xhigh | 1 | state machines | – | 3.1 |
| 3.3 | Measure the button-to-online time and tune the 1.5 s window and the other timing constants | medium | 1 | log analysis | ✋ both | 3.2 |
| 3.4 | M3 acceptance: full matrix (each device × each direction × power-off × target off) | high | 1 (+1 Windows session) | `/run`, test matrix | ✋ both | 3.3 |

## M4: Packaging and services
| # | Task | Effort | Agents | Skills | HW | Depends |
|---|---|---|---|---|---|---|
| 4.1 | Linux: systemd unit, udev rule, sysusers.d, `install.sh`. Test on Debian (carbon) | medium | 1 | systemd, udev | carbon | 2.8 |
| 4.2 | Same on Fedora (SELinux check) | medium | 1 | Fedora, SELinux | fedora | 4.1 |
| 4.3 | Windows: `install.ps1` (Program Files, scheduled task as SYSTEM, firewall rule for private networks only) | medium | 1 | PowerShell, Task Scheduler, Windows Firewall | Windows | 2.8 |
| 4.4 | Release workflow: tag → GitHub Release with the Linux binary and the Windows .exe | low | 1 | GitHub Actions | – | 0.4 |
| 4.5 | Docs: README install/config sections, Doxygen pass with no warnings | low | 1 | technical writing | – | 4.1, 4.3 |

## M5: Polish (optional)
| # | Task | Effort | Agents | Skills | HW | Depends |
|---|---|---|---|---|---|---|
| 5.1 | Check whether HOSTS_INFO setHostFriendlyName is safe, then write the hostname into empty slots | high | 1 | HID++ 0x1815 | ✋ carbon | M3 |
| 5.2 | A real Windows service (`service_win.cpp`) | high | 1 | Win32 service API | Windows | M4 |
| 5.3 | `/simplify` pass on the whole codebase | medium | 1 | `/simplify` | – | M3 |

---

## Where parallel agents pay off
- **1.3 and 2.4:** the Linux and Windows backends can be built and tested at
  the same time. One agent runs on carbon and one on Windows, and they hand
  over through the repo as in C4/C5.
- **M1 hidpp work and M2 net work (2.1–2.5)** don't depend on each other. Two
  agents can run them in separate worktrees once 0.2 is merged.
- Everything else is sequential, or too small to be worth splitting. A
  single agent is cheaper and keeps context.
