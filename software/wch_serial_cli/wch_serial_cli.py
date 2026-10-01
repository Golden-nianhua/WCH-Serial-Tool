#!/usr/bin/env python3
import argparse
import ctypes
from ctypes import wintypes
import struct
import sys
import time
import zlib

INTERFACE_GUID = "{A1D4935B-4D76-4E6F-B623-6C856A67A8E9}"
VID = 0x1A86
APP_PID = 0x572D
APP_EP_OUT = 0x07
APP_EP_IN = 0x87

FILE_ATTRIBUTE_NORMAL = 0x00000080
FILE_FLAG_OVERLAPPED = 0x40000000

PACKET_SIZE = 64
PAYLOAD_SIZE = 48
MAGIC = b"UW"
PROTOCOL_VERSION = 1

CMD_GET_INFO = 0x01
CMD_GET_STATUS = 0x02
CMD_GET_PAIR = 0x03
CMD_START_PAIR = 0x04
CMD_CLEAR_PAIR = 0x05
CMD_GET_RF_CONFIG = 0x06
CMD_SET_RF_CONFIG = 0x07
CMD_START_SCAN = 0x08
CMD_GET_SCAN = 0x09
CMD_PAIR_DEVICE = 0x0A
CMD_GET_RSSI = 0x0B
CMD_RESET = 0x11
CMD_ENTER_ISP = 0x12
CMD_REMOTE_GET_INFO = 0x21
CMD_REMOTE_GET_STATUS = 0x22
CMD_REMOTE_GET_CONFIG = 0x23
CMD_REMOTE_SET_CONFIG = 0x24
CMD_REMOTE_RESET = 0x25
CMD_REMOTE_GET_RSSI = 0x26

REMOTE_COMMANDS = {
    CMD_GET_INFO: CMD_REMOTE_GET_INFO,
    CMD_GET_STATUS: CMD_REMOTE_GET_STATUS,
    CMD_GET_RF_CONFIG: CMD_REMOTE_GET_CONFIG,
    CMD_SET_RF_CONFIG: CMD_REMOTE_SET_CONFIG,
    CMD_GET_RSSI: CMD_REMOTE_GET_RSSI,
    CMD_RESET: CMD_REMOTE_RESET,
}

STATUS_PENDING = 8
STATUS_TIMEOUT = 9

STATUS_NAMES = {
    0: "ok", 1: "bad packet", 2: "bad command", 3: "bad length",
    4: "bad address", 5: "flash error", 6: "CRC error", 7: "bad state",
    STATUS_PENDING: "pending", STATUS_TIMEOUT: "timeout",
}
RF_ROLE_NAMES = {0: "off", 1: "USB receiver", 2: "UART node"}


class GUID(ctypes.Structure):
    _fields_ = [("Data1", wintypes.DWORD), ("Data2", wintypes.WORD),
                ("Data3", wintypes.WORD), ("Data4", ctypes.c_ubyte * 8)]


class SP_DEVICE_INTERFACE_DATA(ctypes.Structure):
    _fields_ = [("cbSize", wintypes.DWORD), ("InterfaceClassGuid", GUID),
                ("Flags", wintypes.DWORD), ("Reserved", ctypes.c_void_p)]


def parse_guid(text):
    import uuid
    value = uuid.UUID(text.strip("{}"))
    raw = value.bytes_le
    result = GUID()
    result.Data1, result.Data2, result.Data3 = struct.unpack_from("<IHH", raw)
    result.Data4[:] = raw[8:]
    return result


