# Test 19: the lines swap_check's "vectors" prints, from fm1-aisling's tools/fm1_install.py (the file swap.c is a port
# of) for the same inputs. run.sh compares the two.
#   python3 -I vec.py FM1_AISLING_FOLDER [PACKAGE...]
import re
import sys

sys.path.insert(0, sys.argv[1] + "/tools")
import fm1_install as f

IDS = ["FM-1_900", "ota-FM-1_900", "OTA-FM-1_1", "Ota-FM-1_77", "FM-1_353010401", "nounderscore", "FM-1_", "_12",
       "FM-1_12a", "FM_1_12", "ABCDEFGHIJKLMNOPQRSTU_12345", "ABCDEFGHIJKLMNOPQR_12345", "ABCDEFGHIJKLMNOPQ_12345",
       "otaFM-1_3", "X_0", ""]
PRODUCTS = ["FM-1_900", "FM-1_353010401", "FM-1_391010100", "ABCDEFGHIJKLMNOP_123", "FM-1", "FM-1_x", "_5", "A_1", ""]
MODELS = [("FM-1_900", "ota-FM-1_900"), ("FM-1_900", "FM-1_1"), ("FM-1_900", "FM-2_900"), ("ota-FM-1_900", "ota-FM-1_2"),
          ("FM-1_900", "FM-10_900"), ("ota-X_1", "X_2"), ("ota-_1", "_1"), ("FM-1_1", "fm-1_1")]


def frame(body):
    return bytearray(b"\xF0" + f.pack7(bytes(body)) + b"\xF7")


def make_pkg(n, product, seed):   # simdev.h's
    x, raw = seed, bytearray(n)
    for i in range(n):
        x = (x * 1103515245 + 12345) & 0x7FFFFFFF
        raw[i] = (x >> 16) & 0xFF
        if raw[i] == ord("F"):
            raw[i] = ord("G")
    for i in range(20):
        raw[i * 48 + 47] = (ord(product[i]) + i + 1) & 0xFF if i < len(product) else 0x7D
    return raw


def fnv(b):
    h = 0xCBF29CE484222325
    for c in b:
        h = ((h ^ c) * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
    return h


for k, n in enumerate([0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 13, 14, 15, 16, 34, 100, 511, 512]):
    print(f"pack7 {n} {f.pack7(bytes((i * 37 + 11 + k) & 0xFF for i in range(n))).hex()}")
for a, addr in enumerate([0, 1, 0x3FF, 0x94D00, 0xE0000000, 0xF0000000, 0xFFFFFFFF]):
    for k, n in enumerate([0, 1, 8, 47, 64, 256, 511, 512]):
        fl = (a * 16 + k * 31) & 0xFF
        data = bytes((i * 151 + a * 7 + k) & 0xFF for i in range(n))
        print(f"resp {addr:08x} {n} {fl:02x} {f.response(addr, data, fl).hex()}")
for k in range(40):
    b = bytearray([0x00, 0x59, 0x30, 8, 0, 0] + [0] * 10)
    n = 15
    for i in range(6, 14):
        b[i] = (k * 53 + i * 29) & 0xFF
    if k == 30:
        b[7:11] = bytes([0, 0, 0, 0xF0])
    b[14] = ~sum(b[6:14]) & 0xFF
    if k == 31:
        b[14] ^= 1
    if k == 32:
        b[2] = 0x11
    if k == 33:
        b[1] = 0x58
    if k == 34:
        b[0] = 1
    if k == 35:
        n = 14
    if k == 36:
        n = 16
    if k == 37:
        b[3] = 9
    w = frame(b[:n])
    if k == 38:
        w[0] = 0xF1
    if k == 39:
        w[-1] = 0x00
    r = f.parse_request(bytes(w))
    print(f"req {k} {w.hex()} " + (f"{r[0]:02x} {r[1]:08x} {r[2]}" if r else "none"))
for k in range(len(IDS) + 6):
    b = bytearray([0x00, 0x59, 0x11, 27, 0, 0] + [0] * 29)
    t = IDS[k if k < len(IDS) else 0].encode()
    n = 34
    b[6:6 + len(t)] = t
    if k % 2:
        b[30:33] = bytes([0xEF, 0x40, (0x14 + k) & 0xFF])
    if k == len(IDS):
        b[6 + 8], b[6 + 9] = 0, ord("Z")
    if k == len(IDS) + 1:
        b[2] = 0x30
    if k == len(IDS) + 2:
        n = 33
    if k == len(IDS) + 3:
        n = 35
    b[33] = ~sum(b[6:33]) & 0xFF
    if k == len(IDS) + 4:
        b[33] ^= 0x55
    w = frame(b[:n])
    if k == len(IDS) + 5:
        w[-1] = 0x01
    i = f.parse_identity(bytes(w))
    print(f"ident {k} {w.hex()} " + (f"[{i.text}] {int(i.loader)} {i.chip:06x}" if i else "none"))
for k, p in enumerate(PRODUCTS):
    got = f.product_of(make_pkg(960, p, 100 + k))
    print(f"product {k} " + (f"[{got}]" if re.fullmatch(r"[^_]+_\d+", got) else "none"))
for a, b in MODELS:
    print(f"model {a} {b} {int(f.model_of(a) == f.model_of(b))}")
for path in sys.argv[2:]:
    raw = open(path, "rb").read()
    try:
        product, image = f.load_package(path, False)
        print(f"pkg {len(raw)} [{product}] 1 1 {fnv(image):016x}")
    except f.InstallError:
        product = f.product_of(raw)
        ok = bool(re.fullmatch(r"[^_]+_\d+", product))
        print(f"pkg {len(raw)} [{product if ok else ''}] {int(ok)} {int(f.LOADER_MARK in raw)} "
              f"{fnv(f.logical_image(raw)) if ok else 0:016x}")
