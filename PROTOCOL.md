# Aromatic diffuser BLE protocol

Source: static analysis of the Android app `com.aromatic.families`
(code package `com.IAA360.ChengHao`), then checked against a real device.
Items marked ✅ were verified on hardware; the rest come from the app code only.

## Device

| | |
|---|---|
| Advertised name | `<owner>’s AROMATIC` (set in the app) |
| Advertising | service `FFF0`, manufacturer ID `0x5943`, data `00 00 32` |
| Model | `AF300 Smart`, protocol `CY_V3.0` ✅ |
| Chip | BEKEN `BK-BLE-1.0`, HW 1.0.0, FW 6.1.2, SW 6.3.0 ✅ |
| Control | service `FFF0`, characteristic `FFF6` (write + notify, CCCD `2902`) |
| Do not touch | `F000FFC0-0451-4000-B000-000000000000` (TI-style OAD firmware update) |

No encryption. Every frame is `[command byte][payload...]` written to `FFF6`;
replies arrive as notifications on `FFF6` and start with a reply code.

## Session

1. Connect, enable notifications on `FFF6`. ✅
2. Login: `8F` + 4-digit password as ASCII (e.g. `8F 30 30 30 30` for "0000"). ✅
   If no reply within 500 ms, resend with the pair code appended: `8F` + password + `"AA01"`.
   ✅ The AF300 answers only the `AA01` form, and only **once per connection** —
   to re-read state, disconnect and log in again.
3. Reply `8F` + ASCII text:
   - `"ERROR"` → wrong password.
   - ≤ 2 chars → protocol **v2.0**; login OK when the text is `"AA"` or `"OK"`.
   - longer → protocol **v3.0**; version = chars 4..7, login OK when chars 7..9 are `"AA"` or `"OK"`.
     Byte 13 = feature bits (oil, battery, custom, manyAroma, fan, roundBattery),
     byte 14 = UI bits (bit7 = "reply" mode, see below).
4. After login send the time: v2 `02 dow yy MM dd HH mm ss`, v3 `21 dow yy MM dd HH mm ss`
   (dow: 0 = Sunday; values are plain binary, not BCD).
5. Heartbeat every 5 s: `E0 AA 55`.

## Commands (v3.0)

| Purpose | Frame |
|---|---|
| Fan level / fog per aroma | `2A <aroma=1> 02 <fan level << 1 \| fog> 00` — fan 0–4. ✅ `2A 01 02 03 00` = fan 1, read back in byte 3 of `4A` |
| Total control | `2D <bits: lamp 1 demo fan onOff>` — LSB = on/off. ✅ `2D 08` off, `2D 09` on (no reply; re-login shows `4D 00` / `4D 01`). Writes all five bits, so keep lamp/demo/fan from the last `4D`. |
| Lamp | `31 <ui> 00 <state>` |
| Schedule slot | `2A aroma 02 bits 00 index flags(fan,timeOnOff,show) hOn mOn hOff mOff days customGrade grade [work_hi work_lo stop_hi stop_lo]` — same layout as reply `4A`. ✅ times, days, level, custom times and enable bit all verified by readback and after a bridge reboot. Days: bit0 = Sunday … bit6 = Saturday. |
| Rename | `22` + UTF-8 name |

## Commands (v2.0)

| Purpose | Frame |
|---|---|
| Read schedule slot n (1..5) | `83 n` → reply `83 ...` |
| Write schedule slot | `03 <timeOnOff(1) index(4)> hOn mOn hOff mOff repeat grade [work stop]` |
| Device type | `89` |
| Rename | `01 len name...` |

## Replies (v3.0)

| Code | Meaning |
|---|---|
| `4A` | schedule slot (aroma, index, times, grade) |
| `46` | max grade + custom work/pause limits |
| `47` | grade table: per grade work(2 B) pause(2 B), seconds |
| `4B` | battery + oil total/remaining per aroma |
| `4D` | total control state: bit0 onOff, bit1 fan, bit2 demo, bit4 lamp ✅ |
| `51` | lamp state (byte 3) |

In "reply" mode the app acknowledges each reply with `code | 0x80` (e.g. `4D` → `CD`).

## Example: full state dump after login (AF300 Smart) ✅

```
8F 43 59 5F 56 33 2E 30 41 41 30 31 01 54 40 04   "CY_V3.0AA01" login OK, features 0x54, UI 0x40
46 04 00 0F 01 2C 00 0F 02 58 01 00 04            4 grades; custom on 15–300 s, off 15–600 s
42 …                                              device name (UTF-8)
45 41 46 33 30 30 20 53 6D 61 72 74               "AF300 Smart"
4A 01 02 01 00 01 07 15 00 17 00 7F 01 01 00 14 01 2C   slot 1: 21:00–23:00 daily, 20 s on / 300 s off
47 00 19 00 50 00 1E 00 32 00 32 00 28 00 5A 00 1E      grades: 25/80, 30/50, 50/40, 90/30 s
4B 00 03 E8 03 E8                                  oil total 1000, remaining 1000
4D 01 00                                           master ON
41 03 1A 0A 15 14 01 15                            device clock: Wed 26-10-21 20:01:21
```

## Device modes and buttons ✅

The panel has a mode button: 6h, 12h, 24h, **Smart** (built-in light sensor:
sprays only when lit) and **Bluetooth** (runs the stored schedules).

- Any BLE session switches the diffuser back to Bluetooth mode, and the mode
  button is ignored while connected. Smart mode and BLE control don't mix.
- Pressing the panel's mode or intensity buttons rewrites the schedule slots
  (e.g. Smart set slot 1 to 00:00–23:59 at Level 3).
- The light level is not reported over BLE: `4C` and the advertisement
  (`00 00 32`) stayed constant while the sensor was covered.
- The diffuser accepted a custom spray of 10 s (reported minimum 15 s).

## Other replies

| Code | Observed | Guess |
|---|---|---|
| `4C` | `4C 00 00 32` (constant) | unknown; same bytes as the advertised manufacturer data |
| `4E` | `4E 00 08 <u16>`, e.g. `0E 07` → `10 E5` | the u16 stays flat outside schedule windows and during pauses, rises while spraying: likely cumulative spray seconds |

## Not yet known
- Whether master-off survives the next schedule window.
- Byte 4 of the slot frame (seen as `00` and `01`).
- Whether the pump honours custom spray times below 15 s exactly.
