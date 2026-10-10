# CH9143 BLE UART protocol reverse engineering

This document records the CH9143-compatible BLE protocol used by WCH
BleComManager and BleUartApp. The conclusions below come from static analysis
of the official host libraries and applications, not from assumptions based on
the UUID names.

## Analyzed software

- BleComManager `1.2.0.0`, installed in `C:\Program Files (x86)\WCH\BleCOM`
- BleComManager `CH9140DLL.dll` `1.1.0.0`
- BleUartApp Windows `CH9140DLL.dll` `1.2.0.0`
- WCH BleUartLib `1.2`, downloaded from WCH file id `346`
- Android `CH9140Library.jar`, decompiled with CFR `0.152`
- Windows x86 `CH9140DLL.dll`, disassembled with MSVC `dumpbin`
- WCH CH592 BLE UART example and Device Information Service

The temporary reverse-engineering input was extracted under:

```text
C:\Users\Golde\AppData\Local\Temp\wch-bleuartlib-1.2
```

## BleComManager call surface

BleComManager imports these functions from `CH9140DLL.dll`:

```text
CH9140Init
CH9140EnumDevice
CH9140OpenDevice
CH9140CloseDevice
CH9140GetAllOpHandle
CH9140GetHandleAction
CH9140GetMtu
CH9140RegisterReadNotify
CH9140ReadBuffer
CH9140WriteBuffer
```

It does not import `CH9140GetCfgPara`, `CH9140SetCfgPara`, the extended
configuration functions, or the high-level `CH9140Uart*` functions. It builds
the UART protocol on top of the generic GATT functions itself.

The version shown by BleComManager's About page is the PC application version.
There is no private CH9143 firmware-version request in its virtual-COM path.

## Discovery packet

The Android library filters scan records by searching for one of these exact
10-byte signatures:

```text
57 AB 07 01 43 48 39 31 34 30    CH9140
57 AB 07 01 43 48 39 31 34 31    CH9141
57 AB 07 01 43 48 39 31 34 33    CH9143
```

For CH9143 the current firmware emits the signature as Manufacturer Specific
Data:

```text
0B FF 57 AB 07 01 43 48 39 31 34 33
```

`0B` is the AD length and `FF` is the Manufacturer Specific AD type. Device
name matching alone is insufficient; without the signature the official scan
library filters the device before any GATT connection is attempted.

## Required GATT database

The host library accepts a device only when it discovers service `FFF0` and all
three characteristics below:

| UUID | Required properties | Direction | Purpose |
| --- | --- | --- | --- |
| `FFF1` | Read, Notify, CCCD | device to host | UART data |
| `FFF2` | Write and Write Without Response | host to device | UART data |
| `FFF3` | Read, Write, Notify, CCCD | both | UART/MODEM control and status |

BleUartApp subscribes to both FFF3 and FFF1 after service discovery. A device
which advertises the CH9143 signature but lacks one characteristic or one CCCD
is reported as an invalid device or fails during connection setup.

The CH572 firmware additionally exposes:

- `180A`: standard Device Information Service
- `FFE0`: project-specific 64-byte configuration service

These extra services are not used by BleComManager.

BleComManager validates the characteristic property masks exactly. Its generic
GATT setup accepts `FFF1` only when `CH9140GetHandleAction` returns `5`
(`Read | Notify`), `FFF2` when it returns `2` (`Write`) and `FFF3` when it
returns `7` (`Read | Write | Notify`). A notify-only `FFF1` returns `4`; the
manager then leaves the FFF1 operation handle unset and repeatedly reconnects.

## Common frame format

Control frames use this layout:

```text
byte 0      command
byte 1..2   payload length, big endian
byte 3..N-2 payload
byte N-1    8-bit sum of bytes 3 through N-2
```

The command byte is excluded from the checksum. Successful command responses
normally set bit 7 of the command byte.

## UART configuration: 06 / 86

Request and response are exactly 12 bytes:

```text
06 00 09 00 <baud LE32> <data bits> <stop bits> <parity> <sum>
86 00 09 00 <baud LE32> <data bits> <stop bits> <parity> <sum>
```

