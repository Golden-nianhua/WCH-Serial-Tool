# WCH Serial Tool CH572 firmware

Current USB layout:

- EP1 OUT/IN: physical CDC UART on PA2/PA3
- EP2 OUT/IN: paired 2.4 GHz CDC UART
- EP3 IN: physical CDC serial-state notification
- EP5 IN: 2.4 GHz CDC serial-state notification
- EP7 OUT/IN: WinUSB vendor configuration channel

Runtime role selection:

- USB enumerates within 0.5 seconds: USB receiver role; EP1 remains the physical
  UART and EP2 carries the paired 2.4 GHz UART.
- USB is not enumerated within 0.5 seconds: USB is disabled and the same firmware
  enters the UART RF-node role. PA2/PA3 are UART RX/TX, PA0 is RTS, and PA1 is
  DTR.

The RF receiver and RF node share the packet buffer and role memory. Only the
selected role is initialized at runtime.

RF payloads use the radio CRC, sequence numbers, acknowledgements and retry.
The receiver acknowledges a UART payload only after it has been stored in the
USB-bound queue. If that queue is full, the sequence is left pending so the RF
node retransmits the same payload after the host drains the queue.

## UART baud rates

EP1 accepts rates through 3 Mbaud when the generated UART baud error is at most
2%. EP2 and the paired RF node are limited to 1.5 Mbaud. Unsupported rates are
rejected by the CDC `SET_LINE_CODING` control request.

The system clock remains fixed at 24 MHz in both USB receiver and RF node
modes. CDC baud-rate changes only update the UART divisor.

| Requested | Generated UART baud |
| ---: | ---: |
| 3,000,000 | 3,000,000 |
| 2,000,000 | unsupported |
| 1,500,000 | 1,500,000 |
| 1,000,000 | 1,000,000 |

At 24 MHz, 3 Mbaud, 1.5 Mbaud and 1 Mbaud use exact integer divisors. 2 Mbaud
cannot be generated within the 2% limit and is rejected.

EP2 rejects 3 Mbaud even though its divisor is exact: at the 24 MHz system clock,
RF interrupt latency can overflow the CH572's eight-byte UART FIFO. Full-duplex
1.5 Mbaud has been verified with one MiB per direction. The RF node applies the
selected UART divisor after the current RF transaction completes; the UART
setting does not increase the 2.4 GHz link's payload throughput.

## Vendor configuration protocol

EP7 uses one fixed 64-byte little-endian packet:

| Offset | Size | Field |
| ---: | ---: | --- |
| 0 | 2 | magic `55 57` |
| 2 | 1 | protocol version, currently `1` |
| 3 | 1 | command; responses set bit 7 |
| 4 | 2 | sequence |
| 6 | 2 | payload length, maximum 48 |
| 8 | 4 | request argument or response status |
| 12 | 48 | payload |
| 60 | 4 | CRC32 of bytes 0 through 59 |

Commands:

| Command | Value | Request payload | Response payload |
| --- | ---: | --- | --- |
| Get information | `0x01` | none | firmware, hardware, clock and flash layout |
| Get status | `0x02` | none | USB state, RF role, pair state, physical UART baud and RF UART baud |
| Get pair status | `0x03` | none | paired, pair state and server data |
| Start pairing | `0x04` | none | none |
| Clear pairing | `0x05` | none | none |
| Get RF configuration | `0x06` | none | four bytes; byte 0 is TX power |
| Set RF configuration | `0x07` | four bytes | none |
| Get RSSI | `0x0B` | none | signed RSSI byte in dBm |
| Get HSE calibration | `0x0C` | none | capacitance, saved value, SOF samples, ticks/ms and error ppm |
| Set HSE capacitance | `0x0D` | capacitance, save flag and two reserved bytes | none |
| Software reset | `0x11` | none | none |
| Remote get information | `0x21` | none | paired RF node information |
| Remote get status | `0x22` | none | paired RF node status |
| Remote get configuration | `0x23` | none | paired RF node configuration |
| Remote set configuration | `0x24` | four bytes | none |
| Remote reset | `0x25` | none | none |
| Remote get RSSI | `0x26` | none | signed RSSI byte in dBm |

Remote commands are delivered through the node's existing 10 ms status poll.
The receiver repeats one transaction until its response arrives, and the node
deduplicates it by transaction ID. Remote reset is performed only after the
receiver acknowledges the successful response.

The TX-power and HSE-capacitance settings share the reserved flash page at
`0x3A000` and survive reset. Pairing metadata remains in the separate page at
`0x3B000`.

## HSE calibration

The 32 MHz crystal load can be selected from 6 pF through 20 pF in 2 pF steps.
While USB is configured, the firmware measures 24 MHz SysTick counts against
the host's 1 ms USB SOF reference. A temporary setting does not write flash;
only an explicit save changes the value restored at the next boot.

```powershell
uv run ..\wch_serial_cli\wch_serial_cli.py xtal
uv run ..\wch_serial_cli\wch_serial_cli.py xtal-test
uv run ..\wch_serial_cli\wch_serial_cli.py xtal-calibrate
uv run ..\wch_serial_cli\wch_serial_cli.py xtal-calibrate --save
```

The TX-power byte uses the WCH RF encoding. Common values are `0x12` for
0 dBm, `0x18` for +2 dBm, `0x1F` for +4 dBm and `0x2D` for +6 dBm. The power
setting applies immediately and is stored in flash.

PA7 drives an active-high LED:

- fast blink: waiting for USB enumeration
- short pulse once per second: USB configured and idle
- solid 40 ms pulse: USB data activity
- RF node fast blink: not paired
- RF node double pulse: paired but not connected
- RF node short pulse once per second: connected

The first 8 KB of flash is reserved. A boot jump stub at `0x0000` immediately
jumps to the normal CH572 application startup at `0x2000`. Both are linked into
the same ELF and emitted in the same HEX file.

MounRiver Studio and `build.ps1` both use `Ld/Link.ld` and produce the same
single image at `obj/ch570q_firmware.hex`. The script keeps its intermediate
objects in `obj/manual` so they do not overwrite MRS object files.
