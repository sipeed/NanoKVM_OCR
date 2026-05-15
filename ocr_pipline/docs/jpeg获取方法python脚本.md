#!/usr/bin/env python3
"""
JENC snapshot test script

Sends a snapshot command over Unix socket /run/kvm/vin_ctrl.sock,
receives a JSON header + raw JPEG bytes and saves to file.

Usage:
  python3 snapshot_test.py [options]

Options:
  -o OUTPUT       Output filename (default: snapshot.jpg)
  -q QUALITY      JPEG quality 1~100 (default: 85)
  -t TIMEOUT      Timeout in milliseconds (default: 1000)
  -s SOCK         Unix socket path (default: /run/kvm/vin_ctrl.sock)
  --loop N        Take N snapshots in a row (default: 1)
  --interval MS   Interval between snapshots in ms (default: 500)
  --crop X Y W H  Crop region; 0 0 0 0 means full frame
"""

import argparse
import json
import os
import socket
import struct
import sys
import time


SOCK_PATH = "/run/kvm/vin_ctrl.sock"


def connect(sock_path: str) -> socket.socket:
    s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    s.settimeout(5.0)
    s.connect(sock_path)
    return s


def recv_line(s: socket.socket) -> str:
    """Read bytes one at a time until newline and return the line as a string."""
    buf = b""
    while True:
        c = s.recv(1)
        if not c:
            raise ConnectionError("connection closed while reading header")
        if c == b"\n":
            return buf.decode("utf-8")
        buf += c


def recv_exact(s: socket.socket, n: int) -> bytes:
    """Read exactly n bytes from the socket."""
    buf = b""
    while len(buf) < n:
        chunk = s.recv(n - len(buf))
        if not chunk:
            raise ConnectionError(f"connection closed: expected {n} bytes, got {len(buf)}")
        buf += chunk
    return buf


def do_snapshot(sock_path: str, quality: int, timeout_ms: int,
                x: int = 0, y: int = 0, w: int = 0, h: int = 0) -> tuple[bool, dict, bytes]:
    """
    Returns (ok, header_dict, jpeg_bytes).
    jpeg_bytes is empty on failure.
    x/y/w/h = 0 means full frame; non-zero enables JENC crop.
    """
    req = json.dumps({
        "cmd": "snapshot",
        "quality": quality,
        "timeout_ms": timeout_ms,
        "x": x,
        "y": y,
        "w": w,
        "h": h,
    })

    s = connect(sock_path)
    try:
        s.sendall(req.encode("utf-8"))
        hdr_str = recv_line(s)
        hdr = json.loads(hdr_str)

        data = b""
        if hdr.get("ok") and hdr.get("size", 0) > 0:
            data = recv_exact(s, hdr["size"])
    finally:
        s.close()

    return hdr.get("ok", False), hdr, data


def main():
    parser = argparse.ArgumentParser(description="nanoagent_vin JENC snapshot test")
    parser.add_argument("-o", "--output", default="snapshot.jpg", help="Output filename")
    parser.add_argument("-q", "--quality", type=int, default=85, help="JPEG quality (1~100)")
    parser.add_argument("-t", "--timeout", type=int, default=1000, help="Timeout in ms")
    parser.add_argument("-s", "--sock", default=SOCK_PATH, help="Unix socket path")
    parser.add_argument("--loop", type=int, default=1, help="Number of snapshots to take")
    parser.add_argument("--interval", type=int, default=500, help="Interval between snapshots in ms")
    parser.add_argument("--crop", type=int, nargs=4, metavar=("X", "Y", "W", "H"),
                        default=[0, 0, 0, 0], help="Crop region X Y W H (default: full frame)")
    args = parser.parse_args()

    cx, cy, cw, ch = args.crop
    crop_info = f" crop=({cx},{cy},{cw},{ch})" if any(args.crop) else ""

    if not os.path.exists(args.sock):
        print(f"[ERROR] socket not found: {args.sock}", file=sys.stderr)
        sys.exit(1)

    for i in range(args.loop):
        if args.loop > 1:
            output = f"{os.path.splitext(args.output)[0]}_{i:04d}.jpg"
        else:
            output = args.output

        t0 = time.monotonic()
        ok, hdr, data = do_snapshot(args.sock, args.quality, args.timeout,
                                    cx, cy, cw, ch)
        elapsed = (time.monotonic() - t0) * 1000

        if ok:
            with open(output, "wb") as f:
                f.write(data)
            print(f"[{i+1}/{args.loop}] OK  {hdr['width']}x{hdr['height']}{crop_info} "
                  f"{len(data)} bytes -> {output}  ({elapsed:.1f} ms)")
        else:
            print(f"[{i+1}/{args.loop}] FAIL  ret={hdr.get('ret_code')}  "
                  f"msg={hdr.get('message')}  ({elapsed:.1f} ms)",
                  file=sys.stderr)

        if i < args.loop - 1:
            time.sleep(args.interval / 1000.0)


if __name__ == "__main__":
    main()