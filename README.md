# Daikin Altherma HT EEPROM dumps (ERSQ / ERRQ outdoor A1P)

**Looking for a healthy EEPROM dump of an ERSQ011/014/016AA or ERRQ011/014/016AA outdoor unit
(Daikin Altherma high temperature, R410A + EKHBRD indoor unit).** If you own one that works,
you can help with a read-only dump that takes about 10 minutes and changes nothing on your unit.

## Why

On one ERSQ016AAV1 the outdoor controller (A1P) received the service command `02 43` on X41A
by accident. That command resets the controller's 1 KB EEPROM to firmware defaults, which drops
the model-specific settings. Since then the unit shows **UA-39** ("Altherma module
incompatibility"), and the remote reports the wrong outdoor model.

No public EEPROM image exists for this outdoor unit. The fastest fix is to compare against a
healthy unit and restore only the bytes that differ. The broken image is in
[dumps/](dumps/) for reference.

## Safety first

- **Tested on one unit only:** an ERSQ016AAV1 outdoor unit (A1P board) with a NodeMCU. It has
  not been tried on any other model or board.
- **Read-only.** The firmware in this repo sends `04 49 lo hi crc`, a 1-byte EEPROM read.
  Optionally, if you tick it on the page, it also
  reads the board's firmware version once (16 bytes of RAM at the fixed address 0x0680).
  There is no code path that writes.
- **Never send other commands to X41A.** Several of them erase or overwrite the controller's
  memory; `02 43 BA` alone resets the EEPROM. Do not "scan" unknown command bytes on a live unit.
- **Mains voltage.** The outdoor unit has 230 V and large inverter capacitors. Switch off the
  breaker, wait at least 10 minutes, and follow the service manual before opening the switch box
  or touching connectors. Connect and disconnect only with the power off.
- **No liability.** Everything here is provided as is, without any warranty. You use it entirely
  at your own risk; the authors accept no responsibility for any damage to your unit, injury,
  lost warranty or other loss. If you are not comfortable working inside the outdoor unit, ask
  your installer.

## What you need

- An ESP32 or a NodeMCU (ESP8266). If you already run ESPAltherma, the same board works.
- PlatformIO to flash it once.
- Jumper wires for the X41A connector on the outdoor A1P board.
- A phone or laptop with Wi-Fi. No USB cable, MQTT broker or home Wi-Fi is needed at the unit.

## Wiring

The connection was worked out by the ESPAltherma community in
[raomin/ESPAltherma#95](https://github.com/raomin/ESPAltherma/issues/95): on ERSQ/ERRQ the
service port is **X41A on the outdoor A1P board** (ESPAltherma protocol I). If ESPAltherma
already runs on your board, keep that wiring and use the same pins in `setup.h`.

The line is 5 V TTL, 9600 baud, 8 data bits, even parity, 1 stop bit. Always connect GND.

## How to dump

1. Copy `firmware/include/setup.h.example` to `firmware/include/setup.h` and edit it (home
   Wi-Fi, pins). If ESPAltherma already runs on the board, use the same pins as there and leave
   `WIFI_SSID` empty: the ESP then uses the Wi-Fi ESPAltherma saved.
2. Flash it: `cd firmware`, then `pio run -e nodemcu -t upload --upload-port <IP>` (OTA, also
   from a running ESPAltherma) or `pio run -e esp32 -t upload`.
3. If the ESP is not wired yet: with the unit powered off, wire it to X41A, then power the unit
   on and wait until it runs normally (no error on the remote).
4. Open the page:
   - The ESP joins your home Wi-Fi. Open its IP (see your router)
     or http://daikin-dump.local.
   - If it cannot join within 60 s, it opens its own Wi-Fi **Daikin-EEPROM-Dump** (password
     `altherma1`). Join it with your phone and open **http://192.168.4.1**. If nobody joins it
     within 5 minutes, the ESP restarts and tries the home Wi-Fi again.
5. Press **Probe**. It reads one byte. If it says "Probe OK", press **Dump**. Reading all
   1024 bytes takes a few minutes; the page shows the progress.
6. When it says "Dump complete, all 512 pairs valid", press **Download .tsv**. The file is named
   after the EEPROM spec number (e.g. `eeprom_AS1704396-01.tsv`) and, if you ticked the option,
   the firmware version (`eeprom_AS1704396-01_DP0009_003.tsv`).

Nothing is sent to the unit until you press Probe or Dump.

### Going back to your previous firmware

Flash your previous firmware again, either by OTA as usual
(`pio run -e <your env> -t upload --upload-port <IP>`) or in the browser: open
**/update** on the dumper page and upload its `firmware.bin`
(PlatformIO: `.pio/build/<env>/firmware.bin`). This also works on the dumper's own Wi-Fi.

Power the unit off before you remove any wires.

### Check the result

`python tools/decode.py eeprom_dump.tsv` must report "All 512 (value, ~value) pairs are
valid". If it does not, run the dump again.

## Sharing your dump

Open an issue with the **EEPROM dump** template and attach the `.tsv` file, or send a pull request
that adds it to `dumps/` (rename it as described in [dumps/README.md](dumps/README.md)). Please include the outdoor and indoor model names from the nameplates,
and the board's software version if you know it.

The first 4 bytes hold an outdoor serial number field. On our unit it was blank (`01 00 00 00`).
If yours holds a real serial number and you prefer not to share it, say so in the issue.

## Repository layout

| Path | What |
| --- | --- |
| [firmware/](firmware/) | ESP32 / ESP8266 read-only dumper with a web page (PlatformIO) |
| [tools/decode.py](tools/decode.py) | Checks a dump and prints the 512 logical bytes with field names |
| [tools/compare.py](tools/compare.py) | Diffs two dumps |
| [docs/protocol.md](docs/protocol.md) | Frame format, commands, and EEPROM layout |
| [dumps/](dumps/) | Collected dumps |

## License

MIT, see [LICENSE](LICENSE). Not affiliated with Daikin.
