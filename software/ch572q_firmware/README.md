# CH572Q BLE UART firmware

CH572Q firmware providing one BLE UART bridge and a USB/ BLE configuration
interface. USB CDC and the proprietary 2.4 GHz RF transport are intentionally
not included. USB configuration mode and BLE UART mode are mutually exclusive.

## Interfaces

- On power-up, USB has 500 ms to enumerate
- USB enumerated: configuration mode using EP7; UART and BLE stay disabled
- USB not enumerated: USB is disabled and BLE UART mode starts
- UART0 in BLE mode: PA2 RX, PA3 TX, default 115200 8-N-1
- MODEM outputs in BLE mode: PA0/UDM RTS#, PA1/UDP DTR#, active low
- LED: PA7, high level turns the LED on
- USB: EP0 control and EP7 WinUSB vendor configuration only
- BLE UART service: FFF0
  - FFF1: UART-to-BLE read/notification
  - FFF2: BLE-to-UART write/write without response
  - FFF3: CH9143-compatible UART and MODEM configuration, notification,
    and command response
- BLE configuration service: FFE0
  - FFE1: 64-byte configuration response, fragmented notifications
  - FFE2: 64-byte configuration request, fragmented writes
- BLE advertising name: `CH9143BLE2U`
- CH9143 discovery signature: manufacturer data
  `57 AB 07 01 43 48 39 31 34 33`
- Standard Device Information service (`180A`): CH572 model/hardware,
  MAC-derived serial number, firmware `0.1.2`, and manufacturer

FFF3 implements the CH9143 UART transport control used by BleComManager:

- UART request: `06 00 09 00 <baud LE32> <bits> <stop> <parity> <sum>`
- UART response: the same 12-byte frame with command `86`
- MODEM request: `07 00 05 00 <flow> <DTR> <RTS> <sum>`
- MODEM response: the same 8-byte frame with command `87`
- UART-ready notification: `88 00 04 00 01 00 01`
- UART-paused notification: `88 00 04 00 04 00 04`

The checksum is the 8-bit sum from byte 3 through the byte before the checksum.
UART settings are applied when a virtual COM port is opened or reconfigured and
are not saved to flash. Stop-bit values follow the WCH API (`1` or `2`). RTS#
and DTR# are driven from the matching fields in the CH9143 MODEM packet. CTS
and automatic hardware flow control are accepted for compatibility but are not
enabled in hardware. BleComManager subscribes to both FFF1 and FFF3 before it
opens the virtual COM port, so both characteristics include independent CCCDs.

The USB interface uses VID `1A86`, PID `572E`, the existing device-interface
GUID, 64-byte configuration packets, unique USB serial generation, reset, and
factory-ISP entry support. Its USB product and interface name is
`WCH Serial Tool`. The information response reports zero CDC ports.
Each BLE configuration fragment begins with its byte offset; the remaining
1-19 bytes belong to the unchanged 64-byte vendor configuration packet.

## Memory layout

- `0x00000000-0x00001FFF`: 8 KiB boot reservation
- `0x00002000-0x00039FFF`: application
- SRAM: 12 KiB

## Build

Open the project in MounRiver Studio or run:

```powershell
.\build.ps1
```

Both builds produce `obj/ch572q_firmware.hex`.
