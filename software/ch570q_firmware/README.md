# WCH Serial Tool CH572 firmware

Current USB layout:

- EP1 OUT/IN: physical CDC UART on PA2/PA3
- EP2 OUT/IN: paired 2.4 GHz CDC UART
- EP7 OUT/IN: WinUSB vendor configuration channel

Runtime role selection:

- USB enumerates within 0.5 seconds: USB receiver role; EP1 remains the physical
  UART and EP2 carries the paired 2.4 GHz UART.
- USB is not enumerated within 0.5 seconds: USB is disabled and the same firmware
  enters the UART RF-node role. PA2/PA3 are UART RX/TX, PA0 is RTS, and PA1 is
  DTR.

The RF receiver and RF node share the packet buffer and role memory. Only the
selected role is initialized at runtime.

## UART baud rates

EP1 accepts every rate up to 1 Mbaud. Above 1 Mbaud it accepts rates whose
generated baud error is at most 2%, plus the requested 6 Mbaud and 12 Mbaud
compatibility settings. Requests above 12 Mbaud are rejected by the CDC
`SET_LINE_CODING` control request.

The system clock remains fixed at 100 MHz in both USB receiver and RF node
modes. CDC baud-rate changes only update the UART divisor.

| Requested | Generated UART baud |
| ---: | ---: |
| 12,000,000 | 12,500,000 |
| 6,000,000 | 6,250,000 |
| 3,000,000 | 3,125,000 |
| 2,000,000 | 2,083,333 |
| 1,500,000 | 1,562,500 |
| 1,000,000 | 961,538 |

CH572 cannot generate exact 12 Mbaud or 6 Mbaud from its available system
clock dividers. Those two compatibility settings have a +4.17% error.

EP2 accepts rates up to 1.5 Mbaud. Higher rates are rejected by CDC. The RF
node applies the selected UART divisor after the current RF transaction
completes.

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
| Software reset | `0x11` | none | none |
| Remote get information | `0x21` | none | paired RF node information |
| Remote get status | `0x22` | none | paired RF node status |
| Remote get configuration | `0x23` | none | paired RF node configuration |
| Remote set configuration | `0x24` | four bytes | none |
| Remote reset | `0x25` | none | none |

Remote commands are delivered through the node's existing 10 ms status poll.
The receiver repeats one transaction until its response arrives, and the node
deduplicates it by transaction ID. Remote reset is performed only after the
receiver acknowledges the successful response.

The TX-power byte uses the WCH RF encoding. Common values are `0x12` for
0 dBm, `0x18` for +2 dBm, `0x1F` for +4 dBm and `0x2D` for +6 dBm. The power
setting currently applies immediately and is not stored in flash.

PA7 drives an active-high LED:

- fast blink: waiting for USB enumeration
- short pulse once per second: USB configured and idle
- solid 40 ms pulse: USB data activity

The first 8 KB of flash is reserved. A boot jump stub at `0x0000` immediately
jumps to the normal CH572 application startup at `0x2000`. Both are linked into
the same ELF and emitted in the same HEX file.

MounRiver Studio and `build.ps1` both use `Ld/Link.ld` and produce the same
single image at `obj/ch570q_firmware.hex`. The script keeps its intermediate
objects in `obj/manual` so they do not overwrite MRS object files.
