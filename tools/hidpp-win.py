#!/usr/bin/env python3
"""Minimal HID++ tool for Windows (stand-in for hidapitester, no dependencies).

Opens the Logi Bolt receiver's HID++ long-report interface (usagePage 0xFF00,
usage 0x0002) the same way hidapi's native Windows backend does: CreateFile
with shared read/write and overlapped I/O, then WriteFile/ReadFile.

  python tools/hidpp-win.py list
  python tools/hidpp-win.py ping 1 2 3 4 5 6
  python tools/hidpp-win.py feature N 0x1814        # find feature index
  python tools/hidpp-win.py name N                  # device name (0x0005)
  python tools/hidpp-win.py send 11 01 09 1A 02     # raw long report, prints replies
  python tools/hidpp-win.py listen 30               # print every report for 30 s
"""
import ctypes
import sys
import time
from ctypes import wintypes as wt

VID, PID = 0x046D, 0xC548
LONG_PAGE, LONG_USAGE = 0xFF00, 0x0002

setupapi = ctypes.WinDLL("setupapi", use_last_error=True)
hid = ctypes.WinDLL("hid", use_last_error=True)
k32 = ctypes.WinDLL("kernel32", use_last_error=True)


class GUID(ctypes.Structure):
    _fields_ = [("d1", wt.DWORD), ("d2", wt.WORD), ("d3", wt.WORD), ("d4", ctypes.c_ubyte * 8)]


class SP_DEVICE_INTERFACE_DATA(ctypes.Structure):
    _fields_ = [("cbSize", wt.DWORD), ("guid", GUID), ("flags", wt.DWORD), ("reserved", ctypes.c_void_p)]


class HIDD_ATTRIBUTES(ctypes.Structure):
    _fields_ = [("Size", wt.ULONG), ("VendorID", wt.USHORT), ("ProductID", wt.USHORT), ("Version", wt.USHORT)]


class HIDP_CAPS(ctypes.Structure):
    _fields_ = [("Usage", wt.USHORT), ("UsagePage", wt.USHORT),
                ("InputReportByteLength", wt.USHORT), ("OutputReportByteLength", wt.USHORT),
                ("FeatureReportByteLength", wt.USHORT), ("Reserved", wt.USHORT * 17),
                ("rest", wt.USHORT * 10)]


class OVERLAPPED(ctypes.Structure):
    _fields_ = [("Internal", ctypes.c_void_p), ("InternalHigh", ctypes.c_void_p),
                ("Offset", wt.DWORD), ("OffsetHigh", wt.DWORD), ("hEvent", wt.HANDLE)]


k32.CreateFileW.restype = wt.HANDLE
k32.CreateEventW.restype = wt.HANDLE
setupapi.SetupDiGetClassDevsW.restype = wt.HANDLE
INVALID = wt.HANDLE(-1).value
GENERIC_RW = 0x80000000 | 0x40000000
SHARE_RW = 0x1 | 0x2
OPEN_EXISTING = 3
FLAG_OVERLAPPED = 0x40000000
ERROR_IO_PENDING = 997


def ts():
    return time.strftime("%H:%M:%S") + ".%03d" % (int(time.time() * 1000) % 1000)


def enumerate_hid():
    guid = GUID()
    hid.HidD_GetHidGuid(ctypes.byref(guid))
    devs = setupapi.SetupDiGetClassDevsW(ctypes.byref(guid), None, None, 0x12)  # PRESENT|DEVICEINTERFACE
    i = 0
    while True:
        ifd = SP_DEVICE_INTERFACE_DATA(cbSize=ctypes.sizeof(SP_DEVICE_INTERFACE_DATA))
        if not setupapi.SetupDiEnumDeviceInterfaces(wt.HANDLE(devs), None, ctypes.byref(guid), i, ctypes.byref(ifd)):
            break
        i += 1
        size = wt.DWORD()
        setupapi.SetupDiGetDeviceInterfaceDetailW(wt.HANDLE(devs), ctypes.byref(ifd), None, 0, ctypes.byref(size), None)
        buf = ctypes.create_string_buffer(size.value)
        ctypes.cast(buf, ctypes.POINTER(wt.DWORD))[0] = 8 if ctypes.sizeof(ctypes.c_void_p) == 8 else 6
        if not setupapi.SetupDiGetDeviceInterfaceDetailW(wt.HANDLE(devs), ctypes.byref(ifd), buf, size, None, None):
            continue
        path = ctypes.wstring_at(ctypes.addressof(buf) + 4)
        # Open with no access rights just to read attributes/caps (works even for kbd/mouse).
        h = k32.CreateFileW(path, 0, SHARE_RW, None, OPEN_EXISTING, 0, None)
        if h == INVALID:
            continue
        attr = HIDD_ATTRIBUTES(Size=ctypes.sizeof(HIDD_ATTRIBUTES))
        hid.HidD_GetAttributes(wt.HANDLE(h), ctypes.byref(attr))
        caps = HIDP_CAPS()
        pp = ctypes.c_void_p()
        if hid.HidD_GetPreparsedData(wt.HANDLE(h), ctypes.byref(pp)):
            hid.HidP_GetCaps(pp, ctypes.byref(caps))
            hid.HidD_FreePreparsedData(pp)
        k32.CloseHandle(wt.HANDLE(h))
        yield path, attr, caps
    setupapi.SetupDiDestroyDeviceInfoList(wt.HANDLE(devs))


