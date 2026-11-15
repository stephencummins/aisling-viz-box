#!/usr/bin/env python3
# A FAT16/FAT32 reader and writer that owes nothing to FatFs, so the checks have a second opinion on what the box
# leaves on a card and can put a file there the way a computer would. Short (8.3) names in the root only.
#
#   fat.py IMG cat NAME          the file's bytes to stdout (exit 1 if it isn't there)
#   fat.py IMG put NAME FILE     write FILE as NAME (over an old one, its clusters freed first)
#   fat.py IMG info              what kind of FAT, and its numbers
import struct
import sys


class Fat:
    def __init__(self, path):
        self.f = open(path, "r+b")
        self.base = 0
        b = self.rd(0, 1)
        if not self.is_vbr(b):   # a partition table: the first FAT partition
            for i in range(4):
                e = b[446 + 16 * i:462 + 16 * i]
                if e[4] in (0x01, 0x04, 0x06, 0x0B, 0x0C, 0x0E):
                    self.base = struct.unpack_from("<I", e, 8)[0]
                    break
            else:
                sys.exit("no FAT partition")
            b = self.rd(0, 1)
            if not self.is_vbr(b):
                sys.exit("no FAT volume in the partition")
        self.bps, self.spc, self.rsvd, self.nfat, self.nroot = struct.unpack_from("<HBHBH", b, 11)
        tot16, _, fatsz16 = struct.unpack_from("<HBH", b, 19)
        tot32 = struct.unpack_from("<I", b, 32)[0]
        fatsz32 = struct.unpack_from("<I", b, 36)[0]
        if self.bps != 512:
            sys.exit("not 512-byte sectors")
        self.fatsz = fatsz16 or fatsz32
        self.total = tot16 or tot32
        self.rootsecs = (self.nroot * 32 + 511) // 512
        self.data = self.rsvd + self.nfat * self.fatsz + self.rootsecs
        self.nclus = (self.total - self.data) // self.spc
        self.bits = 12 if self.nclus < 4085 else 16 if self.nclus < 65525 else 32
        if self.bits == 12:
            sys.exit("FAT12: not handled")
        self.rootclus = struct.unpack_from("<I", b, 44)[0] if self.bits == 32 else 0
        self.fsinfo = struct.unpack_from("<H", b, 48)[0] if self.bits == 32 else 0
        self.fat = bytearray(self.rd(self.rsvd, self.fatsz))

    @staticmethod
    def is_vbr(b):
        return b[510:512] == b"\x55\xAA" and b[0] in (0xEB, 0xE9) and struct.unpack_from("<H", b, 11)[0] == 512

    def rd(self, sec, n):
        self.f.seek((self.base + sec) * 512)
        return self.f.read(n * 512)

    def wr(self, sec, data):
        self.f.seek((self.base + sec) * 512)
        self.f.write(data)

    def get(self, c):
        if self.bits == 16:
            return struct.unpack_from("<H", self.fat, c * 2)[0]
        return struct.unpack_from("<I", self.fat, c * 4)[0] & 0x0FFFFFFF

    def put(self, c, v):
        if self.bits == 16:
            struct.pack_into("<H", self.fat, c * 2, v & 0xFFFF)
        else:
            old = struct.unpack_from("<I", self.fat, c * 4)[0] & 0xF0000000
            struct.pack_into("<I", self.fat, c * 4, old | (v & 0x0FFFFFFF))

    def end(self, c):
        return c < 2 or c >= (0xFFF8 if self.bits == 16 else 0x0FFFFFF8)

    def clus_sec(self, c):
        return self.data + (c - 2) * self.spc

    def chain(self, c):
        out = []
        while not self.end(c):
            if c >= self.nclus + 2 or c in out:
                sys.exit("a broken cluster chain")
            out.append(c)
            c = self.get(c)
        return out

    def root_secs(self):   # the root directory's sectors, in order
        if self.bits == 16:
            return list(range(self.rsvd + self.nfat * self.fatsz, self.data))
        out = []
        for c in self.chain(self.rootclus):
            out += range(self.clus_sec(c), self.clus_sec(c) + self.spc)
        return out

    @staticmethod
    def short(name):
        stem, _, ext = name.upper().partition(".")
        if not 0 < len(stem) <= 8 or len(ext) > 3:
            sys.exit("not an 8.3 name")
        return (stem.ljust(8) + ext.ljust(3)).encode("ascii")

    def find(self, name):   # (sector, offset, entry) of the file, and of the first free entry
        want, hit, free = self.short(name), None, None
        for s in self.root_secs():
            b = self.rd(s, 1)
            for o in range(0, 512, 32):
                e = b[o:o + 32]
                if e[0] == 0x00:
                    return hit, free or (s, o)
                if e[0] == 0xE5:
                    free = free or (s, o)
                elif e[11] & 0x0F != 0x0F and not e[11] & 0x18 and e[0:11] == want:   # not a long-name piece, a
                    hit = (s, o, e)                                                    # label or a folder
        return hit, free

    def cat(self, name):
        hit, _ = self.find(name)
        if not hit:
            return None
        e = hit[2]
        size = struct.unpack_from("<I", e, 28)[0]
        c = struct.unpack_from("<H", e, 26)[0] | (struct.unpack_from("<H", e, 20)[0] << 16 if self.bits == 32 else 0)
        data = b"".join(self.rd(self.clus_sec(k), self.spc) for k in self.chain(c))
        if len(data) < size:
            sys.exit("the file's chain is shorter than its size")
        return data[:size]

    def flush_fat(self):
        for i in range(self.nfat):
            self.wr(self.rsvd + i * self.fatsz, self.fat)
        if self.fsinfo:   # the free count and the next-free hint: unknown now, which every reader must take
            b = bytearray(self.rd(self.fsinfo, 1))
            if b[0:4] == b"RRaA" and b[484:488] == b"rrAa":
                struct.pack_into("<II", b, 488, 0xFFFFFFFF, 0xFFFFFFFF)
                self.wr(self.fsinfo, b)

    def write(self, name, data):
        hit, free = self.find(name)
        if hit:
            s, o, e = hit
            c = struct.unpack_from("<H", e, 26)[0] | (struct.unpack_from("<H", e, 20)[0] << 16 if self.bits == 32 else 0)
            for k in self.chain(c):
                self.put(k, 0)
        elif free:
            s, o = free
            e = bytearray(32)
            e[0:11] = self.short(name)
            e[11] = 0x20
            struct.pack_into("<HH", e, 22, 12 << 11, (46 << 9) | (11 << 5) | 15)   # 12:00, 15 Nov 2026
            struct.pack_into("<H", e, 16, (46 << 9) | (11 << 5) | 15)
        else:
            sys.exit("the root directory is full")
        need = (len(data) + self.spc * 512 - 1) // (self.spc * 512)
        got = [c for c in range(2, self.nclus + 2) if self.get(c) == 0][:need]
        if len(got) < need:
            sys.exit("the card is full")
        for i, c in enumerate(got):
            self.put(c, got[i + 1] if i + 1 < need else 0x0FFFFFFF)
            piece = data[i * self.spc * 512:(i + 1) * self.spc * 512]
            self.wr(self.clus_sec(c), piece.ljust(self.spc * 512, b"\0"))
        e = bytearray(e)
        first = got[0] if got else 0
        struct.pack_into("<H", e, 26, first & 0xFFFF)
        struct.pack_into("<H", e, 20, first >> 16 if self.bits == 32 else 0)
        struct.pack_into("<I", e, 28, len(data))
        b = bytearray(self.rd(s, 1))
        b[o:o + 32] = e
        self.wr(s, b)
        self.flush_fat()


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__ or "fat.py IMG cat|put|info ...")
    fat = Fat(sys.argv[1])
    what = sys.argv[2]
    if what == "cat" and len(sys.argv) == 4:
        data = fat.cat(sys.argv[3])
        if data is None:
            sys.exit(1)
        sys.stdout.buffer.write(data)
    elif what == "put" and len(sys.argv) == 5:
        fat.write(sys.argv[3], open(sys.argv[4], "rb").read())
    elif what == "info":
        free = sum(1 for c in range(2, fat.nclus + 2) if fat.get(c) == 0)
        print(f"FAT{fat.bits} at sector {fat.base}: {fat.nclus} clusters of {fat.spc * 512} bytes, {free} free")
    else:
        sys.exit("fat.py IMG cat NAME | put NAME FILE | info")


main()