if sys.platform == "win32":
    setupapi = ctypes.WinDLL("setupapi", use_last_error=True)
    kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
    winusb = ctypes.WinDLL("winusb", use_last_error=True)

    setupapi.SetupDiGetClassDevsW.restype = ctypes.c_void_p
    setupapi.SetupDiGetClassDevsW.argtypes = [ctypes.POINTER(GUID), wintypes.LPCWSTR,
                                               wintypes.HWND, wintypes.DWORD]
    setupapi.SetupDiEnumDeviceInterfaces.argtypes = [ctypes.c_void_p, ctypes.c_void_p,
                                                       ctypes.POINTER(GUID), wintypes.DWORD,
                                                       ctypes.POINTER(SP_DEVICE_INTERFACE_DATA)]
    setupapi.SetupDiGetDeviceInterfaceDetailW.argtypes = [ctypes.c_void_p,
                                                            ctypes.POINTER(SP_DEVICE_INTERFACE_DATA),
                                                            ctypes.c_void_p, wintypes.DWORD,
                                                            ctypes.POINTER(wintypes.DWORD), ctypes.c_void_p]
    setupapi.SetupDiDestroyDeviceInfoList.argtypes = [ctypes.c_void_p]
    kernel32.CreateFileW.restype = wintypes.HANDLE
    kernel32.CreateFileW.argtypes = [wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD,
                                      ctypes.c_void_p, wintypes.DWORD, wintypes.DWORD,
                                      wintypes.HANDLE]
    kernel32.CloseHandle.argtypes = [wintypes.HANDLE]
    winusb.WinUsb_Initialize.argtypes = [wintypes.HANDLE, ctypes.POINTER(ctypes.c_void_p)]
    winusb.WinUsb_Free.argtypes = [ctypes.c_void_p]
    winusb.WinUsb_WritePipe.argtypes = [ctypes.c_void_p, ctypes.c_ubyte, ctypes.c_void_p,
                                         wintypes.ULONG, ctypes.POINTER(wintypes.ULONG), ctypes.c_void_p]
    winusb.WinUsb_ReadPipe.argtypes = [ctypes.c_void_p, ctypes.c_ubyte, ctypes.c_void_p,
                                        wintypes.ULONG, ctypes.POINTER(wintypes.ULONG), ctypes.c_void_p]
    winusb.WinUsb_SetPipePolicy.argtypes = [ctypes.c_void_p, ctypes.c_ubyte, wintypes.ULONG,
                                             wintypes.ULONG, ctypes.c_void_p]


def win_error(message):
    raise OSError(ctypes.get_last_error(), message)


def enumerate_paths():
    if sys.platform != "win32":
        raise RuntimeError("This tool currently requires Windows")
    guid = parse_guid(INTERFACE_GUID)
    handle = setupapi.SetupDiGetClassDevsW(ctypes.byref(guid), None, None, 0x12)
    if handle == ctypes.c_void_p(-1).value:
        win_error("SetupDiGetClassDevsW")
    paths = []
    try:
        index = 0
        while True:
            data = SP_DEVICE_INTERFACE_DATA()
            data.cbSize = ctypes.sizeof(data)
            if not setupapi.SetupDiEnumDeviceInterfaces(handle, None, ctypes.byref(guid),
                                                         index, ctypes.byref(data)):
                if ctypes.get_last_error() == 259:
                    break
                win_error("SetupDiEnumDeviceInterfaces")
            required = wintypes.DWORD()
            setupapi.SetupDiGetDeviceInterfaceDetailW(handle, ctypes.byref(data), None, 0,
                                                       ctypes.byref(required), None)
            buffer = ctypes.create_string_buffer(required.value)
            ctypes.cast(buffer, ctypes.POINTER(wintypes.DWORD))[0] = 8 if ctypes.sizeof(ctypes.c_void_p) == 8 else 6
            if not setupapi.SetupDiGetDeviceInterfaceDetailW(handle, ctypes.byref(data), buffer,
                                                              required, None, None):
                win_error("SetupDiGetDeviceInterfaceDetailW")
            paths.append(ctypes.wstring_at(ctypes.addressof(buffer) + 4))
            index += 1
    finally:
        setupapi.SetupDiDestroyDeviceInfoList(handle)
    return paths


class Device:
    def __init__(self, path):
        self.path = path
        self.ep_out = APP_EP_OUT
        self.ep_in = APP_EP_IN
        self.file = kernel32.CreateFileW(
            path, 0xC0000000, 3, None, 3,
            FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED, None
        )
        if self.file == wintypes.HANDLE(-1).value:
            win_error("CreateFileW")
        self.usb = ctypes.c_void_p()
        if not winusb.WinUsb_Initialize(self.file, ctypes.byref(self.usb)):
            kernel32.CloseHandle(self.file)
            win_error("WinUsb_Initialize")
        timeout = wintypes.ULONG(5000)
        for pipe in (self.ep_out, self.ep_in):
            winusb.WinUsb_SetPipePolicy(self.usb, pipe, 3, ctypes.sizeof(timeout), ctypes.byref(timeout))

    def close(self):
        if self.usb:
            winusb.WinUsb_Free(self.usb)
            self.usb = None
        if self.file:
            kernel32.CloseHandle(self.file)
            self.file = None

    def transfer(self, packet):
        tx = ctypes.create_string_buffer(packet)
        count = wintypes.ULONG()
        if not winusb.WinUsb_WritePipe(self.usb, self.ep_out, tx, len(packet), ctypes.byref(count), None):
            win_error("WinUsb_WritePipe")
        rx = ctypes.create_string_buffer(PACKET_SIZE)
        if not winusb.WinUsb_ReadPipe(self.usb, self.ep_in, rx, PACKET_SIZE, ctypes.byref(count), None):
            win_error("WinUsb_ReadPipe")
        return rx.raw[:count.value]

    def __enter__(self): return self
    def __exit__(self, *_): self.close()


