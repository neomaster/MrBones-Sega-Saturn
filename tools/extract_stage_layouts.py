"""Read USA stage object lists directly from the two CUE/BINs and their link maps."""
import argparse
import csv
import hashlib
import io
import re
import struct
import zipfile
from pathlib import Path

from make_manifest import Disc

MAP_NAMES = {"BAT1": "BATWOOD1", "BAT2": "BATWOOD2", "BAT3": "BATWOOD3",
             "ICE1": "ICELAKE1", "ICE2": "ICELAKE2", "GENRM": "GENROOM",
             "BIGBNS": "BIGBONES", "LAFF": "LAFFRIOT"}


def extract(disc1, disc2, manifest, output, events=None):
    event_rows = []
    files = {}
    for line in manifest.read_text().splitlines():
        fields = line.split()
        if fields and fields[0] == "file":
            _, disc, _, lba, size, digest, path = fields
            files[int(disc), path] = int(lba), int(size), digest
    discs = {1: Disc(str(disc1)), 2: Disc(str(disc2))}

    def resource(number, path):
        lba, size, expected = files[number, path]
        data = discs[number].read(lba, size)
        if hashlib.sha256(data).hexdigest() != expected:
            raise ValueError(f"SHA-256 mismatch on disc {number} {path}")
        return data

    try:
        maps = zipfile.ZipFile(io.BytesIO(resource(1, "/30MAP/MAPFILES.ZIP")))
        with maps, output.open("w", newline="", encoding="utf-8") as target:
            writer = csv.writer(target)
            writer.writerow(["stage", "disc", "03CODE_sha256", "entry_va", "next_va",
                             "constructor_va", "map_symbol", "symbol_delta",
                             "flags", "x_16_16", "y_16_16", "field20", "field24", "linked_tail_hex"])
            stages = sorted({p.split("/")[1] for _, p in files if p.endswith("/03CODE")
                             and 4 <= int(p[1:3]) <= 27 and "TROLL" not in p})
            for stage in stages:
                path = "/" + stage + "/03CODE"
                disc = max((1, 2), key=lambda d: files[d, path][1])
                data = resource(disc, path)
                digest = files[disc, path][2]
                folder = MAP_NAMES.get(stage[2:], stage[2:])
                symbols = maps.read(folder + "/A.MAP").decode("latin1")
                base = int(re.search(r"^\.entry\s+(\w+)", symbols, re.M)[1], 16)
                labels = [(int(a, 16), n) for a, n in re.findall(
                    r"^\s*([0-9a-f]{8})\s+(_\S+)\s*$", symbols, re.M)]
                by_name = {n: a for a, n in labels}
                if events and "_TriggerList" in by_name:
                    address = by_name["_TriggerList"]
                    end = min(a for a, _ in labels if a > address)
                    if not base <= address < end <= base + len(data) or (end - address) % 8:
                        raise ValueError(f"{stage} invalid trigger table bounds")
                    records = list(struct.iter_unpack(">II", data[address-base:end-base]))
                    if records[-1] not in ((0, 0), (0xffff, 0)):
                        raise ValueError(f"{stage} missing trigger terminator")
                    previous = -1
                    for i, (frame, mask) in enumerate(records[:-1]):
                        if frame < previous:
                            raise ValueError(f"{stage} trigger sequence goes backwards at {i}")
                        previous = frame
                        event_rows.append([stage, disc, digest, hex(address + i*8),
                                           "_TriggerList", i, frame, hex(mask), "", "", ""])
                if events and "_TableHole" in by_name and "_TableYFall" in by_name:
                    address, end = by_name["_TableHole"], by_name["_TableYFall"]
                    if not base <= address < end <= base + len(data) or (end-address) % 12:
                        raise ValueError(f"{stage} invalid hole table bounds")
                    records = list(struct.iter_unpack(">iii", data[address-base:end-base]))
                    if records[-1][2] != -2:
                        raise ValueError(f"{stage} missing hole table terminator")
                    for i, (x, y, kind) in enumerate(records[:-1]):
                        if kind not in (0, 1, 2):
                            raise ValueError(f"{stage} unknown hole type {kind}")
                        event_rows.append([stage, disc, digest, hex(address+i*12),
                                           "_TableHole", i, "", "", x, y, kind])
                    address, end = by_name["_TableYFall"], by_name["_ColTable"]
                    if not base <= address < end <= base + len(data) or (end-address) % 8:
                        raise ValueError(f"{stage} invalid fall profile bounds")
                    previous = None
                    for i, (x, y) in enumerate(struct.iter_unpack(">ii", data[address-base:end-base])):
                        if previous is not None and x >= previous:
                            raise ValueError(f"{stage} nonmonotonic fall profile at {i}")
                        previous = x
                        event_rows.append([stage, disc, digest, hex(address+i*8),
                                           "_TableYFall", i, "", "", x, y, ""])
                entries = sorted(a for a, n in labels if re.fullmatch(r"__LayoutEntry_\d+_", n))
                constructors = {a: n for a, n in labels if n.startswith("_Create")}
                best = []
                for shift in sorted(range(-512, 513, 4), key=abs):
                    # Walk only the primary linked list. VILLAGE also has 16-byte records
                    # with numeric labels after its NULL terminator.
                    addresses = {a + shift for a in entries}
                    addr = entries[0] + shift if entries else 0
                    rows = []
                    while addr in addresses and 0 <= addr - base <= len(data) - 28:
                        addresses.remove(addr)
                        link, ctor, flags, x, y, field20, field24 = struct.unpack_from(
                            ">IIIiiii", data, addr - base)
                        if not base <= ctor < base + len(data) or ctor % 2:
                            break
                        rows.append((addr, link, ctor, flags, x, y, field20, field24))
                        if not link:
                            if len(rows) > len(best) and (len(rows) > 1 or ctor in constructors):
                                best = rows
                            break
                        addr = link
                # The first object has its own symbol, preceding __LayoutEntry_0001_.
                # Some retail overlays relocate the list head separately from its children.
                first_symbol = by_name.get("_FirstNumenStruct")
                if first_symbol is not None:
                    first_link = best[0][0] if best else 0
                    for displacement in sorted(range(-512, 513, 4), key=abs):
                        addr = first_symbol + displacement
                        offset = addr - base
                        if offset < 0 or offset + 28 > len(data) or (best and addr >= first_link):
                            continue
                        row = struct.unpack_from(">IIIiiii", data, offset)
                        if row[0] != first_link or not (row[1] in constructors or row[1]-12 in constructors):
                            continue
                        if not best and displacement:
                            continue
                        best.insert(0, (addr, *row))
                        break
                if not best:
                    writer.writerow([stage, disc, digest] + [""] * 11)
                for addr, link, ctor, flags, x, y, field20, field24 in best:
                    match = min(constructors, key=lambda a: abs(a - ctor), default=None)
                    delta = ctor - match if match is not None else None
                    # A relocated symbol is a candidate. Preserve its raw VA and delta.
                    name = constructors[match] if delta in (0, 12) else ""
                    # Bytes bounded by the next on-disc address. A terminal record has no extent.
                    tail = data[addr - base + 28:link - base].hex() if link else ""
                    writer.writerow([stage, disc, digest, hex(addr), hex(link), hex(ctor),
                                     name, delta if name else "", flags, x, y, field20, field24, tail])
                print(f"{stage} disc={disc} records={len(best)}")
        if events:
            with events.open("w", newline="", encoding="utf-8") as target:
                writer = csv.writer(target)
                writer.writerow(["stage", "disc", "03CODE_sha256", "entry_va", "symbol",
                                 "index", "pan_frame", "mask", "x_16_16", "y_16_16", "kind"])
                writer.writerows(event_rows)
    finally:
        for disc in discs.values():
            disc.f.close()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    for arg in ("disc1", "disc2", "manifest", "output"):
        parser.add_argument(arg, type=Path)
    parser.add_argument("--events", type=Path, help="export PAN trigger and hole tables")
    extract(**vars(parser.parse_args()))

