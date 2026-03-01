import struct
import threading
import time
from datetime import datetime
from typing import Optional, Callable

import serial


# ---------------------------------------------------------------------------
# CRC-16/CCITT (reflected, polynomial 0x1021, init 0x0000)
# Matches Zephyr crc16_ccitt which is CRC-16/KERMIT (input & output reflected)
# ---------------------------------------------------------------------------

def crc16_ccitt(data: bytes, init: int = 0x0000) -> int:
    crc = init
    for byte in data:
        crc ^= byte
        for _ in range(8):
            if crc & 0x0001:
                crc = (crc >> 1) ^ 0x8408
            else:
                crc >>= 1
    return crc


# ---------------------------------------------------------------------------
# COBS encode / decode  (matches firmware src/protocol/cobs.c)
# ---------------------------------------------------------------------------

def cobs_encode(data: bytes) -> bytes:
    output = bytearray()
    code_index = 0
    output.append(0)  # placeholder for first code byte
    code = 1
    for byte in data:
        if byte == 0:
            output[code_index] = code
            code = 1
            code_index = len(output)
            output.append(0)
        else:
            output.append(byte)
            code += 1
            if code == 0xFF:
                output[code_index] = code
                code = 1
                code_index = len(output)
                output.append(0)
    output[code_index] = code
    return bytes(output)


def cobs_decode(data: bytes) -> bytes:
    output = bytearray()
    idx = 0
    while idx < len(data):
        code = data[idx]
        if code == 0:
            return bytes()
        idx += 1
        for _ in range(1, code):
            if idx >= len(data):
                return bytes()
            output.append(data[idx])
            idx += 1
        if code < 0xFF and idx < len(data):
            output.append(0)
    return bytes(output)


# ---------------------------------------------------------------------------
# Packet structure helpers
# ---------------------------------------------------------------------------

HEADER_BYTE  = 0xAA
PACKET_HDR   = struct.Struct("<BBHB")  # header, type, id(LE), length

CMD_RTC_SET_TIME   = 0x01
CMD_RTC_GET_TIME   = 0x02
CMD_SET_BASE_FREQ  = 0x03
CMD_GET_BASE_FREQ  = 0x04
CMD_RESET          = 0xFD
RESP_ACK           = 0xFF
RESP_NACK          = 0xFE


def build_packet(cmd_id: int, payload: bytes, pkt_id: int) -> bytes:
    """Build a raw packet (before COBS)."""
    length = len(payload)
    hdr = PACKET_HDR.pack(HEADER_BYTE, cmd_id, pkt_id, length)
    body = hdr + payload
    crc = crc16_ccitt(body)
    return body + struct.pack("<H", crc)


def frame_packet(cmd_id: int, payload: bytes, pkt_id: int) -> bytes:
    """Build a COBS-framed packet ready for UART transmission."""
    raw = build_packet(cmd_id, payload, pkt_id)
    return cobs_encode(raw) + b"\x00"


def parse_packet(raw: bytes) -> Optional[dict]:
    """Parse a decoded (post-COBS) packet. Returns dict or None on error."""
    if len(raw) < PACKET_HDR.size + 2:
        return None
    header, ptype, pid, length = PACKET_HDR.unpack_from(raw)
    if header != HEADER_BYTE:
        return None
    expected_len = PACKET_HDR.size + length + 2
    if len(raw) < expected_len:
        return None
    payload = raw[PACKET_HDR.size : PACKET_HDR.size + length]
    crc_calc = crc16_ccitt(raw[: PACKET_HDR.size + length])
    crc_recv = struct.unpack_from("<H", raw, PACKET_HDR.size + length)[0]
    if crc_calc != crc_recv:
        return None
    return {"type": ptype, "id": pid, "payload": payload}


# ---------------------------------------------------------------------------
# MiniHF  – high level serial interface
# ---------------------------------------------------------------------------

