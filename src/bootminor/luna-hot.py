#!/usr/bin/env python3
"""luna-hot — send hot-swap patches to a running Luna process.

Wire protocol (see docs/HOTSWAP.md §4):

    header (12 bytes, little-endian):
      magic      u32 = 0x4C4E4831  ('LNH1')
      op         u8  = 1 (INSTALL)
      reserved   u8  = 0
      name_len   u16
      code_len   u32
    body:
      name_bytes  (name_len bytes, no NUL)
      code_bytes  (code_len bytes)

    reply: 4 bytes u32
      0          -> OK
      1          -> unknown function
      2          -> mmap failed
      3          -> mprotect / permission failure
      0xFF       -> protocol error

Only Python stdlib is used: socket, struct, sys, argparse, pathlib, os.
"""

import argparse
import os
import pathlib
import socket
import struct
import sys

MAGIC = 0x4C4E4831
OP_INSTALL = 1
HEADER_FMT = "<I B B H I"
HEADER_LEN = struct.calcsize(HEADER_FMT)  # 12
DEFAULT_SOCKET = "/tmp/luna.sock"

MAX_NAME_LEN = 0xFFFF          # name_len is u16
MAX_CODE_LEN = 16 * 1024 * 1024  # 16 MiB sanity ceiling

REPLY_NAMES = {
    0x00000000: "OK",
    0x00000001: "ERR unknown-function",
    0x00000002: "ERR mmap-failed",
    0x00000003: "ERR mprotect-failed",
    0x000000FF: "ERR protocol-error",
}


def _recv_exact(sock: socket.socket, n: int) -> bytes:
    """Read exactly n bytes or raise ConnectionError on short read."""
    buf = bytearray()
    while len(buf) < n:
        chunk = sock.recv(n - len(buf))
        if not chunk:
            raise ConnectionError(
                f"peer closed after {len(buf)}/{n} bytes"
            )
        buf.extend(chunk)
    return bytes(buf)


def cmd_send(args: argparse.Namespace) -> int:
    name = args.fn_name
    name_bytes = name.encode("utf-8")
    if len(name_bytes) == 0:
        print("luna-hot: fn_name must be non-empty", file=sys.stderr)
        return 1
    if len(name_bytes) > MAX_NAME_LEN:
        print(
            f"luna-hot: fn_name too long "
            f"({len(name_bytes)} > {MAX_NAME_LEN})",
            file=sys.stderr,
        )
        return 1

    code_path = pathlib.Path(args.code_file)
    try:
        code_bytes = code_path.read_bytes()
    except OSError as e:
        print(f"luna-hot: cannot read {code_path}: {e}", file=sys.stderr)
        return 1
    if len(code_bytes) == 0:
        print(f"luna-hot: {code_path} is empty", file=sys.stderr)
        return 1
    if len(code_bytes) > MAX_CODE_LEN:
        print(
            f"luna-hot: code blob too big "
            f"({len(code_bytes)} > {MAX_CODE_LEN})",
            file=sys.stderr,
        )
        return 1

    header = struct.pack(
        HEADER_FMT,
        MAGIC,
        OP_INSTALL,
        0,
        len(name_bytes),
        len(code_bytes),
    )

    sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    try:
        try:
            sock.connect(args.socket)
        except OSError as e:
            print(
                f"luna-hot: connect {args.socket} failed: {e}",
                file=sys.stderr,
            )
            return 1
        sock.sendall(header)
        sock.sendall(name_bytes)
        sock.sendall(code_bytes)

        reply_raw = _recv_exact(sock, 4)
    finally:
        sock.close()

    (reply_code,) = struct.unpack("<I", reply_raw)
    label = REPLY_NAMES.get(reply_code, f"ERR 0x{reply_code:08X}")
    print(label)
    return 0 if reply_code == 0 else 1


def cmd_listen(args: argparse.Namespace) -> int:
    path = args.socket
    # Clean up any stale socket file from a previous run.
    try:
        if os.path.exists(path):
            os.unlink(path)
    except OSError as e:
        print(
            f"luna-hot: cannot remove stale socket {path}: {e}",
            file=sys.stderr,
        )
        return 1

    srv = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    try:
        srv.bind(path)
    except OSError as e:
        print(f"luna-hot: bind {path} failed: {e}", file=sys.stderr)
        return 1
    srv.listen(1)
    print(f"luna-hot: listening on {path} (Ctrl-C to stop)")

    try:
        while True:
            conn, _ = srv.accept()
            with conn:
                try:
                    header = _recv_exact(conn, HEADER_LEN)
                except ConnectionError as e:
                    print(f"  short header: {e}")
                    continue
                magic, op, reserved, name_len, code_len = struct.unpack(
                    HEADER_FMT, header
                )
                if magic != MAGIC:
                    print(
                        f"  bad magic 0x{magic:08X} — sending protocol-error"
                    )
                    conn.sendall(struct.pack("<I", 0x000000FF))
                    continue
                try:
                    name = _recv_exact(conn, name_len).decode(
                        "utf-8", errors="replace"
                    )
                    code = _recv_exact(conn, code_len)
                except ConnectionError as e:
                    print(f"  short body: {e}")
                    continue
                print(
                    f"  frame: op={op} fn={name!r} bytes={len(code)} "
                    f"reserved={reserved}"
                )
                conn.sendall(struct.pack("<I", 0))  # reply OK
    except KeyboardInterrupt:
        print("\nluna-hot: stopped")
        return 0
    finally:
        srv.close()
        try:
            os.unlink(path)
        except OSError:
            pass


def build_parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(
        prog="luna-hot",
        description="Send hot-swap patches to a running Luna process.",
    )
    sub = p.add_subparsers(dest="cmd", required=True)

    p_send = sub.add_parser(
        "send",
        help="send an INSTALL frame with a pre-built code blob",
    )
    p_send.add_argument("fn_name", help="Luna function name to replace")
    p_send.add_argument(
        "code_file",
        help="raw binary file of x86-64 machine code for the new body",
    )
    p_send.add_argument(
        "--socket",
        default=DEFAULT_SOCKET,
        help=f"Unix socket path (default: {DEFAULT_SOCKET})",
    )
    p_send.set_defaults(func=cmd_send)

    p_listen = sub.add_parser(
        "listen-for-test",
        help="debug: listen on the socket and print received frames",
    )
    p_listen.add_argument(
        "--socket",
        default=DEFAULT_SOCKET,
        help=f"Unix socket path (default: {DEFAULT_SOCKET})",
    )
    p_listen.set_defaults(func=cmd_listen)

    return p


def main(argv=None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main())