class Dev:
    def __init__(self):
        for path, attr, caps in enumerate_hid():
            if (attr.VendorID, attr.ProductID, caps.UsagePage, caps.Usage) == (VID, PID, LONG_PAGE, LONG_USAGE):
                break
        else:
            sys.exit("receiver HID++ long interface not found")
        self.inlen, self.outlen = caps.InputReportByteLength, caps.OutputReportByteLength
        self.h = k32.CreateFileW(path, GENERIC_RW, SHARE_RW, None, OPEN_EXISTING, FLAG_OVERLAPPED, None)
        if self.h == INVALID:
            sys.exit("open failed, error %d" % ctypes.get_last_error())
        print("%s opened %s (in %d, out %d bytes)" % (ts(), path, self.inlen, self.outlen))
        self.ev = k32.CreateEventW(None, True, False, None)
        self.pending = None

    def _io(self, fn, buf, n, timeout_ms):
        ov = OVERLAPPED(hEvent=self.ev)
        k32.ResetEvent(wt.HANDLE(self.ev))
        got = wt.DWORD()
        if not fn(wt.HANDLE(self.h), buf, n, None, ctypes.byref(ov)):
            err = ctypes.get_last_error()
            if err != ERROR_IO_PENDING:
                raise OSError("I/O error %d" % err)
            if k32.WaitForSingleObject(wt.HANDLE(self.ev), timeout_ms) != 0:
                k32.CancelIoEx(wt.HANDLE(self.h), ctypes.byref(ov))
                k32.GetOverlappedResult(wt.HANDLE(self.h), ctypes.byref(ov), ctypes.byref(got), True)
                return None
        k32.GetOverlappedResult(wt.HANDLE(self.h), ctypes.byref(ov), ctypes.byref(got), True)
        return bytes(buf[:got.value])

    def write(self, data):
        data = bytes(data) + bytes(self.outlen - len(data))
        buf = ctypes.create_string_buffer(data, self.outlen)
        print("%s >> %s" % (ts(), data.hex(" ")))
        if self._io(k32.WriteFile, buf, self.outlen, 1000) is None:
            raise OSError("write timed out")

    def read(self, timeout_ms):
        buf = ctypes.create_string_buffer(self.inlen)
        r = self._io(k32.ReadFile, buf, self.inlen, timeout_ms)
        if r:
            print("%s << %s" % (ts(), r.hex(" ")))
        return r

    def request(self, data, timeout=1.0):
        """Send, then print every report until one matches (idx, feature, function|swid) or an error."""
        self.write(data)
        end = time.time() + timeout
        while time.time() < end:
            r = self.read(int((end - time.time()) * 1000) + 1)
            if not r:
                continue
            if r[1] == data[1] and r[2] == data[2] and r[3] == data[3]:
                return r
            if r[1] == data[1] and r[2] == 0xFF and r[3] == data[2]:  # HID++ 2.0 error
                print("   error code 0x%02x" % r[5])
                return r
            if r[0] == 0x10 and r[1] == data[1] and r[2] == 0x8F:  # HID++ 1.0 error
                print("   HID++1.0 error 0x%02x (device not connected?)" % r[5])
                return r
        print("   (no reply within %.1f s)" % timeout)
        return None


def num(s):
    return int(s, 0) if s.lower().startswith("0x") else int(s, 16) if len(sys.argv) > 1 and sys.argv[1] == "send" else int(s, 0)


def main():
    cmd, args = sys.argv[1], sys.argv[2:]
    if cmd == "list":
        for path, a, c in enumerate_hid():
            if a.VendorID == VID:
                print("%04x:%04x usagePage 0x%04x usage 0x%04x in %d out %d\n    %s" % (
                    a.VendorID, a.ProductID, c.UsagePage, c.Usage, c.InputReportByteLength, c.OutputReportByteLength, path))
        return
    d = Dev()
    if cmd == "ping":
        for n in [int(x, 0) for x in args]:
            r = d.request([0x11, n, 0x00, 0x1A, 0, 0, 0xAA])
            ok = r is not None and r[2] == 0x00 and r[6] == 0xAA
            print("   index %d: %s" % (n, "ANSWERED, protocol %d.%d" % (r[4], r[5]) if ok else "no answer"))
    elif cmd == "feature":
        n, fid = int(args[0], 0), int(args[1], 0)
        r = d.request([0x11, n, 0x00, 0x0A, fid >> 8, fid & 0xFF])
        if r and r[2] == 0x00:
            print("   feature 0x%04x at index %d, version %d" % (fid, r[4], r[6]))
    elif cmd == "name":
        n = int(args[0], 0)
        r = d.request([0x11, n, 0x00, 0x0A, 0x00, 0x05])
        if not (r and r[2] == 0 and r[4]):
            return
        fi = r[4]
        r = d.request([0x11, n, fi, 0x0A])
        length, name = r[4], b""
        while len(name) < length:
            r = d.request([0x11, n, fi, 0x1A, len(name)])
            name += r[4:4 + min(16, length - len(name))]
        print("   name: %s" % name.decode(errors="replace"))
    elif cmd == "send":
        d.request([int(x, 16) for x in args], timeout=2.0)
        end = time.time() + 2
        while time.time() < end:
            d.read(200)
    elif cmd == "listen":
        end = time.time() + float(args[0])
        while time.time() < end:
            d.read(200)


if __name__ == "__main__":
    main()
