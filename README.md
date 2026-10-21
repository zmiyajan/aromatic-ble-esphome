# Aromatic BLE → Home Assistant bridge

Control a Bluetooth **Aromatic** scent diffuser from Home Assistant using a
cheap ESP32 running [ESPHome](https://esphome.io). The ESP32 talks Bluetooth LE
to the diffuser and WiFi to Home Assistant.

```
Home Assistant  ⇄  WiFi  ⇄  ESP32-C3 (ESPHome)  ⇄  Bluetooth LE  ⇄  Diffuser
```

> Unofficial project, not affiliated with Aromatic Families Trading Est.
> The protocol was worked out for interoperability with a device the author owns.

## What you get in Home Assistant

| Entity | Type | |
|---|---|---|
| **Diffuser** | switch | Master on/off. State is read back from the device. |
| Diffuser login | binary sensor | Whether the last BLE login succeeded (diagnostic) |
| Refresh diffuser state | button | Reads the current state now (diagnostic) |

The bridge **connects on demand**: on each command, and every 5 minutes, it
connects, logs in, reads the state, sends the command if there is one, and
disconnects. Between those connections the phone app can still reach the
diffuser.

## Tested hardware

| | |
|---|---|
| Diffuser | Aromatic **AF300 Smart**, protocol `CY_V3.0`, BEKEN `BK-BLE-1.0` chip |
| App | Aromatic Families (Android `com.aromatic.families`) |
| Bridge | ESP32-C3 Super Mini, ESPHome 2026.10.0 |

Other models that use the same app will probably work too. The `2D` master
switch only applies to models where the app shows a "total control" switch; see
[PROTOCOL.md](PROTOCOL.md).

## Setup

1. Install ESPHome (`pip install esphome`, `uv tool install esphome`, or the
   Home Assistant add-on).
2. Copy `secrets.yaml.example` to `secrets.yaml` and fill it in:
   - WiFi name and password
   - the diffuser's 4-digit app password (the tested unit used `0000`)
   - the diffuser's MAC address. To find it, flash `probe-1-scan.yaml` and look
     for a device named `…AROMATIC` advertising service `0xFFF0`:
     ```sh
     esphome run probe-1-scan.yaml
     ```
   - an API key: `openssl rand -base64 32`
3. Close the Aromatic app on your phone. The diffuser accepts only one
   connection at a time.
4. Flash the bridge over USB:
   ```sh
   esphome run aromatic-bridge.yaml
   ```
5. In Home Assistant, go to **Settings → Devices & services**. Accept the
   discovered **Aromatic** ESPHome device, or add it by IP, and paste the
   `api_encryption_key` when asked.

Place the ESP32 within a few metres of the diffuser and power it from any USB
charger.

## Probes

These configs were used to reverse-engineer the protocol. They are handy if you
have a different model:

| File | What it does | Writes to the device? |
|---|---|---|
| `probe-1-scan.yaml` | Lists nearby BLE devices with services and manufacturer data | No |
| `probe-2-gatt.yaml` | Connects and lists GATT services | No |
| `probe-3-info.yaml` | Reads Device Information and battery | No |
| `probe-4-login.yaml` | Logs in and dumps every reply as hex | Login only |
| `probe-5-onoff.yaml` | OFF then ON across fresh connections, with readback | **Yes** |

All of them print to the USB log, so no WiFi is needed:
`esphome run probe-N-….yaml`.

## Safety

- Never write to service `F000FFC0-0451-4000-B000-000000000000`. It is the
  firmware-update (OAD) service, and a bad write can brick the diffuser.
- The bridge only sends the documented `8F` login, `21` time sync and `2D`
  total-control frames.

## Known limits

- Intensity and schedules are not exposed yet. The frames are documented in
  PROTOCOL.md but untested.
- It is untested whether turning the master switch off survives the next
  schedule window.
- On the tested unit, the device answers login only once per connection, which
  is why the bridge reconnects to refresh.

## بالعربي

جسر يربط معطر **Aromatic** اللي يشتغل بالبلوتوث مع **Home Assistant** عن طريق
شريحة ESP32 عليها ESPHome. يطلع لك في Home Assistant زر تشغيل/إطفاء للمعطر،
وتقدر تسوي عليه أتمتة. التفاصيل التقنية في [PROTOCOL.md](PROTOCOL.md).

خطوات التركيب:
1. انسخ `secrets.yaml.example` باسم `secrets.yaml` وعبّه.
2. سكّر تطبيق المعطر من جوالك.
3. برمج الشريحة بأمر `esphome run aromatic-bridge.yaml`.
4. ضيف الجهاز في Home Assistant.

## License

[MIT](LICENSE)
