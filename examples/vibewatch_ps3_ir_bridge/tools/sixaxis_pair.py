#!/usr/bin/env python3
"""
sixaxis_pair.py - Read/write the Bluetooth "master" address stored on a
Sony DualShock 3 (Sixaxis) controller over USB.

This is a small, dependency-light, cross-platform replacement for
SixaxisPairTool (Windows-only) and sixpair.c (requires a C compiler). It
talks to the controller using the same HID Feature Report (0xF5) that the
original sixpair.c uses, via the `hidapi` library.

This uses `hidapi` (not raw libusb) because on macOS the DualShock 3's USB
interface is already claimed by the built-in IOHIDFamily driver, so
libusb-based tools (including PyUSB) fail to claim it with a "Access
denied" error even when run as root. hidapi talks to HID devices through
the OS's HID stack (IOHIDManager on macOS) instead of claiming the raw USB
interface, which avoids that conflict.

Setup (no compiler required):
    brew install hidapi         # macOS; on Linux install libhidapi via your package manager
    pip3 install hid            # or use a venv, see the example README

Usage:
    # Show the MAC address the controller currently connects to:
    python3 sixaxis_pair.py

    # Re-pair the controller to a new host (e.g. this ESP32's MAC address,
    # as printed by vibewatch_ps3_ir_bridge.ino on the Serial monitor):
    python3 sixaxis_pair.py AA:BB:CC:DD:EE:FF

Connect the DualShock 3 to this computer with a USB cable before running
either command.
"""

import re
import sys

try:
    import hid
except ImportError:
    print("hidapi bindings are required. Install them with: pip3 install hid\n"
          "(and the native hidapi library, e.g. 'brew install hidapi' on macOS)")
    sys.exit(1)

VENDOR_ID = 0x054C
PRODUCT_ID = 0x0268

# DualShock 3 Bluetooth master address is exposed as HID Feature Report
# 0xF5: byte 0 = report ID, byte 1 = 0x01, byte 2 = 0x00, bytes 3-8 = MAC.
REPORT_ID_MASTER_ADDR = 0xF5
REPORT_SIZE = 9  # report ID byte + 8 payload bytes

MAC_RE = re.compile(r"^([0-9a-fA-F]{2}:){5}[0-9a-fA-F]{2}$")


def open_controller():
    try:
        return hid.Device(vid=VENDOR_ID, pid=PRODUCT_ID)
    except hid.HIDException as error:
        print("No DualShock 3 controller found on USB "
              f"(or it could not be opened): {error}\n"
              "Plug it in with a USB cable and try again.")
        sys.exit(1)


def get_master(device):
    data = device.get_feature_report(REPORT_ID_MASTER_ADDR, REPORT_SIZE)
    return ":".join(f"{byte:02x}" for byte in data[3:9])


def set_master(device, mac):
    mac_bytes = [int(part, 16) for part in mac.split(":")]
    payload = bytes([REPORT_ID_MASTER_ADDR, 0x01, 0x00] + mac_bytes)
    device.send_feature_report(payload)


def main():
    if len(sys.argv) > 2:
        print(f"usage: {sys.argv[0]} [AA:BB:CC:DD:EE:FF]")
        sys.exit(1)

    new_mac = None
    if len(sys.argv) == 2:
        new_mac = sys.argv[1]
        if not MAC_RE.match(new_mac):
            print(f"'{new_mac}' does not look like a MAC address "
                  "(expected AA:BB:CC:DD:EE:FF)")
            sys.exit(1)

    device = open_controller()
    try:
        print(f"Current Bluetooth master: {get_master(device)}")
        if new_mac is not None:
            print(f"Setting master bd_addr to {new_mac.lower()}")
            set_master(device, new_mac)
            print(f"New Bluetooth master:     {get_master(device)}")
    finally:
        device.close()


if __name__ == "__main__":
    main()