def find_paths(pid=None):
    expected = None if pid is None else f"pid_{pid:04x}"
    paths = [p for p in enumerate_paths() if f"vid_{VID:04x}" in p.lower()]
    if expected:
        paths = [p for p in paths if expected in p.lower()]
    if not paths:
        kind = "device" if pid is None else f"PID {pid:04X} device"
        raise RuntimeError(f"CH572Q {kind} not found")
    return paths


sequence = 0


def command_status(device, code, payload=b"", argument=0):
    global sequence
    if len(payload) > PAYLOAD_SIZE:
        raise ValueError("payload too large")
    sequence = (sequence + 1) & 0xFFFF
    head = struct.pack("<2sBBHHI", MAGIC, PROTOCOL_VERSION, code, sequence,
                       len(payload), argument)
    packet = head + payload.ljust(PAYLOAD_SIZE, b"\0")
    packet += struct.pack("<I", zlib.crc32(packet) & 0xFFFFFFFF)
    response = device.transfer(packet)
    if len(response) != PACKET_SIZE:
        raise RuntimeError(f"short response: {len(response)} bytes")
    magic, version, response_code, response_seq, length, status = struct.unpack_from("<2sBBHHI", response)
    if magic != MAGIC or version != PROTOCOL_VERSION or response_code != (code | 0x80) or response_seq != sequence:
        raise RuntimeError("invalid response header")
    if zlib.crc32(response[:60]) & 0xFFFFFFFF != struct.unpack_from("<I", response, 60)[0]:
        raise RuntimeError("response CRC error")
    return status, response[12:12 + length]


def command(device, code, payload=b"", argument=0):
    status, response = command_status(device, code, payload, argument)
    if status:
        raise RuntimeError(STATUS_NAMES.get(status, f"device error {status}"))
    return response


def remote_command(device, code, payload=b""):
    deadline = time.monotonic() + 3.0
    while True:
        status, response = command_status(device, code, payload)
        if status == 0:
            return response
        if status != STATUS_PENDING:
            raise RuntimeError(STATUS_NAMES.get(status, f"device error {status}"))
        if time.monotonic() >= deadline:
            raise RuntimeError("remote command timeout")
        time.sleep(0.02)


def target_command(device, code, remote=False, payload=b""):
    if remote:
        return remote_command(device, REMOTE_COMMANDS[code], payload)
    return command(device, code, payload)


def show_info(device, remote=False):
    data = target_command(device, CMD_GET_INFO, remote)
    major, minor, patch, hardware, cdc_ports, rf_ready, protocol, _, clock, start, end = struct.unpack_from("<8B3I", data)
    print(f"firmware: {major}.{minor}.{patch}")
    print(f"hardware: CH5{hardware:02X}")
    print(f"CDC ports: {cdc_ports}")
    print(f"RF available: {'yes' if rf_ready else 'no'}")
    print(f"protocol: {protocol}")
    print(f"system clock: {clock} Hz")
    print(f"application: 0x{start:05X}-0x{end - 1:05X}")
    if len(data) >= 36:
        chip_id, boot_enabled = struct.unpack_from("<2B", data, 20)
        mac = data[22:28]
        unique_id = data[28:36]
        print(f"chip ID: 0x{chip_id:02X}")
        print(f"MAC: {':'.join(f'{byte:02X}' for byte in mac)}")
        print(f"unique ID: {unique_id.hex().upper()}")
        print(f"USB serial: CH572Q{mac.hex().upper()}")
        print(f"factory bootloader: {'enabled' if boot_enabled else 'disabled'}")


def show_status(device, remote=False):
    data = target_command(device, CMD_GET_STATUS, remote)
    configured, rf_ready, role, paired, state, _, server, uart_baud, rf_baud = struct.unpack(
        "<6BHII", data
    )
    print(f"USB configured: {'yes' if configured else 'no'}")
    print(f"RF available: {'yes' if rf_ready else 'no'}")
    print(f"RF role: {RF_ROLE_NAMES.get(role, role)}")
    print(f"paired: {'yes' if paired else 'no'}")
    print(f"pair state: {state}")
    print(f"server data: 0x{server:04X}")
    print(f"physical UART baud: {uart_baud}")
    print(f"RF UART baud: {rf_baud}")


def show_pair_status(device):
    paired, state, server = struct.unpack("<BBH", command(device, CMD_GET_PAIR))
    print(f"paired: {'yes' if paired else 'no'}")
    print(f"pair state: {state}")
    print(f"server data: 0x{server:04X}")


def show_rf_config(device, remote=False):
    data = target_command(device, CMD_GET_RF_CONFIG, remote)
    tx_power, _, _, _ = struct.unpack("<4B", data)
    print(f"TX power code: 0x{tx_power:02X}")


