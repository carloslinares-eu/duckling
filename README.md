# duckling

[![CI](https://github.com/carloslinares-eu/duckling/actions/workflows/ci.yml/badge.svg)](https://github.com/carloslinares-eu/duckling/actions/workflows/ci.yml)

**Your Logitech mouse and keyboard switch computers together.** Press
Easy-Switch on one, and the other follows. It works across Linux and Windows,
with Logi Bolt receivers.

Logitech's own *Flow* does something similar, but only between computers
that all run Logi Options+, and that doesn't include Linux. duckling is a
small daemon you run on every computer instead.

> **Status: early development.** The build system and CI are in place. The
> HID++ code is in progress (milestone M1 in [DEVPLAN.md](DEVPLAN.md)).
> Nothing switches devices yet.

## How it works

```
  press Easy-Switch 1 on the keyboard
        │
        ▼
  keyboard announces "leaving ch3 → ch1"       (HID++ CHANGE_HOST event)
        │
        ▼
  duckling on this computer: is the ch1 computer alive?   (LAN HELLO beacons)
        │ yes
        ▼
  send the mouse to ch1                        (HID++ CHANGE_HOST)
```

- **Keyboard switches → mouse follows.** The MX Mechanical says where it's
  going right before it disconnects.
- **Mouse switches → keyboard follows.** The MX Master 3S doesn't say where
  it's going. The duckling on the computer it reaches broadcasts an
  `ARRIVED` message, and the computer it left sends the keyboard after it.
- **A device is only ever sent to a computer that's switched on**, so you
  never end up with both devices stuck on a sleeping PC.
- **LAN messages are UDP broadcasts signed with a shared secret** (HMAC), so
  other machines on the network can't trigger switches.
- **Channels are detected automatically** from the devices themselves
  (HID++ `HOSTS_INFO`). The only thing to configure is the shared secret.

The design, the hardware findings behind it, and the architecture diagrams
are in [PLAN.md](PLAN.md) and [docs/architecture/](docs/architecture/).

## Requirements

| | |
|---|---|
| Devices | Logitech devices with HID++ 2.0 `CHANGE_HOST` (0x1814). Tested with MX Master 3S and MX Mechanical |
| Connection | A Logi Bolt receiver (046D:C548) on every computer |
| Linux | Any systemd distribution (developed on Debian 13; Fedora to be tested) |
| Windows | Windows 10/11 x64. Logi Options+ can stay installed |
| Network | All computers on the same LAN (UDP port 47474) |

## Building

Requirements: CMake ≥ 3.25, Ninja, a C++20 compiler. On Linux you also need
the libudev headers. The other dependencies (hidapi, toml++, doctest) are
downloaded at configure time.

**Linux**
```sh
sudo apt install cmake ninja-build g++ libudev-dev pkg-config   # Debian/Ubuntu
# sudo dnf install cmake ninja-build gcc-c++ systemd-devel      # Fedora
cmake --preset linux-release
cmake --build --preset linux-release
ctest --preset linux-release
./build/linux-release/duckling version
```

**Windows, cross-compiled from Linux**
```sh
sudo apt install mingw-w64
cmake --preset windows-mingw-release
cmake --build --preset windows-mingw-release
# -> build/windows-mingw-release/duckling.exe (no extra DLLs needed)
```

**Windows, native MSVC.** Run from a "Developer PowerShell for VS":
```powershell
cmake --preset windows-msvc-release
cmake --build --preset windows-msvc-release
ctest --preset windows-msvc-release
```

**API docs**: `doxygen Doxyfile` → `build/docs/html/index.html`

## Installing

*Not available yet.* This comes in milestone M4:
- Linux: a systemd service with a udev rule.
- Windows: an install script that registers a startup task and a firewall
  rule.

## Usage

```
duckling status      show the receiver, paired devices, this host's channel
                     on each device and the devices' host-name tables
duckling switch <mouse|keyboard|all> <channel|hostname>
                     send devices connected here to channel 1-3, or to the
                     channel named after a host (case-insensitive)
duckling sniff [seconds]
                     print every HID++ report from the receiver, labelled;
                     type a label + Enter to insert a marker
duckling version     print version and build information
duckling help        show usage
```

Example (Windows, both devices connected there):
```
receiver   Logi Bolt  \\?\HID#VID_046D&PID_C548&MI_02&Col01#…
this host  DESKTOP-CARLOS

slot 1  keyboard  MX Mechanical  (wpid B366, serial 284C459C)
        connected here, HID++ 4.5, this host is ch1 of 3
        ch1  DESKTOP-CARLOS   <- this host
        ch2  Logitech Bolt receiver
        ch3  Logitech Bolt receiver

slot 2  mouse     MX Master 3S  (wpid B034, serial E576CD4A)
        connected here, HID++ 4.5, this host is ch1 of 3
        ch1  DESKTOP-CARLOS   <- this host
        ch2  fedora
        ch3  Logitech Bolt receiver
```
Devices on another host show as "not connected here". The receiver still
knows them, so they're listed without waiting for a timeout.

Switching both devices from carbon back to Windows:
```
$ duckling switch all DESKTOP-CARLOS
keyboard: ch3 (Logitech Bolt receiver) -> ch1 (DESKTOP-CARLOS)
mouse: ch3 (Logitech Bolt receiver) -> ch1 (DESKTOP-CARLOS)
```
`switch` doesn't check that the target computer is on. A device sent to a
computer that is off stays offline until you press its Easy-Switch button.

Recording traffic for a bug report or a hardware check (the output is a
valid test fixture):
```
$ duckling sniff | tee hw-logs/my-check.txt
6.510 10 02 41 10 02 34 b0  <- mouse online (slot 2, wpid B034)
15.410 11 02 0a 11 00 00 00 …  <- mouse reply, swId 0x1 (duckling)
15.428 10 02 41 10 42 34 b0  <- mouse OFFLINE (slot 2, wpid B034)
```

Planned: `duckling run` (the daemon) and `gen-secret`.

## Repository layout

| Path | Contents |
|---|---|
| `src/` | C++ sources (`app`, `core`, `hidpp`, `io`, `net`) |
| `tests/` | doctest unit tests |
| `cmake/` | dependency fetching, warnings, the MinGW toolchain |
| `docs/architecture/` | PlantUML diagrams |
| `hw-logs/` | raw HID++ recordings from the hardware checks (also test fixtures) |
| `tools/` | Python test helpers and the Windows test script (not shipped) |
| `PLAN.md` / `DEVPLAN.md` | design decisions / task list |

## Contributing

Bugs and fixes are tracked as [GitHub issues](https://github.com/carloslinares-eu/duckling/issues).
Code conventions (C++20, Doxygen on every public symbol) are in
[CLAUDE.md](CLAUDE.md).

## License

duckling is free software, licensed under the **GNU General Public License
v3.0**. See [LICENSE](LICENSE).

The dependencies keep their own licenses:
- hidapi: GPL-3.0, BSD or its original license; used here under GPL-3.0.
- toml++: MIT.
- doctest: MIT, used for the tests only.
