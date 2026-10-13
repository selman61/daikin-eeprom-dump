# Dumps

## File format

Plain text, one byte per line: physical address and value in hex, separated by a tab or space.
Lines starting with `#` are comments; put the model and board in the first one.

```
# ERSQ016AAV1 outdoor A1P
000	01
001	FE
...
3FF	..
```

A raw 1024-byte `.bin` is also accepted by the tools.

The dumper names its download after the EEPROM spec (and firmware, if read), e.g.
`eeprom_AS1704396-01_DP0009_003.tsv`. Before adding it here, rename it to
`MODEL_BOARD_[STATE_]YYYY-MM-DD.tsv`, e.g. `ERRQ011AAV1_A1P_2026-11-02.tsv`.

## Collected

| File | Unit | State |
| --- | --- | --- |
| [ERSQ016AAV1_A1P_ID0091_after-eeprom-reset_2026-10-12.tsv](ERSQ016AAV1_A1P_ID0091_after-eeprom-reset_2026-10-12.tsv) | ERSQ016AAV1 + EKHBRD016ACV1 | **Broken reference**: after an EEPROM reset, UA-39 |

Since this dump, the following logical bytes were changed on that unit while testing (UA-39 is
still present): `008` 2A→AA (hot water heater ON via BS buttons), `03B` 10→18, `00C` 50→60.
