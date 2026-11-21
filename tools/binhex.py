#!/usr/bin/env python3
"""Write a BinHex 4.0 (.hqx) file from a Mac file's data fork, resource fork and type/creator.

BinHex survives email, the web and any file system, and Afterglow imports it directly,
so it is the way to hand the module to someone else. (Python dropped its binhex module
in 3.11; the format is small enough to write here.)
"""
import binascii
import os
import pathlib
import sys

ALPHABET = b"!\"#$%&'()*+,-012345689@ABCDEFGHIJKLMNPQRSTUVXYZ[`abcdefhijklmpqr"


def with_crc(block):
    """Each fork is followed by its CRC-CCITT (XMODEM), as binascii.crc_hqx computes it."""
    return block + binascii.crc_hqx(block, 0).to_bytes(2, "big")


def rle90(data):
    """BinHex run-length: 0x90 is the marker. A literal 0x90 is 0x90 0x00; a run of
    one byte n times (4..255) is the byte, 0x90, n. Runs of 0x90 itself stay literal."""
    out, i = bytearray(), 0
    while i < len(data):
        b, n = data[i], 1
        while i + n < len(data) and data[i + n] == b and n < 255:
            n += 1
        if b == 0x90:
            out += b"\x90\x00"
            i += 1
        elif n >= 4:
            out += bytes([b, 0x90, n])
            i += n
        else:
            out.append(b)
            i += 1
    return bytes(out)


def encode6(data):
    out, bits, acc = bytearray(), 0, 0
    for b in data:
        acc = (acc << 8) | b
        bits += 8
        while bits >= 6:
            bits -= 6
            out.append(ALPHABET[(acc >> bits) & 0x3F])
    if bits:
        out.append(ALPHABET[(acc << (6 - bits)) & 0x3F])
    return bytes(out)


def main():
    src, dst = pathlib.Path(sys.argv[1]), pathlib.Path(sys.argv[2])
    data = src.read_bytes()
    rsrc = (src / "..namedfork" / "rsrc").read_bytes()
    finfo = os.getxattr(src, "com.apple.FinderInfo") if hasattr(os, "getxattr") else None
    if finfo is None:
        import subprocess
        hexstr = subprocess.run(["xattr", "-px", "com.apple.FinderInfo", str(src)],
                                capture_output=True, text=True, check=True).stdout
        finfo = bytes.fromhex(hexstr.replace(" ", "").replace("\n", ""))
    ftype, creator, flags = finfo[0:4], finfo[4:8], finfo[8:10]
    name = src.name.encode("mac_roman")
    header = bytes([len(name)]) + name + b"\0" + ftype + creator + flags \
        + len(data).to_bytes(4, "big") + len(rsrc).to_bytes(4, "big")
    stream = with_crc(header) + with_crc(data) + with_crc(rsrc)
    body = encode6(rle90(stream))
    lines = [body[i:i + 64] for i in range(0, len(body), 64)]
    text = b"(This file must be converted with BinHex 4.0)\r\r:" + b"\r".join(lines) + b":\r"
    dst.write_bytes(text)
    print(f"wrote {dst} ({len(rsrc)} byte resource fork)")


if __name__ == "__main__":
    main()
