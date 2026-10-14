#!/usr/bin/env python3
import argparse
import concurrent.futures
import random
import sys
import time
from dataclasses import dataclass

import serial


def port_name(value: str) -> str:
    return f"COM{value}" if value.isdecimal() else value


def read_exact(port: serial.Serial, length: int, timeout: float) -> bytes:
    received = bytearray()
    deadline = time.monotonic() + timeout

    while len(received) < length and time.monotonic() < deadline:
        data = port.read(length - len(received))
        if data:
            received.extend(data)
    return bytes(received)


@dataclass
class WriteResult:
    length: int | None
    elapsed: float
    error: str | None = None


def write_chunk(port: serial.Serial, data: bytes) -> WriteResult:
    started = time.monotonic()
    try:
        length = port.write(data)
        return WriteResult(length, time.monotonic() - started)
    except (serial.SerialException, OSError) as error:
        return WriteResult(
            None, time.monotonic() - started,
            f"{type(error).__name__}: {error}",
        )


def first_difference(expected: bytes, received: bytes) -> int | None:
    for index, (left, right) in enumerate(zip(expected, received)):
        if left != right:
            return index
    if len(expected) != len(received):
        return min(len(expected), len(received))
    return None


def report_transfer(
    direction: str, offset: int, expected: bytes, received: bytes,
    written: WriteResult,
) -> bool:
    difference = first_difference(expected, received)
    passed = difference is None and written.length == len(expected)
    sent = "unknown" if written.length is None else str(written.length)

    print(
        f"{'OK' if passed else 'FAIL'} {direction}: "
        f"write {sent}/{len(expected)} bytes ({written.elapsed:.3f}s), "
        f"received {len(received)}/{len(expected)} bytes"
    )
    if written.error:
        print(f"  write error: {written.error}")
    if difference is None:
        return passed

    print(f"  first difference at stream offset {offset + difference}")
    if difference < len(received):
        print(
            f"  expected 0x{expected[difference]:02X}, "
            f"received 0x{received[difference]:02X}"
        )
    else:
        print("  receive timeout before this byte")
    start = max(0, difference - 8)
    end = difference + 16
    print(f"  expected [{start}:{end}]: {expected[start:end].hex(' ')}")
    print(f"  received [{start}:{end}]: {received[start:end].hex(' ')}")
    return passed


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Two-port full-duplex UART data-integrity test (8-N-1)"
    )
    parser.add_argument("ch57x_port", help="CH57x COM number or name")
    parser.add_argument("reference_port", help="FT232/reference COM number or name")
    parser.add_argument("baud", type=int, help="baud rate used by both ports")
    parser.add_argument(
        "--direction", choices=("both", "a-to-b", "b-to-a"), default="both",
        help="both (default), CH57x to reference (a-to-b), or reverse (b-to-a)",
    )
    parser.add_argument(
        "--bytes", type=int, default=65536,
        help="bytes to test in each direction (default: 65536)",
    )
    parser.add_argument(
        "--chunk", type=int, default=512,
        help="bytes per simultaneous transfer (default: 512)",
    )
    parser.add_argument(
        "--timeout", type=float, default=2.0,
        help="receive/write timeout per chunk in seconds (default: 2.0)",
    )
    parser.add_argument(
        "--settle", type=float, default=1.0,
        help="delay after opening ports before testing (default: 1.0)",
    )
    parser.add_argument("--seed", type=int, default=1, help="test-data seed")
    parser.add_argument(
        "--interval", type=float, default=0.0,
        help="extra delay between chunks in seconds (default: 0)",
    )
    args = parser.parse_args()

    if (args.baud <= 0 or args.bytes <= 0 or args.chunk <= 0 or
            args.timeout <= 0 or args.settle < 0 or args.interval < 0):
        parser.error("baud, bytes, chunk and timeout must be positive; delays must be nonnegative")

    ch57x_name = port_name(args.ch57x_port)
    reference_name = port_name(args.reference_port)
    if ch57x_name.casefold() == reference_name.casefold():
        parser.error("the two ports must be different")

    ch57x_generator = random.Random(args.seed)
    reference_generator = random.Random(args.seed ^ 0x57F232)
    checked = 0

    serial_options = {
        "baudrate": args.baud,
        "bytesize": serial.EIGHTBITS,
        "parity": serial.PARITY_NONE,
        "stopbits": serial.STOPBITS_ONE,
        "timeout": 0.02,
        "write_timeout": args.timeout,
        "xonxoff": False,
        "rtscts": False,
        "dsrdtr": False,
    }

    try:
        with (
            serial.Serial(ch57x_name, **serial_options) as ch57x,
            serial.Serial(reference_name, **serial_options) as reference,
            concurrent.futures.ThreadPoolExecutor(max_workers=4) as executor,
        ):
            ch57x.reset_input_buffer()
            ch57x.reset_output_buffer()
            reference.reset_input_buffer()
            reference.reset_output_buffer()
            time.sleep(args.settle)
            ch57x.reset_input_buffer()
            reference.reset_input_buffer()
            print(
                f"A={ch57x_name}, B={reference_name}, {args.baud} baud 8-N-1, "
                f"direction={args.direction}, chunk={args.chunk}; flow control disabled",
                flush=True,
            )
            started = time.monotonic()
            progress_time = started

            while checked < args.bytes:
                length = min(args.chunk, args.bytes - checked)
                from_ch57x = ch57x_generator.randbytes(length)
                from_reference = reference_generator.randbytes(length)

                directions = []
                if args.direction != "b-to-a":
                    directions.append((ch57x, reference, from_ch57x))
                if args.direction != "a-to-b":
                    directions.append((reference, ch57x, from_reference))

                reads = [
                    executor.submit(read_exact, target, length, args.timeout)
                    for _, target, _ in directions
                ]
                writes = [
                    executor.submit(write_chunk, source, data)
                    for source, _, data in directions
                ]
                results = [
                    (written.result(), received.result())
                    for written, received in zip(writes, reads)
                ]
                failed = any(
                    written.length != length or data != received
                    for (_, _, data), (written, received) in zip(directions, results)
                )
                if failed:
                    print()
                    for (source, target, expected), (written, received) in zip(directions, results):
                        report_transfer(
                            f"{source.port} -> {target.port}", checked,
                            expected, received, written,
                        )
                    return 1

                checked += length
                elapsed = time.monotonic() - started
                one_way_rate = checked / elapsed if elapsed else 0.0
                if time.monotonic() - progress_time >= 1.0 or checked == args.bytes:
                    print(
                        f"\r{checked}/{args.bytes} bytes per active direction OK, "
                        f"{one_way_rate / 1024:.1f} KiB/s per direction",
                        end="", flush=True,
                    )
                    progress_time = time.monotonic()
                if args.interval and checked < args.bytes:
                    time.sleep(args.interval)

            for _, target, _ in directions:
                extra = target.read(1)
                if extra:
                    print(f"\nFAIL {target.port}: unexpected trailing data {extra.hex()}")
                    return 1
    except (serial.SerialException, OSError) as error:
        print(f"\nERROR: {error}")
        return 2

    elapsed = time.monotonic() - started
    print(
        f"\nPASS: {checked} bytes matched per active direction at {args.baud} baud "
        f"in {elapsed:.2f} s ({checked / elapsed / 1024:.1f} KiB/s per direction)"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
