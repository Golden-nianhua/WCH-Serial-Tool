# WCH Serial Tool

WCH Serial Tool is an unofficial USB and wireless serial bridge project for
WCH microcontrollers. It currently contains the verified CH572 firmware and a
Windows WinUSB configuration CLI.

This project is independent and is not an official WCH product.

## Included components

- `software/ch570q_firmware`: CH572 dual-CDC firmware with a physical UART,
  2.4 GHz UART bridge, pairing, remote node configuration and software reset.
- `software/wch_serial_cli`: Windows command-line configuration utility using
  WinUSB and `uv`.

The CH591 firmware and hardware/3D design files are still under development
and are not part of the initial public repository.

See the README in each component directory for build and usage instructions.