class MiniHF:
    """Python interface to the miniHF firmware over serial/UART."""

    def __init__(self, port: str, baudrate: int = 115200, timeout: float = 2.0, debug: bool = False):
        self._ser = serial.Serial(port, baudrate, timeout=timeout)
        self._timeout = timeout
        self._next_id = 1
        self._lock = threading.Lock()
        self._rx_buf = bytearray()
        self._debug = debug

    # -- low level ----------------------------------------------------------

    def _alloc_id(self) -> int:
        pid = self._next_id
        self._next_id = (self._next_id + 1) & 0xFFFF
        return pid

    def _send(self, cmd_id: int, payload: bytes = b"", pkt_id: Optional[int] = None) -> int:
        if pkt_id is None:
            pkt_id = self._alloc_id()
        frame = frame_packet(cmd_id, payload, pkt_id)
        if self._debug:
            print(f"[TX] hex: {frame.hex(' ')}")
            print(f"[TX] str: {frame!r}")
        with self._lock:
            self._ser.write(frame)
        return pkt_id

    def _recv(self, expected_id: Optional[int] = None, timeout: Optional[float] = None) -> Optional[dict]:
        """Read COBS frames from serial until a packet matching *expected_id* arrives."""
        deadline = time.monotonic() + (timeout or self._timeout)
        while time.monotonic() < deadline:
            remaining = max(0.01, deadline - time.monotonic())
            self._ser.timeout = remaining
            chunk = self._ser.read(self._ser.in_waiting or 1)
            if not chunk:
                continue
            if self._debug:
                print(f"[RX] hex: {chunk.hex(' ')}")
                print(f"[RX] str: {chunk!r}")
            self._rx_buf.extend(chunk)
            # process any complete frames (delimited by 0x00)
            while b"\x00" in self._rx_buf:
                idx = self._rx_buf.index(b"\x00")
                frame_data = bytes(self._rx_buf[:idx])
                del self._rx_buf[: idx + 1]
                if not frame_data:
                    continue
                decoded = cobs_decode(frame_data)
                if not decoded:
                    continue
                pkt = parse_packet(decoded)
                if pkt is None:
                    continue
                if expected_id is not None and pkt["id"] != expected_id:
                    continue
                return pkt
        return None

    def _transact(self, cmd_id: int, payload: bytes = b"", timeout: Optional[float] = None) -> dict:
        """Send a command and wait for its response. Raises on timeout/NACK."""
        pid = self._send(cmd_id, payload)
        resp = self._recv(expected_id=pid, timeout=timeout)
        if resp is None:
            raise TimeoutError(f"No response for cmd 0x{cmd_id:02X} id {pid}")
        if resp["type"] == RESP_NACK:
            raise RuntimeError(f"NACK received for cmd 0x{cmd_id:02X} id {pid}")
        return resp

    # -- public API ---------------------------------------------------------

    def set_rtc_time(self, dt: Optional[datetime] = None) -> None:
        """Set the RTC time. Defaults to *now* if *dt* is None."""
        if dt is None:
            dt = datetime.now()
        payload = struct.pack("<H", dt.year) + struct.pack(
            "BBBBB", dt.month, dt.day, dt.hour, dt.minute, dt.second
        )
        resp = self._transact(CMD_RTC_SET_TIME, payload)
        if resp["type"] != RESP_ACK:
            raise RuntimeError("Unexpected response for set_rtc_time")

    def get_rtc_time(self) -> datetime:
        """Read the current RTC time."""
        resp = self._transact(CMD_RTC_GET_TIME)
        p = resp["payload"]
        year = struct.unpack_from("<H", p, 0)[0]
        mon, day, hour, minute, sec = struct.unpack_from("BBBBB", p, 2)
        return datetime(year, mon, day, hour, minute, sec)

    def set_base_freq(self, freq_hz: float) -> None:
        """Set the base frequency. *freq_hz* is in Hz (stored as freq×100 internally)."""
        freq_int = int(round(freq_hz * 100))
        payload = struct.pack("<Q", freq_int)
        resp = self._transact(CMD_SET_BASE_FREQ, payload)
        if resp["type"] != RESP_ACK:
            raise RuntimeError("Unexpected response for set_base_freq")

    def get_base_freq(self) -> float:
        """Get the base frequency in Hz."""
        resp = self._transact(CMD_GET_BASE_FREQ)
        freq_int = struct.unpack_from("<Q", resp["payload"], 0)[0]
        return freq_int / 100.0

    def reset(self) -> None:
        """Trigger a cold reboot on the device (no response expected)."""
        self._send(CMD_RESET)

    def close(self) -> None:
        """Close the serial port."""
        self._ser.close()

    def __enter__(self):
        return self

    def __exit__(self, *args):
        self.close()


# ---------------------------------------------------------------------------
# CLI demo
# ---------------------------------------------------------------------------

def main():
    import argparse

    parser = argparse.ArgumentParser(description="miniHF command-line interface")
    parser.add_argument("-p", "--port", default="/dev/cu.usbserial-1430", help="Serial port")
    parser.add_argument("-b", "--baud", type=int, default=115200, help="Baud rate")
    parser.add_argument("-d", "--debug", action="store_true", help="Print all UART traffic as hex and string")
    sub = parser.add_subparsers(dest="command")

    sub.add_parser("get-time", help="Read RTC time")
    sp = sub.add_parser("set-time", help="Set RTC time (defaults to now)")
    sp.add_argument("datetime", nargs="?", default=None, help="ISO datetime, e.g. 2026-03-01T12:00:00")

    sp = sub.add_parser("get-freq", help="Read base frequency")
    sp = sub.add_parser("set-freq", help="Set base frequency (Hz)")
    sp.add_argument("freq", type=float, help="Frequency in Hz")

    sub.add_parser("reset", help="Reboot the device")

    args = parser.parse_args()

    if not args.command:
        parser.print_help()
        return

    with MiniHF(args.port, args.baud, debug=args.debug) as radio:
        if args.command == "get-time":
            print(radio.get_rtc_time().isoformat())
        elif args.command == "set-time":
            dt = datetime.fromisoformat(args.datetime) if args.datetime else None
            radio.set_rtc_time(dt)
            print("OK")
        elif args.command == "get-freq":
            print(f"{radio.get_base_freq()} Hz")
        elif args.command == "set-freq":
            radio.set_base_freq(args.freq)
            print("OK")
        elif args.command == "reset":
            radio.reset()
            print("Resetting...")


if __name__ == "__main__":
    main()