# CH572Q USB/BLE UART firmware

CH572Q firmware providing one physical UART through either USB CDC or BLE UART.
The image contains a USB selector application and a separate BLE application;
the proprietary 2.4 GHz RF transport is not included.

## Interfaces

- On power-up, USB has 500 ms to enumerate
- USB enumerated: USB CDC on EP1 plus WinUSB configuration on EP7; BLE stays disabled
- USB not enumerated: the selector requests a software reset, then the boot
  stub starts the BLE UART application
- UART0 in both modes: PA2 RX, PA3 TX, default 115200 8-N-1
- MODEM outputs in BLE mode: PA0/UDM RTS#, PA1/UDP DTR#, active low
- LED: PA7, high level turns the LED on
- USB EP0: control transfers
- USB EP1 OUT/IN: physical CDC UART data
- USB EP2 IN: CDC serial-state notification endpoint
- USB EP7 OUT/IN: WinUSB vendor configuration
- BLE UART service: FFF0
  - FFF1: UART-to-BLE read/notification
  - FFF2: BLE-to-UART write/write without response
  - FFF3: CH9143-compatible UART and MODEM configuration, notification,
    and command response
- BLE advertising name: `WCH Serial Tool`
- CH9143 discovery signature: manufacturer data
  `57 AB 07 01 43 48 39 31 34 33`
- Standard Device Information service (`180A`): CH572 model/hardware,
  MAC-derived serial number, firmware `0.2.0`, and manufacturer

The BLE application has its own 5 KiB protocol heap. USB configuration,
flash writes, and factory-ISP entry are linked only into the USB selector.

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

USB mode uses a 24 MHz system clock. Its CDC line-coding policy is:

- Rates through 3 Mbps are accepted only when the UART divider error is at most 2%
- 3 Mbps, 1.5 Mbps and 1 Mbps use exact integer dividers; 2 Mbps is unsupported
- 921600 bps is unsupported because its actual rate would be 1 Mbps
- Rates above 3 Mbps are rejected
- Unsupported `SET_LINE_CODING` requests are stalled so the host can report failure

BLE mode switches to 100 MHz before the BLE stack starts. UART divisors are
recalculated after the clock switch. The same 2% error limit applies to FFF3
UART configuration. For example, 115200 bps is supported, while 921600 bps and
1 Mbps are rejected at 100 MHz. A rejected FFF3 write returns a GATT value
error instead of silently keeping the previous baud rate.

The BLE stack allows four packets per connection event. UART notification
scheduling uses up to four packets for one-way UART-to-BLE traffic. After
BLE-to-UART traffic is observed, it limits notifications to two packets for
100 ms, leaving room for CH9143 ready/paused control and incoming writes during
full-duplex traffic. It uses the WCH
high-speed example's 496-byte link buffers, and reserves seven packet buffers
from a 5 KiB heap. UART notifications are capped at 244 bytes for the common
247-byte ATT MTU. Four seconds after connecting, the firmware requests the
official speed-test interval of 7.5 ms. The PHY remains at 1 Mbps for
BleComManager compatibility. Actual throughput still depends on the central's
negotiated MTU and connection interval. Input beyond the resulting link rate
is dropped when the UART receive ring becomes full.

## HSE calibration

In USB mode, the firmware can test the eight internal 32 MHz crystal load
settings from 6 pF through 20 pF. It compares 24 MHz SysTick counts with the
host's 1 ms USB SOF reference. Temporary settings do not write flash, and the
saved setting is restored before USB or BLE starts on the next boot.

```powershell
uv run ..\wch_serial_cli\wch_serial_cli.py xtal
uv run ..\wch_serial_cli\wch_serial_cli.py xtal-test
uv run ..\wch_serial_cli\wch_serial_cli.py xtal-calibrate
uv run ..\wch_serial_cli\wch_serial_cli.py xtal-calibrate --save
```

## Memory layout

- `0x00000000-0x00001FFF`: boot jump and reservation
- `0x00002000-0x00011FFF`: USB selector, CDC, and WinUSB configuration
- `0x00012000-0x00039FFF`: BLE UART application
- `0x0003A000-0x0003AFFF`: persistent settings
- SRAM: 12 KiB

## Build

Run:

```powershell
.\build.ps1
```

The build creates separate USB and BLE ELF/map files, then combines them into
the single programming image `obj/ch572q_firmware.hex`.
