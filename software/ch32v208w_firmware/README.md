# CH32V208W CH9143-compatible UART firmware

CH32V208W firmware combining a CH9143-compatible BLE UART with a WinUSB Vendor configuration interface. BLE, USB Vendor and the physical UART run at the same time; this firmware does not expose USB CDC.

## Interfaces

- BLE advertising name: `WCH Serial Tool`
- BLE UART service: `FFF0`
  - `FFF1`: UART-to-BLE read/notification
  - `FFF2`: BLE-to-UART write/write without response
  - `FFF3`: CH9143-compatible UART and modem configuration
- USB: VID `1A86`, PID `572F`
  - Vendor interface 0
  - EP7 OUT `0x07`, EP7 IN `0x87`
  - Microsoft OS 2.0 device-level descriptor for automatic WinUSB binding
- Physical UART: USART1, PA9 TX and PA10 RX, default 115200 8-N-1
- External RF PA: PD3 TXEN and PD4 RXEN, active high
- Status LED: PC13, active low

The CH9143 modem command is accepted for compatibility, but RTS and DTR are not routed to GPIO in this hardware definition.

## Memory and clock

- System clock: 96 MHz from the 32 MHz HSE
- Code/RAM allocation: 160 KiB Flash + 32 KiB RAM
- BLE SNV uses the addresses defined by the WCH BLE HAL

The 160 KiB/32 KiB User Select Word must match the linker script.

## Build

Open the project in MounRiver Studio or run:

```powershell
.\build.ps1
```

Both methods produce `obj/ch32v208w_firmware.hex`.
