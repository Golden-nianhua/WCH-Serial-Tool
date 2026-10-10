# CH572Q USB/BLE UART firmware

CH572Q firmware providing one physical UART through either USB CDC or BLE UART.
USB CDC plus WinUSB configuration mode and BLE UART mode are mutually exclusive;
the proprietary 2.4 GHz RF transport is not included.

## Interfaces

- On power-up, USB has 500 ms to enumerate
- USB enumerated: USB CDC on EP1 plus WinUSB configuration on EP7; BLE stays disabled
- USB not enumerated: USB is disabled and BLE UART mode starts
- UART0 in both modes: PA2 RX, PA3 TX, default 115200 8-N-1
- MODEM outputs in BLE mode: PA0/UDM RTS#, PA1/UDP DTR#, active low
- LED: PA7, high level turns the LED on
- USB EP0: control transfers
- USB EP1 OUT/IN: physical CDC UART data
- USB EP7 OUT/IN: WinUSB vendor configuration
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
  MAC-derived serial number, firmware `0.2.0`, and manufacturer

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
`WCH Serial Tool`. The information response reports one CDC port and identifies
this as the combined USB/BLE UART firmware.
Each BLE configuration fragment begins with its byte offset; the remaining
1-19 bytes belong to the unchanged 64-byte vendor configuration packet.

The USB CDC line-coding policy matches the CH570 physical UART implementation:

- Baud rates up to 1 Mbps are accepted
- 6 Mbps and 12 Mbps are accepted directly
- Other rates above 1 Mbps are accepted only when the UART divider error is at most 2%
- Rates above 12 Mbps are rejected
- Unsupported `SET_LINE_CODING` requests are stalled so the host can report failure

The system clock remains fixed at 100 MHz in both USB and BLE modes.

## HSE calibration

In USB mode, the firmware can test the eight internal 32 MHz crystal load
settings from 6 pF through 20 pF. It compares 100 MHz SysTick counts with the
host's 1 ms USB SOF reference. Temporary settings do not write flash, and the
saved setting is restored before USB or BLE starts on the next boot.

```powershell
uv run ..\wch_serial_cli\wch_serial_cli.py xtal
uv run ..\wch_serial_cli\wch_serial_cli.py xtal-test
uv run ..\wch_serial_cli\wch_serial_cli.py xtal-calibrate
uv run ..\wch_serial_cli\wch_serial_cli.py xtal-calibrate --save
```

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
