"""Compare two dumps byte by byte (logical addresses) and show the field names of differences.

Usage: python tools/compare.py dumps/A.tsv dumps/B.tsv
"""
import sys

from eeprom import FIELDS, hx, load, logical


def main(path_a, path_b):
    a, _ = logical(load(path_a))
    b, _ = logical(load(path_b))
    diffs = [k for k in range(len(a)) if a[k] != b[k]]
    print(f"A = {path_a}\nB = {path_b}\n{len(diffs)} logical bytes differ\n")
    print("addr   RAM    A   B   changed bits  field")
    for k in diffs:
        changed = "" if a[k] is None or b[k] is None else f"{a[k] ^ b[k]:08b}"
        print(f"L{k:03X}  {0x400 + k:04X}  {hx(a[k])}  {hx(b[k])}  {changed:12}  {FIELDS.get(k, '')}")


if __name__ == "__main__":
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    main(sys.argv[1], sys.argv[2])
