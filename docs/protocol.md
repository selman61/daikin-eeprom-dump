# X41A protocol and EEPROM layout

## Line

5 V TTL UART on the X41A service connector, 9600 baud, 8E1.

## Frame

```
[length] [command] [data ...] [checksum]
checksum = 0xFF - (sum of all previous bytes & 0xFF)
```

The board answers with `[command] [data ...] [checksum]`. A command it does not support is
answered with `15 EA` (NAK).

## Commands

This repo uses two read-only commands:

| Cmd | Meaning | Request | Reply | When |
| --- | --- | --- | --- | --- |
| `0x49` | EEPROM read, 1 byte | `04 49 lo hi crc` | `49 dd crc` | Probe, Dump |
| `0x52` | RAM read, 16 bytes at the fixed address 0x0680 (firmware ID, e.g. `DP0009_003`) | `04 52 80 06 crc` | `52 d0..d15 crc` | only if the option is ticked |

The RAM read always uses that one address: reading the controller's register areas
(0x0000-0x03FF, 0x2C00-0x2FFF) can clear status bits.

**Do not send any other command to X41A.** Several of them erase or overwrite the
controller's memory. One example, because it is easy to send by accident with ESPAltherma's
custom command feature: `02 43 BA` resets the EEPROM to firmware defaults
(that is what broke the unit in [dumps/](../dumps/)).

## EEPROM layout (ERSQ A1P, micon ID0091)

- 1 KB physical. 512 logical bytes, each stored as a pair: value at physical `2k`, its complement
  (`~value`) at physical `2k+1`. A valid dump has `value XOR complement == 0xFF` everywhere.
- Addresses wrap at 0x400.
- Logical byte `k` is mirrored in the controller RAM at `0x0400 + k`.
- Field names for logical 0x000-0x094 are best-effort; entries marked (?) are guesses. See
  `FIELDS` in [tools/eeprom.py](../tools/eeprom.py).
- Logical 0x08B-0x08F holds the "AS number" of the EEPROM spec, e.g. `01 70 43 96 01` =
  AS1704396-01. It survives an EEPROM reset.
