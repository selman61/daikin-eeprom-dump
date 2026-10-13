"""Shared helpers: load a dump, split it into logical bytes, and the known field names.

Physical layout (1 KB): logical byte k is stored at physical 2k as the value and at 2k+1 as
its complement (~value). Logical k is also mirrored in the controller RAM at 0x0400 + k.
"""
from pathlib import Path

PHYS_SIZE = 0x400
LOGICAL_SIZE = PHYS_SIZE // 2

# Logical address -> field name. Names for 0x000-0x094 are best-effort; entries marked (?) are
# guesses.
FIELDS = {
    0x000: "Outdoor unit serial no. byte 0",
    0x001: "Outdoor unit serial no. byte 1",
    0x002: "Outdoor unit serial no. byte 2",
    0x003: "Outdoor unit serial no. byte 3",
    0x004: "Checksum of 000-003",
    0x005: "Device ID low",
    0x006: "Device ID high",
    0x007: "Fan intermittent / cool-heat selection / defrost / pipe length / sequential start",
    0x008: "Tc, Te, defrost settings; bit7 = hot water heater",
    0x009: "Time saving, low noise, eco, interlock, air supply, auto cool/heat",
    0x00A: "Thermal storage Tc/Te, retry, capacity priority, sound, forced main",
    0x00C: "bits0-3 HP setting, 4-5 treatment area, 6 power supply, 7 HP/CO",
    0x00D: "bits0-1 line code, 2-3 step power limit, 4-7 outdoor unit type",
    0x00E: "bits0-1 BP connect, 2 defrost EV, 3 12HP casing, 4 ERX, 5 overseas voltage",
    0x00F: "bit1 HO setting",
    0x010: "Auto address, EX4 address, multi, iBF, INV/NON, phase, rotation",
    0x03B: "bit3 = Altherma outdoor flag (?)",
    0x03C: "(?)",
    0x040: "Exchanger type setting (?)",
    0x045: "bit4 = hot water heater 2 presence (?)",
    0x05E: "(?)",
    0x05F: "(?)",
    0x060: "bits4-5 P board type inconsistent (?)",
    0x071: "(?)",
    0x08B: "AS number byte 0",
    0x08C: "AS number byte 1",
    0x08D: "AS number byte 2",
    0x08E: "AS number byte 3",
    0x08F: "AS number byte 4",
    0x095: "Error history: last code (9A = UA) (?)",
    0x096: "Error history: last sub-code (?)",
    0x0FA: "(?)",
}


def load(path):
    """Return a list of 1024 physical byte values (None where unread) from .tsv/.txt or .bin."""
    p = Path(path)
    if p.suffix.lower() == ".bin":
        data = p.read_bytes()
        if len(data) != PHYS_SIZE:
            raise ValueError(f"{p}: expected {PHYS_SIZE} bytes, got {len(data)}")
        return list(data)
    phys = [None] * PHYS_SIZE
    for line in p.read_text(encoding="utf-8").splitlines():
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        parts = line.split()
        if parts[0] == "EEP":  # raw firmware log line
            parts = parts[1:]
        if len(parts) < 2 or len(parts[1]) != 2:
            continue
        try:
            a, v = int(parts[0], 16), int(parts[1], 16)
        except ValueError:
            continue
        if 0 <= a < PHYS_SIZE:
            phys[a] = v
    return phys


def logical(phys):
    """Return (values, problems): 512 logical values and a list of (k, value, complement) that
    do not form a valid (v, ~v) pair."""
    values, problems = [], []
    for k in range(LOGICAL_SIZE):
        v, c = phys[2 * k], phys[2 * k + 1]
        if v is None or c is None or v ^ c != 0xFF:
            problems.append((k, v, c))
        values.append(v)
    return values, problems


def hx(v):
    return "--" if v is None else f"{v:02X}"