- Data bits: `5` through `8`
- Stop bits: `1` means one stop bit, `2` means two stop bits
- Parity: `0` none, `1` odd, `2` even, `3` mark, `4` space
- Baud is a little-endian 32-bit integer

The official Android parser only accepts a response whose length is 12 and
whose first byte is `86`. Returning a four-byte generic ACK is incompatible.

## MODEM configuration: 07 / 87

Request and response are exactly 8 bytes:

```text
07 00 05 00 <flow> <DTR> <RTS> <sum>
87 00 05 00 <flow> <DTR> <RTS> <sum>
```

- Flow: `0` disabled, `1` hardware flow control enabled
- DTR/RTS: `1` active/low, `0` inactive/high

The official parser requires an 8-byte response beginning with `87`. DTR and
RTS occupy different bytes and must not be exchanged.

## UART and MODEM status: 88

FFF3 status notifications are exactly 7 bytes:

```text
88 00 04 00 <flags> <modem> <sum>
```

Flags in byte 4:

| Bit | Meaning |
| --- | --- |
| 0 | device UART can accept another BLE packet |
| 1 | MODEM input state in byte 5 is valid |
| 2 | device UART is paused; host should stop writing |

MODEM input state in byte 5:

| Bit | Signal |
| --- | --- |
| 7 | DCD |
| 6 | RI |
| 5 | DSR |
| 4 | CTS |

Typical notifications without MODEM inputs are:

```text
88 00 04 00 01 00 01    ready
88 00 04 00 04 00 04    paused
```

BleUartApp uses the ready/paused state for software-side packet pacing. FFF3
notification support is therefore part of the UART data path, not optional
metadata.

## UART data path

- Host writes raw UART bytes to FFF2.
- Device notifies raw UART bytes on FFF1.
- Payload length is the negotiated ATT MTU minus 3.
- CH9143 uses Write Without Response; CH9141 is handled differently by the
  Android library.
- The host stops sending after an FFF3 paused notification and resumes after a
  ready notification.

## Generic 512-byte configuration commands

These commands were recovered from the exported Windows DLL functions. They
are not called by BleComManager's virtual-COM implementation.

Configuration selector:

```text
01    normal configuration
03    extended configuration
```

GET request:

```text
01 00 02 <selector> <sum>
```

GET response is 517 bytes:

```text
81 02 02 <selector> <512 data bytes> <sum>
```

SET request is 517 bytes:

```text
02 02 02 <selector> <512 data bytes> <sum>
```

SET response is 5 bytes:

```text
82 00 02 <selector> <sum>
```

RESET request/response:

```text
03 00 02 <selector> <sum>
83 00 02 <selector> <sum>
```

Reset channel request/response:

```text
04 00 02 00 00
84 00 02 00 00
```

The DLL splits the 517-byte SET request into 20-byte FFF3 writes. It reads a
GET response in multiple operations and strips the four-byte response header
and final checksum before returning the 512-byte region to its caller.

WCH does not publish the field layout of the two 512-byte regions in the host
library package. The CH572 firmware does not claim semantic compatibility with
these regions and does not reserve another 1 KiB of its 12 KiB SRAM for fake
data.

## Current CH572 implementation

Implemented:

- CH9143 discovery signature
- FFF0, FFF1, FFF2 and FFF3 with independent CCCDs
- full-length `06/86` and `07/87` readback
- `88` ready and paused notifications
- FFF1/FFF2 UART data transport
- DTR and RTS output control
- standard Device Information Service with real CH572 identity

Not implemented:

- semantics and persistence of the two undocumented 512-byte CH9143
  configuration regions
- DCD, RI, DSR and CTS inputs, because the current hardware only assigns DTR
  and RTS outputs
- actual automatic CTS/RTS hardware flow control

## Current observed failure stage

After adding the discovery signature:

- BleComManager discovers the device but cannot bring the virtual COM port
  online reliably.
- BleUartApp discovers the device but does not complete connection.

This proves advertisement filtering is fixed. Remaining failure candidates are
now limited to:

1. BLE link establishment or immediate link termination.
2. FFF0 service discovery or characteristic-property validation.
3. FFF3 or FFF1 CCCD subscription.
4. FFF3 UART/MODEM request write and response read.

Use `software/ch9140_dll_probe` to test these stages directly through the same
WCH DLL APIs used by the applications.
