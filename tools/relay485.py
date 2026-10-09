#!/usr/bin/env python3
"""Manual and scripted control for the ABox RS485 relay test firmware."""

from __future__ import annotations

import argparse
import json
import sys
import time
from datetime import datetime, timezone
from typing import Iterable, Optional

try:
    import serial
except ImportError:  # pragma: no cover - exercised by the CLI error path
    serial = None


MIN_CYCLE_MS = 100
MAX_CYCLE_MS = 600_000
BAUDRATE = 115200


class Relay485Error(RuntimeError):
    """A serial transport or relay protocol failure."""


def timestamp() -> str:
    return datetime.now(timezone.utc).isoformat(timespec="milliseconds")


def normalize_line(raw: bytes) -> str:
    return raw.decode("ascii", errors="replace").strip()


def is_error(line: str) -> bool:
    return line.upper().startswith("ERR ") or line.upper() == "ERR"


class Relay485Client:
    def __init__(self, port: str, timeout: float = 1.0, json_output: bool = False) -> None:
        if serial is None:
            raise Relay485Error("pyserial is required; install tools/requirements-relay485.txt")
        self.port_name = port
        self.timeout = timeout
        self.json_output = json_output
        try:
            self.serial = serial.Serial(
                port=port,
                baudrate=BAUDRATE,
                bytesize=serial.EIGHTBITS,
                parity=serial.PARITY_NONE,
                stopbits=serial.STOPBITS_ONE,
                timeout=0.1,
                write_timeout=1.0,
            )
        except (serial.SerialException, OSError) as exc:
            raise Relay485Error(f"cannot open {port}: {exc}") from exc

    def close(self) -> None:
        self.serial.close()

    def emit(self, line: str) -> None:
        if self.json_output:
            print(json.dumps({"time": timestamp(), "port": self.port_name, "line": line}, ensure_ascii=False))
        else:
            print(line)

    def _read_lines(self, deadline: float, ignore: Iterable[str] = ()) -> list[str]:
        ignored = {item.strip().upper() for item in ignore}
        lines: list[str] = []
        while time.monotonic() < deadline:
            raw = self.serial.readline()
            if not raw:
                continue
            line = normalize_line(raw)
            if not line or line.upper() in ignored:
                continue
            lines.append(line)
            self.emit(line)
        return lines

    def _send(self, command: str) -> None:
        self.serial.reset_input_buffer()
        self.serial.write((command + "\r\n").encode("ascii"))
        self.serial.flush()

    def _wait_for(self, command: str, prefix: str, timeout: Optional[float] = None) -> str:
        deadline = time.monotonic() + (self.timeout if timeout is None else timeout)
        sent = command.upper()
        while time.monotonic() < deadline:
            raw = self.serial.readline()
            if not raw:
                continue
            line = normalize_line(raw)
            if not line or line.upper() == sent:
                continue
            self.emit(line)
            if is_error(line):
                raise Relay485Error(line)
            if line.upper().startswith(prefix.upper()):
                return line
        raise Relay485Error(f"timeout waiting for {prefix} after {command}")

    def command(self, command: str, prefix: str) -> str:
        self._send(command)
        return self._wait_for(command, prefix)

    def cycle(self, off_ms: int, wait: bool = True) -> list[str]:
        if not MIN_CYCLE_MS <= off_ms <= MAX_CYCLE_MS:
            raise Relay485Error(f"off_ms must be between {MIN_CYCLE_MS} and {MAX_CYCLE_MS}")
        command = f"CYCLE {off_ms}"
        self._send(command)
        ack = self._wait_for(command, "OK CYCLE")
        if not wait:
            return [ack]
        event_timeout = max(2.0, off_ms / 1000.0 + 2.0)
        event = self._wait_for(command, "EVENT CYCLE_DONE", timeout=event_timeout)
        return [ack, event]


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True, help="USB-RS485 serial port, for example COM5")
    parser.add_argument("--timeout", type=float, default=1.0, help="response timeout in seconds")
    parser.add_argument("--json", action="store_true", help="emit timestamped JSON lines")
    subparsers = parser.add_subparsers(dest="action", required=True)
    subparsers.add_parser("on", help="energize RELAY_OUT1")
    subparsers.add_parser("off", help="release RELAY_OUT1")
    subparsers.add_parser("status", help="read the commanded relay state")
    cycle = subparsers.add_parser("cycle", help="turn off, wait, and turn on")
    cycle.add_argument("off_ms", type=int)
    cycle.add_argument("--no-wait", action="store_true", help="return after the CYCLE acknowledgement")
    subparsers.add_parser("shell", help="interactive command shell")
    return parser


def run_shell(client: Relay485Client) -> None:
    print("Commands: ON, OFF, STATUS, CYCLE <100..600000>, EXIT")
    for raw in sys.stdin:
        command = raw.strip()
        if not command:
            continue
        if command.upper() in {"EXIT", "QUIT"}:
            return
        if command.upper().startswith("CYCLE "):
            try:
                off_ms = int(command.split(None, 1)[1])
                client.cycle(off_ms)
            except (ValueError, Relay485Error) as exc:
                raise Relay485Error(str(exc)) from exc
        else:
            client.command(command, "OK ")


def main(argv: Optional[list[str]] = None) -> int:
    args = build_parser().parse_args(argv)
    client: Optional[Relay485Client] = None
    try:
        client = Relay485Client(args.port, timeout=args.timeout, json_output=args.json)
        if args.action == "on":
            client.command("ON", "OK ON")
        elif args.action == "off":
            client.command("OFF", "OK OFF")
        elif args.action == "status":
            client.command("STATUS", "OK STATUS")
        elif args.action == "cycle":
            client.cycle(args.off_ms, wait=not args.no_wait)
        elif args.action == "shell":
            run_shell(client)
        return 0
    except (Relay485Error, KeyboardInterrupt) as exc:
        print(f"relay485: {exc}", file=sys.stderr)
        return 3
    finally:
        if client is not None:
            client.close()


if __name__ == "__main__":
    raise SystemExit(main())
