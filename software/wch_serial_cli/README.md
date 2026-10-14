# WCH Serial CLI

Requires Windows 10/11 and `uv`. Do not manually create or activate a virtual
environment; `uv run` manages Python and the dependencies from
`pyproject.toml`.

## Two-port UART integrity test

Cross-connect TX to RX and share GND between the CH57x UART and an FT232 or
another reference UART, using compatible logic levels. For wireless tests,
connect the reference adapter to the RF node and select the receiver's RF COM
port. Pass the CH57x COM port, reference COM port, and their common baud rate:

```powershell
uv run uart_loopback_test.py 83 84 3000000
uv run uart_loopback_test.py COM83 COM84 115200 --bytes 1048576
```

The test sends different deterministic binary streams through both ports at
the same time, checks each stream at the opposite port, and stops at the first
missing or mismatched byte.

To isolate a direction, A is the first (CH57x) port and B is the second
(reference) port:

```powershell
uv run uart_loopback_test.py 93 78 3000000 --direction a-to-b
uv run uart_loopback_test.py 93 78 3000000 --direction b-to-a
uv run uart_loopback_test.py 93 78 3000000 --chunk 64 --interval 0.01
```

Both directions report their write result and receive byte count on failure,
including the first differing offset and nearby bytes. A write timeout does
not establish how many bytes the Windows driver transmitted. Each chunk is
verified before the next is sent; this checks data integrity and does not
measure the maximum continuous streaming rate.

The CH572 RF CDC port is limited to 1.5 Mbaud. Its physical CDC UART can use
3 Mbaud when the generated baud error is within the firmware's 2% limit.

The application interface is matched using:

```text
VID:      0x1A86
RF PID:   0x572D
BLE PID:  0x572E
CH32 PID: 0x572F
GUID: {A1D4935B-4D76-4E6F-B623-6C856A67A8E9}
```

Run commands from this directory:

```powershell
uv run wch_serial_cli.py list
uv run wch_serial_cli.py info
uv run wch_serial_cli.py status
uv run wch_serial_cli.py pair-status
uv run wch_serial_cli.py pair
uv run wch_serial_cli.py clear-pair
uv run wch_serial_cli.py rf-config
uv run wch_serial_cli.py set-power 0x12
uv run wch_serial_cli.py rssi
uv run wch_serial_cli.py --ble 0 phy
uv run wch_serial_cli.py info --remote
uv run wch_serial_cli.py status --remote
uv run wch_serial_cli.py rf-config --remote
uv run wch_serial_cli.py set-power 0x12 --remote
uv run wch_serial_cli.py rssi --remote
uv run wch_serial_cli.py reset --remote
uv run wch_serial_cli.py scan
uv run wch_serial_cli.py scan-results
uv run wch_serial_cli.py connect 0
uv run wch_serial_cli.py reset
uv run wch_serial_cli.py isp
uv run wch_serial_cli.py xtal
uv run wch_serial_cli.py xtal-test
uv run wch_serial_cli.py xtal-calibrate
uv run wch_serial_cli.py xtal-calibrate --save
```

Commands operate on all connected receivers by default. List them and select
one by index when needed:

```powershell
uv run wch_serial_cli.py list
uv run wch_serial_cli.py --device 1 status
uv run wch_serial_cli.py --device 1 pair
```

Common WCH TX-power codes:

```text
0x12 = 0 dBm
0x18 = +2 dBm
0x1F = +4 dBm
0x2D = +6 dBm
```

The configured TX power is saved in flash on both the receiver and RF node.
The `rssi` command reports node-to-receiver signal strength. Use
`rssi --remote` for receiver-to-node signal strength. RSSI is available only
while the RF link is connected.

The `phy` command reports the negotiated BLE transmit and receive PHY on the
CH32V208 firmware. Query it through the BLE configuration service while the
link is active, for example `uv run wch_serial_cli.py --ble 0 phy`.

`xtal-test` measures all eight HSE load-capacitance settings against USB SOF,
prints their frequency errors, and restores the original setting.
`xtal-calibrate` leaves the best setting active for the current session;
add `--save` to store it in flash for future boots. The USB host clock is the
reference, so this is board-level adjustment rather than laboratory-grade
frequency calibration.

The `isp` command disconnects USB, erases the 4 KB block containing the
application entry at address zero, and resets the CH572. The factory bootloader
then treats the chip as having no application and waits for the official WCH
ISP tool. The application will not boot again until new firmware is programmed.
