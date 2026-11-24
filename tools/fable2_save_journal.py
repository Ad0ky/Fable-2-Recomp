#!/usr/bin/env python3
"""Replays fable2-nx's save journal (save_journal.bin, written next to the NRO by xboxkrnl_io.cpp's SaveJournal).

Every read and write the game made on a save file is in the journal with its bytes. This rebuilds each file step by
step, and says:
  * reads that returned bytes different from what the file held at that moment (a stale or wrong read);
  * writes that replaced bytes an earlier write had put there with different ones (the game rewriting a sector);
  * for mainsave.bin, the first write after which its zlib blocks stop unpacking, and the bytes involved.

Usage: python3 tools/fable2_save_journal.py save_journal.bin [--file mainsave.bin] [--dump OUT_DIR]
"""
import argparse
import os
import struct
import sys
import zlib

HEADER = struct.Struct("<4scIQqIIIHI")  # magic, op, handle, us, offset, length, status, done, path len, data len


def records(path):
    data = open(path, "rb").read()
    pos = 0
    while pos + HEADER.size <= len(data):
        magic, op, handle, us, offset, length, status, done, plen, dlen = HEADER.unpack_from(data, pos)
        if magic != b"F2SJ":
            print(f"journal: bad record at byte {pos}, stopping", file=sys.stderr)
            return
        pos += HEADER.size
        name = data[pos:pos + plen].decode("utf-8", "replace")
        pos += plen
        payload = data[pos:pos + dlen]
        pos += dlen
        yield dict(op=op.decode(), handle=handle, us=us, offset=offset, length=length, status=status, done=done,
                   name=name, data=payload)


def base_name(name):
    return name.replace("/", "\\").split("\\")[-1].split(":")[-1].lower()


def zlib_blocks(buf):
    """(ok, blocks, message) for Fable 2's mainsave layout: [u32 BE packed size][u32 BE unpacked size][deflate...]."""
    off = 0
    n = 0
    obj = None
    while off < len(buf):
        if off + 8 > len(buf):
            return False, n, f"{len(buf) - off} trailing bytes at {off}"
        clen, ulen = struct.unpack_from(">II", buf, off)
        payload = buf[off + 8:off + 8 + clen]
        if len(payload) < clen:
            return False, n, f"block {n} at {off} is cut: {len(payload)} of {clen} bytes"
        if obj is None or payload[:2] == b"\x78\xda":
            obj = zlib.decompressobj()
        try:
            out = obj.decompress(payload)
        except zlib.error as e:
            return False, n, f"block {n} at {off} (packed {clen}, unpacked {ulen}): {e}"
        if len(out) != ulen:
            return False, n, f"block {n} at {off}: unpacked {len(out)} of {ulen}"
        off += 8 + clen
        n += 1
    return True, n, f"{n} blocks unpack"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("journal")
    ap.add_argument("--file", default="", help="only this save file (for example mainsave.bin)")
    ap.add_argument("--dump", default="", help="write each rebuilt file at every session's end into this folder")
    args = ap.parse_args()

    files = {}        # name -> bytearray, the file as the writes left it
    positions = {}    # handle -> position, for offset -1
    sessions = {}     # name -> session number (a write at offset 0 after a gap starts a new save)
    last_ok = {}
    for i, r in enumerate(records(args.journal)):
        name = base_name(r["name"])
        if args.file and name != args.file.lower():
            continue
        buf = files.setdefault(name, bytearray())
        at = r["offset"] if r["offset"] >= 0 else positions.get(r["handle"], 0)
        tag = f"#{i} {r['op']} {name} h{r['handle']:#x} {r['length']} @ {at}"
        if r["op"] == "W":
            if at == 0 and r["data"] and len(buf) and name == "mainsave.bin" and r["data"][:4] != buf[:4]:
                if args.dump:
                    os.makedirs(args.dump, exist_ok=True)
                    s = sessions.get(name, 0)
                    open(os.path.join(args.dump, f"{name}.{s}"), "wb").write(buf)
                sessions[name] = sessions.get(name, 0) + 1
                print(f"--- {name}: a new save starts at record #{i}")
            data = r["data"][:r["done"]] if r["done"] else r["data"]
            end = at + len(data)
            old = bytes(buf[at:min(end, len(buf))])
            if len(buf) < end:
                buf.extend(b"\0" * (end - len(buf)))
            new_part = data[:len(old)]
            if old and old != new_part:
                first = next(k for k in range(len(old)) if old[k] != new_part[k])
                changed = sum(1 for k in range(len(old)) if old[k] != new_part[k])
                print(f"{tag}: rewrites {changed} of {len(old)} bytes already written, the first at {at + first}: "
                      f"was {old[first:first + 12].hex()} now {new_part[first:first + 12].hex()}")
            buf[at:end] = data
            positions[r["handle"]] = end
            if name == "mainsave.bin":
                ok, n, msg = zlib_blocks(bytes(buf))
                if last_ok.get(name, True) and not ok and "is cut" not in msg and "trailing" not in msg:
                    print(f"{tag}: AFTER THIS WRITE mainsave.bin stops unpacking: {msg}")
                last_ok[name] = ok or "is cut" in msg or "trailing" in msg
        else:
            got = r["data"]
            want = bytes(buf[at:at + len(got)])
            positions[r["handle"]] = at + len(got)
            if got != want:
                first = next((k for k in range(min(len(got), len(want))) if got[k] != want[k]), min(len(got), len(want)))
                print(f"{tag}: READ DIFFERS from the file as written, first at {at + first}: "
                      f"read {got[first:first + 12].hex()} file {want[first:first + 12].hex()} "
                      f"(status {r['status']:#x}, {r['done']} read)")
            elif r["status"] not in (0, 0x103):
                print(f"{tag}: status {r['status']:#x}, {r['done']} read")
    for name, buf in files.items():
        line = f"{name}: {len(buf)} bytes at the end"
        if name == "mainsave.bin":
            line += "; " + zlib_blocks(bytes(buf))[2]
        print(line)
        if args.dump:
            os.makedirs(args.dump, exist_ok=True)
            open(os.path.join(args.dump, f"{name}.{sessions.get(name, 0)}"), "wb").write(buf)


if __name__ == "__main__":
    main()