def show_rssi(device, remote=False):
    rssi, = struct.unpack("<b", target_command(device, CMD_GET_RSSI, remote))
    direction = "receiver to node" if remote else "node to receiver"
    print(f"{direction} RSSI: {rssi} dBm")


def get_scan_results(device):
    data = command(device, CMD_GET_SCAN)
    active, count, _ = struct.unpack_from("<BBH", data)
    devices = []
    for index in range(count):
        device_id, rssi, _, server_data = struct.unpack_from(
            "<6sbBH", data, 4 + index * 10
        )
        devices.append((device_id, rssi, server_data))
    return active, devices


def show_scan_results(device):
    active, devices = get_scan_results(device)
    print(f"scan active: {'yes' if active else 'no'}")
    if not devices:
        print("no RF UART nodes found")
    for index, (device_id, rssi, server_data) in enumerate(devices):
        address = ':'.join(f'{byte:02X}' for byte in device_id)
        print(f"  [{index}] {address}  RSSI {rssi} dBm  previous 0x{server_data:04X}")


def parse_number(value):
    return int(value, 0)


def main():
    parser = argparse.ArgumentParser(description="WCH Serial Tool configuration")
    parser.add_argument("--device", type=int,
                        help="select one device by list index; default is all devices")
    sub = parser.add_subparsers(dest="action", required=True)
    for name in ("list", "pair-status", "pair", "clear-pair",
                 "scan-results", "isp"):
        sub.add_parser(name)
    for name in ("info", "status", "rf-config", "rssi", "reset"):
        target = sub.add_parser(name)
        target.add_argument("--remote", action="store_true",
                            help="run the command on the paired RF node")
    scan = sub.add_parser("scan")
    scan.add_argument("--seconds", type=float, default=2.0,
                      help="scan duration before displaying results")
    connect = sub.add_parser("connect")
    connect.add_argument("index", type=int,
                         help="node index displayed by scan or scan-results")
    power = sub.add_parser("set-power")
    power.add_argument("value", type=parse_number,
                       help="WCH TX power code, for example 0x12 or 0x2D")
    power.add_argument("--remote", action="store_true",
                       help="set power on the paired RF node")
    args = parser.parse_args()

    if args.action == "list":
        paths = enumerate_paths()
        if not paths: print("no devices")
        for index, path in enumerate(paths): print(f"[{index}] {path}")
    else:
        paths = find_paths(APP_PID)
        if args.device is not None:
            if args.device >= len(paths):
                raise RuntimeError(
                    f"device index {args.device} not found; {len(paths)} device(s) available"
                )
            selected = [(args.device, paths[args.device])]
        else:
            selected = list(enumerate(paths))

        for position, (index, path) in enumerate(selected):
            print(f"[{index}] {path}")
            with Device(path) as device:
                if args.action == "info": show_info(device, args.remote)
                elif args.action == "status": show_status(device, args.remote)
                elif args.action == "pair-status": show_pair_status(device)
                elif args.action == "pair": command(device, CMD_START_PAIR); print("pairing restarted")
                elif args.action == "clear-pair": command(device, CMD_CLEAR_PAIR); print("pairing cleared")
                elif args.action == "rf-config": show_rf_config(device, args.remote)
                elif args.action == "rssi": show_rssi(device, args.remote)
                elif args.action == "scan-results": show_scan_results(device)
                elif args.action == "scan":
                    command(device, CMD_START_SCAN)
                    time.sleep(args.seconds)
                    show_scan_results(device)
                elif args.action == "connect":
                    _, devices = get_scan_results(device)
                    if args.index >= len(devices):
                        raise RuntimeError(
                            f"RF node index {args.index} not found; {len(devices)} node(s) available"
                        )
                    command(device, CMD_PAIR_DEVICE, devices[args.index][0])
                    print(f"pairing with RF node {args.index}")
                elif args.action == "set-power":
                    payload = struct.pack("<4B", args.value, 0, 0, 0)
                    target_command(device, CMD_SET_RF_CONFIG,
                                   args.remote, payload)
                    target = "remote node" if args.remote else "receiver"
                    print(f"{target} TX power set to 0x{args.value:02X}")
                elif args.action == "reset":
                    target_command(device, CMD_RESET, args.remote)
                    print("remote node resetting" if args.remote else "resetting")
                elif args.action == "isp":
                    command(device, CMD_ENTER_ISP)
                    print("erasing application entry and entering official WCH ISP bootloader")
            if position + 1 < len(selected):
                print()


if __name__ == "__main__":
    try:
        main()
    except (OSError, RuntimeError, ValueError) as error:
        print(f"error: {error}", file=sys.stderr)
        sys.exit(1)
