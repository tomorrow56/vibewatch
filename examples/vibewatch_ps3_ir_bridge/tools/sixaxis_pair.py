#!/usr/bin/env python3
"""
sixaxis_pair.py - Read/write the Bluetooth "master" address stored on a
Sony DualShock 3 (Sixaxis) controller over USB.

This is a small, dependency-light, cross-platform replacement for
SixaxisPairTool (Windows-only) and sixpair.c (requires a C compiler). It
talks to the controller's HID Feature Report 0xF5 via the `hidapi`
library, using the same byte layout as bluepad32's
tools/sixaxispairer.c (a known-working hidapi-based implementation).

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

    # Add --debug to either form to print the raw bytes sent/received over
    # the HID Feature Report, useful for diagnosing partial writes:
    python3 sixaxis_pair.py AA:BB:CC:DD:EE:FF --debug

Connect the DualShock 3 to this computer with a USB cable before running
either command.
"""

import re
import sys
import time

try:
    import hid
except ImportError:
    print("hidapi bindings are required. Install them with: pip3 install hid\n"
          "(and the native hidapi library, e.g. 'brew install hidapi' on macOS)")
    sys.exit(1)

VENDOR_ID = 0x054C
PRODUCT_ID = 0x0268

# DualShock 3 Bluetooth master address is exposed as HID Feature Report
# 0xF5: byte 0 = report ID, byte 1 = 0x00, bytes 2-7 = MAC (6 bytes).
#
# This layout matches bluepad32's tools/sixaxispairer.c (a known-working
# hidapi-based implementation), which sends exactly
# {report_id, 0x00, mac[0..5]} (8 bytes total via hid_send_feature_report,
# which itself prepends the report ID as required on macOS). Note this is
# one byte shorter than the raw USB control transfer msg[8] used by the
# original sixpair.c ({0x01, 0x00, mac[0..5]}, sent without a report ID
# byte since sixpair.c talks to the device via a raw libusb control
# transfer instead of going through the OS's HID API): hidapi's macOS
# backend effectively substitutes the report ID for that leading 0x01
# byte, so including both would shift the MAC by one byte and corrupt the
# last byte -- which is exactly the corruption this once had before this
# fix.
REPORT_ID_MASTER_ADDR = 0xF5
REPORT_SIZE = 8  # report ID byte + 1 reserved byte + 6 MAC bytes

MAC_RE = re.compile(r"^([0-9a-fA-F]{2}:){5}[0-9a-fA-F]{2}$")


def open_controller():
    try:
        return hid.Device(vid=VENDOR_ID, pid=PRODUCT_ID)
    except hid.HIDException as error:
        print("No DualShock 3 controller found on USB "
              f"(or it could not be opened): {error}\n"
              "Plug it in with a USB cable and try again.")
        sys.exit(1)


def _hex(data):
    return " ".join(f"{b:02x}" for b in data)


def get_master(device, debug=False):
    data = device.get_feature_report(REPORT_ID_MASTER_ADDR, REPORT_SIZE)
    if debug:
        print(f"  [debug] raw GET_FEATURE(0xF5) reply ({len(data)} bytes): "
              f"{_hex(data)}")
    return ":".join(f"{byte:02x}" for byte in data[2:8])


def set_master(device, mac, debug=False):
    mac_bytes = [int(part, 16) for part in mac.split(":")]
    payload = bytes([REPORT_ID_MASTER_ADDR, 0x00] + mac_bytes)
    if debug:
        print(f"  [debug] raw SET_FEATURE(0xF5) payload ({len(payload)} "
              f"bytes): {_hex(payload)}")
    device.send_feature_report(payload)


def main():
    args = [a for a in sys.argv[1:] if a != "--debug"]
    debug = "--debug" in sys.argv[1:]

    if len(args) > 1:
        print(f"usage: {sys.argv[0]} [AA:BB:CC:DD:EE:FF] [--debug]")
        sys.exit(1)

    new_mac = None
    if len(args) == 1:
        new_mac = args[0]
        if not MAC_RE.match(new_mac):
            print(f"'{new_mac}' does not look like a MAC address "
                  "(expected AA:BB:CC:DD:EE:FF)")
            sys.exit(1)

    device = open_controller()
    try:
        print(f"Current Bluetooth master: {get_master(device, debug)}")
        if new_mac is not None:
            print(f"Setting master bd_addr to {new_mac.lower()}")
            set_master(device, new_mac, debug)
            # Re-open the device (instead of reusing the same handle) before
            # verifying, since some hidapi backends/devices cache feature
            # report reads on an open handle and would otherwise echo back
            # a stale value immediately after writing.
            device.close()
            time.sleep(0.3)
            device = open_controller()
            confirmed = get_master(device, debug)
            print(f"New Bluetooth master:     {confirmed}")
            if confirmed != new_mac.lower():
                print("WARNING: the controller did not report back the "
                      "MAC address you set. Unplug and replug the USB "
                      "cable, then run this script with no arguments to "
                      "double-check the stored value.")
    finally:
        device.close()


if __name__ == "__main__":
    main()
