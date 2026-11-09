# duckling: Logitech "follow me" switcher (plan, draft)

Devices: MX Master 3S + MX Mechanical. Hosts: Linux + Windows.
Linux box has a Logi Bolt receiver (046d:c548), exposed as /dev/hidraw0-2.

## Goal
Press Easy-Switch on either device and the other device follows to the same
computer, on both Linux and Windows. Like Logi Flow, without the edge crossing.

## How it works (Option A, no network)
1. The daemon finds the receiver and lists the paired devices (mouse, keyboard).
2. For each device, it looks up the HID++ CHANGE_HOST feature (0x1814) and reads
   the current channel.
3. It waits for connect/disconnect notifications from the receiver.
4. When one device disconnects, it sends CHANGE_HOST to the other device with the
   channel the first one went to.
5. Each daemon only ever pushes devices *away* from its own computer, so the two
   daemons never need to talk to each other.

## Option B (later): Flow-style edge crossing
- The cursor hitting the screen edge switches both devices.
- Needs a LAN protocol between the daemons and the global cursor position
  (hard on Wayland).
- Clipboard sharing could be added.

## The hard part: where did the device go?
- Two computers only: "the other channel" is enough. Simplest option.
- Three computers: a config file mapping channels to hosts, or a LAN message
  saying "mouse arrived on host X".
- A disconnect isn't always a switch (power-off, low battery). Options:
  - check whether the device reports which host it's on before it drops,
  - add a short delay and only act if it doesn't reconnect,
  - accept the occasional false trigger.
  Needs testing on the real hardware.

## Hardware checks (run by hand, before writing any project code)

Write each result in the "Result:" line. The whole set takes about 20 minutes.

### Setup on Linux-B (this machine)
- Solaar 1.1.14 is already installed.
- The Bolt receiver's HID++ interface is **/dev/hidraw2**. hidraw0 and hidraw1
  are the plain keyboard and mouse input interfaces. Re-check after replugging,
  because the numbers can change:
  ```
  for d in /sys/class/hidraw/hidraw*; do python3 -c "import sys;b=open(sys.argv[1]+'/device/report_descriptor','rb').read();print(sys.argv[1].split('/')[-1],'HID++' if b'\x06\x00\xff' in b else '-')" $d; done
  ```
- Raw report sniffer. It prints one HID++ report per line with a timestamp:
  ```
  sudo python3 -c "import os,time;fd=os.open('/dev/hidraw2',os.O_RDONLY)
  while True: r=os.read(fd,64);print(f'{time.monotonic():.3f}',r.hex(' '))"
  ```
  HID++ basics for reading the output:
  - Byte 0 is the report type: 0x10 is a short report (7 bytes), 0x11 a long
    one (20 bytes).
  - Byte 1 is the device index on the receiver.
  - Byte 2 is the feature index, or 0x41 for a "device connection" notice from
    the receiver.

### C1. Can software switch a device's channel through the receiver? (Linux)
1. Run `solaar show`. Note each device's name, its index on the receiver, and
   that the "Change Host" feature is listed.
2. Run `solaar config "MX Mechanical"` and look at the change-host setting:
   which values does it accept?
3. Run `solaar config "MX Mechanical" change-host 1`. The keyboard should jump to
   Windows. Press Easy-Switch 3 to bring it back.
4. Repeat with the mouse.
- Pass: both devices switch on command.
- Result:

### C2. What does the receiver report when a device leaves? (Linux)
Keep the sniffer running through all of these:
1. Press Easy-Switch 1 on the mouse. Wait 5 s, then press Easy-Switch 3.
2. Turn the mouse off with its power switch. Wait 5 s, then turn it on.
3. Do steps 1–2 with the keyboard.
4. Switch the keyboard with `solaar config ... change-host 1`, as in C1, while
   the sniffer runs.
- Questions to answer:
  - Does any report arrive **before** the 0x41 "disconnected" notice, and does
    it name the target channel?
  - Can a "switched away" disconnect be told apart from a "powered off" one?
  - How long after reconnecting does the 0x41 "connected" notice arrive? (This
    is when ARRIVED would be sent.)
