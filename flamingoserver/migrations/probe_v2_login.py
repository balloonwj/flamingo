#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
v2 协议登录 TCP 探针：照客户端 FrameCodec 格式发一个 cmd_login，收原始应答
字节并解码出来。用来确认服务端到底回了什么 —— 排查 flamingoclient
登录被识别为 LOGIN_FAILED 的根因。

帧头格式（与 client/server FrameCodec 一致，大端序，共 24 字节）：
   0  uint16  magic   = 0xF1A3
   2  uint8   version = 0x02
   3  uint8   flags   (0 = 未压缩, 1 = zlib 压缩)
   4  uint16  cmd
   6  uint16  reserved = 0
   8  uint32  seq
  12  uint32  bodylen   (线上包体长度)
  16  uint32  originlen (原始包体长度)
  20  uint32  crc32(前 20 字节)

cmd_login = 1003
"""
import socket
import struct
import zlib
import json
import hashlib
import sys

sys.stdout.reconfigure(encoding='utf-8')

HOST = "127.0.0.1"
PORT = 20000
ACCOUNT = "test99990"
PLAINTEXT = "test1234"

FRAME_MAGIC = 0xF1A3
PROTOCOL_V2 = 0x02
CMD_LOGIN = 1003


def crc32_ieee(data: bytes) -> int:
    """复刻客户端 FrameCodec::crc32：CRC-32/IEEE，初值 0xFFFFFFFF、最终取反。"""
    crc = 0xFFFFFFFF
    for b in data:
        crc ^= b
        for _ in range(8):
            if crc & 1:
                crc = (crc >> 1) ^ 0xEDB88320
            else:
                crc >>= 1
    return (~crc) & 0xFFFFFFFF


def encode_frame(cmd: int, seq: int, body_bytes: bytes, flags: int = 0,
                 originlen: int = None) -> bytes:
    bodylen = len(body_bytes)
    if originlen is None:
        originlen = bodylen
    hdr = bytearray(24)
    struct.pack_into(">H", hdr, 0, FRAME_MAGIC)
    hdr[2] = PROTOCOL_V2
    hdr[3] = flags
    struct.pack_into(">H", hdr, 4, cmd)
    struct.pack_into(">H", hdr, 6, 0)             # reserved
    struct.pack_into(">I", hdr, 8, seq)
    struct.pack_into(">I", hdr, 12, bodylen)
    struct.pack_into(">I", hdr, 16, originlen)
    struct.pack_into(">I", hdr, 20, crc32_ieee(bytes(hdr[:20])))
    return bytes(hdr) + body_bytes


def recv_exact(sock: socket.socket, n: int) -> bytes:
    """阻塞收够 n 字节；不够直接 RuntimeError。"""
    buf = b""
    while len(buf) < n:
        chunk = sock.recv(n - len(buf))
        if not chunk:
            raise RuntimeError(f"connection closed; got {len(buf)}/{n} bytes")
        buf += chunk
    return buf


def decode_frame(sock: socket.socket):
    """先收 24 字节帧头解析，再按 bodylen 续收包体；返回 (header_dict, body_bytes)。"""
    hdr = recv_exact(sock, 24)
    magic = struct.unpack_from(">H", hdr, 0)[0]
    version = hdr[2]
    flags = hdr[3]
    cmd = struct.unpack_from(">H", hdr, 4)[0]
    seq = struct.unpack_from(">I", hdr, 8)[0]
    bodylen = struct.unpack_from(">I", hdr, 12)[0]
    originlen = struct.unpack_from(">I", hdr, 16)[0]
    crc = struct.unpack_from(">I", hdr, 20)[0]
    body = recv_exact(sock, bodylen) if bodylen > 0 else b""
    if flags & 0x01:
        try:
            body_plain = zlib.decompress(body)
        except zlib.error as e:
            body_plain = body
            print(f"  [WARN] zlib decompress failed: {e}; showing raw body")
    else:
        body_plain = body
    return {
        "magic": f"0x{magic:04X}",
        "version": version,
        "flags": flags,
        "cmd": cmd,
        "seq": seq,
        "bodylen": bodylen,
        "originlen": originlen,
        "crc": f"0x{crc:08X}",
    }, body_plain


def main():
    sha = hashlib.sha256(PLAINTEXT.encode("utf-8")).hexdigest().lower()
    body_obj = {
        "clientversion": "3.2.1",
        "account": ACCOUNT,
        "password": sha,
        "clienttype": 1,
        "presence": 1,
        "deviceid": "PROBE-DEVICE-0001",
        "devicename": "probe",
    }
    body = json.dumps(body_obj, ensure_ascii=False, separators=(",", ":")).encode("utf-8")
    frame = encode_frame(CMD_LOGIN, seq=1, body_bytes=body)
    print(f"-> connect {HOST}:{PORT}")
    s = socket.create_connection((HOST, PORT), timeout=5)
    try:
        print(f"-> send cmd_login (frame {len(frame)} B, body {len(body)} B)")
        print(f"   body = {body.decode('utf-8')}")
        s.sendall(frame)

        print(f"-> waiting for response frame ...")
        hdr, body_bytes = decode_frame(s)
        print(f"<- header: {json.dumps(hdr, ensure_ascii=False)}")
        try:
            j = json.loads(body_bytes.decode("utf-8"))
            print(f"<- body (parsed JSON):")
            print(json.dumps(j, ensure_ascii=False, indent=2))
        except Exception as e:
            print(f"<- body (raw, JSON parse failed: {e}): {body_bytes!r}")
    finally:
        s.close()


if __name__ == "__main__":
    main()
