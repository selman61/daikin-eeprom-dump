"""Check a dump and print it as 512 logical bytes with the known field names.

Usage: python tools/decode.py dumps/FILE.tsv
"""
import sys

from eeprom import FIELDS, LOGICAL_SIZE, hx, load, logical


def main(path):
    values, problems = logical(load(path))
    print(f"{path}")
    if problems:
        print(f"WARNING: {len(problems)} logical bytes are missing or not a valid (v, ~v) pair:")
        for k, v, c in problems[:20]:
            print(f"  L{k:03X}: value {hx(v)} complement {hx(c)}")
    else:
        print("All 512 (value, ~value) pairs are valid.")
    print()
    for row in range(0, LOGICAL_SIZE, 16):
        print(f"L{row:03X} (RAM {0x400 + row:04X})  " + " ".join(hx(values[row + i]) for i in range(16)))
    print()
    print("Known fields:")
    for k, name in FIELDS.items():
        v = values[k]
        bits = "--------" if v is None else f"{v:08b}"
        print(f"  L{k:03X} = {hx(v)} ({bits})  {name}")


if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    main(sys.argv[1])