- Result:

### C3. What happens when the target host is off? (Linux)
1. With the Windows PC off or asleep, run `solaar config "MX Mechanical"
   change-host 1`.
2. Does the keyboard stay on ch1 forever, return to ch3 by itself, or need a
   manual Easy-Switch press?
- This decides whether the daemon must check that a peer is alive (HELLO)
  before switching a device away, even on Linux.
- Result:

### C4. Can our program share the receiver with Logi Options+? (Windows, blocker)
1. Download hidapitester (github.com/todbot/hidapitester, a single .exe). Keep
   Options+ running.
2. List the receiver's interfaces:
   `hidapitester --vidpid 046D:C548 --list-detail`. Look for usagePage 0xFF00,
   usage 0x0002, which is the long HID++ reports.
3. Ping device index N (try 1–6) with HID++ root feature, function 1:
   ```
   hidapitester --vidpid 046D:C548 --usagePage 0xFF00 --usage 0x0002 --open --length 20 --send-output 0x11,N,0x00,0x1A,0,0,0xAA --read-input --timeout 1000
   ```
   A reply that starts `11 N 00 1A` and has `AA` in byte 6 means the device
   answered.
4. Optional: also send CHANGE_HOST. Do it on Linux first: find its feature index
   from `solaar show` (feature 0x1814, call that index F) and use function 1,
   setCurrentHost. In this command, F, N and the trailing 0x00 (= ch1) are
   placeholders:
   `--send-output 0x11,N,F,0x1A,0x00` (0x00 = ch1). On Windows, use index 0x02
   (= ch3) to send the device back to this Linux machine.
- Pass: the ping is answered while Options+ is running.
- Also note: does Options+ react, change settings, or complain?
- Result:

### C5. Does Options+ undo our switch? (Windows)
1. With Flow turned off in Options+, switch the keyboard to ch3 using the C4
   command.
2. Does it stay on ch3? Do Options+ settings still apply after it comes back to
   ch1?
- Result:

### C6. Are the channels numbered the same on both devices? (both)
1. Press Easy-Switch 1, 2 and 3 on each device and note which computer each one
   reaches.
2. Confirms decision 3: ch1 = Windows, ch2 = Linux-A, ch3 = Linux-B.
- Result:

## How it would be delivered
- Language: C++20, built with CMake. One small executable per OS, same code
  on both.
  - HID: hidapi, using the hidraw backend on Linux and the native one on
    Windows. It is fetched and built statically with CMake FetchContent, so
    there are no runtime library dependencies. hidapi 0.14 has no hotplug
    events, so the daemon re-scans for the receiver every few seconds.
  - Network: plain sockets (BSD on Linux, Winsock on Windows) behind a thin
    wrapper of about 100 lines. Asio or ZeroMQ aren't needed for UDP
    broadcast.
  - HMAC-SHA256: a small vendored single-file implementation, so OpenSSL
    isn't needed.
  - Config: a small INI/TOML file parsed by a single-header library, such as
    toml++.
  - Static linking: on Linux, link libstdc++ statically. On Windows, use the
    static runtime (MSVC /MT, or MinGW with -static), so the .exe runs on
    its own.
  - Toolchain: this machine has g++ 14.2, but CMake still needs to be
    installed (`sudo apt install cmake ninja-build`). Windows builds can be
    cross-compiled from Linux with MinGW-w64 or built in GitHub Actions.
- Linux: a single binary, a systemd unit, and a udev rule for hidraw permissions.
- Windows: a single .exe that starts at login (Task Scheduler or registry Run
  key). A real Windows service is possible but probably not needed.
- Config (optional): channel map, devices to ignore.

## Decisions
1. Hosts: Windows plus one of two Linux machines. Only two are on at a time,
   but all three channels may be used (e.g. ch1 Windows, ch2 Linux-A, ch3 Linux-B).
   Consequence: on Linux "the other channel" is always Windows, so that side is
   easy. On Windows the daemon has to pick between ch2 and ch3.
   Ways to choose (to be decided):
   a. Device tells us: check whether the device sends anything naming the
      target channel before it drops (hardware check C2).
   b. LAN presence beacon: each daemon broadcasts "alive on channel N" over UDP.
      Windows sends the follower to whichever Linux host is up. This is a small
      bit of networking, not Option B. **Current favorite.**
   c. Remember last: Windows sends the follower to the Linux channel used most
      recently. Needs no network, but it is wrong the first time after you
      change machines.
   d. Share one channel: move a single Bolt receiver between the two Linux
      machines, so both are "ch2" and the problem goes away. Only works if
      plugging the receiver back and forth is acceptable.
2. Connection: Bolt receiver on every host (Windows has its own). Both OSes use
   the same HID++ path: receiver, then device index, then CHANGE_HOST. There is
   one protocol implementation and no Bluetooth code.
3. Channels (same on mouse and keyboard; to be confirmed):
   ch1 = Windows, ch2 = Linux-A, ch3 = Linux-B (this machine, receiver 046d:c548).
   Routing rules:
   - Leaving a Linux host: always send the follower to ch1.
   - Leaving Windows: send the follower to ch2 or ch3, using the methods listed
     under decision 1.
   - Each daemon learns its own channel at startup by asking a connected device
     for its current host (CHANGE_HOST getHostInfo), so it needs no config.
4. Logi Options+ stays installed on Windows, so our daemon has to share the
   receiver with it.
   - Turn off Flow in Options+ so the two tools don't both try to switch
     devices.
   - Hardware check C4 (opening the HID++ interface while Options+ runs) is now a
     blocker. If it fails, the fallbacks are, in order:
     1. retry when the open fails,
     2. ask the user to pause Options+.
   - Watch out: Options+ may reapply device settings when a device reconnects.
     It shouldn't touch the host channel, but we need to verify that.
5. Linux desktops: modern Debian and Fedora, both on Wayland. This machine
   (Linux-B) runs Debian 13 with GNOME on Wayland.
   - Option A is not affected. It talks to the receiver through hidraw, which
     works the same under any display server.
   - Option B (edge crossing) would have to use the xdg-desktop-portal
     InputCapture interface plus libei. That is the supported way to capture
     input at the screen edge on Wayland. It is available on GNOME 45+ and
     KDE Plasma 6.1+, and Deskflow uses it. Feasible later, not trivial.
6. Telling the Windows daemon where a device went: LAN messages, sent as plain
   UDP for v1.
   - Two message types, both sent by broadcast or multicast on the LAN:
     - HELLO {host_id, channel} every 2 s. A peer is "alive" if heard from in the
       last 6 s.
     - ARRIVED {host_id, channel, device} sent the moment a device connects to a
       host.
   - Leaving Windows: when the mouse disconnects from Windows, the daemon waits
     up to about 1 s for an ARRIVED message from the Linux host the mouse went
     to, then sends the keyboard to that channel. This uses the exact
     destination with no guessing. If no ARRIVED comes, it falls back to the one
     alive Linux peer, or does nothing if there are none or several.
   - Every message carries an HMAC made with a shared secret from the config, so
     other machines on the LAN can't trigger switches. Messages are ignored if
     older than a few seconds.
   - Windows Firewall will ask once to allow the UDP port.
   - ZeroMQ: considered, but not for v1.
     - It has no discovery of its own. Discovery comes from czmq/Zyre, which are
       themselves built on UDP beacons.
     - It adds a native C++ library (libzmq), which makes "one small executable"
       harder, especially on Windows.
     - With C++, cppzmq would make the API pleasant, but libzmq still has to be
       built and linked statically on both OSes.
     - It's worth revisiting for Option B, where reliable messaging (clipboard,
       input forwarding) would actually benefit from it.

## Open questions
(none; all answered)
