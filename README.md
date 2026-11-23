# APRS World Radar

Open-source ESP32 firmware for a 1.3" 240×240 display. It shows live **APRS stations** and **aircraft** around you on a
radar sweep, a driving dashboard with your drive trail, and your own station card with a QR code. Data comes from
[aprs.world](https://aprs.world) and [adsb.lol](https://adsb.lol). It works at home over Wi-Fi and in the car over a
phone hotspot.

| Screen | What it shows |
|---|---|
| APRS radar | Stations around you with afterglow blips (colour = station type), nearest callsigns, auto-zoom; heading-up while driving |
| Air traffic | Aircraft with heading, one-minute speed vector, colour by altitude, traffic beyond the ring as rim dots |
| Drive | Speed, heading tape, nearest APRS station and aircraft with direction arrows, trip distance |
| Trail | Your drive drawn on a grid (last minute highlighted, gaps over 30 s left open), distance, time, average speed |
| My station | Your callsign, last heard, device, packet count, QR code to your aprs.world page |
| aprs.world | Logo and a QR code to the live map |

Position is taken from the first available source: GPS module → your tracker's last APRS position (aprs.world) → home
coordinates → the city-level location of your internet connection. With no setup beyond Wi-Fi it already works.

## Hardware

- ESP32 DevKit V1 (ESP32-WROOM-32, 4 MB flash)
- 1.3" 240×240 ST7789 SPI display without a CS pin (e.g. GoldenMorning GMT130)
- Optional: NEO-6M / NEO-M8N GPS module, a push button, a buzzer

### Wiring / Bağlantı

| Display | ESP32 | | GPS | ESP32 |
|---|---|---|---|---|
| GND | GND | | VCC | VIN (5 V) |
| VCC | 3V3 | | GND | GND |
| SCL | GPIO 18 | | TX | GPIO 32 |
| SDA | GPIO 23 | | RX | GPIO 33 |
| RES | GPIO 4 | | | |
| DC | GPIO 21 | | Button → GND | GPIO 27 (BOOT works too) |
| BLK | GPIO 22 | | Buzzer + | GPIO 25 |

Pins are in `src/config.h`. In the car, power the board from a USB car charger, never straight from 12 V.

## Build and flash

[PlatformIO](https://platformio.org) is required.

```bash
pio run -e esp32dev -t upload     # build and flash over USB
pio device monitor                # serial log: Wi-Fi, every request, fps, heap
```

Optional local defaults: copy `include/secrets.example.h` to `include/secrets.h` (git-ignored). The settings page
overrides them.

## Setup

On first boot (or when no saved Wi-Fi is reachable for a minute) the device opens the access point
**`APRSWorld-XXXX`**. The screen shows a QR code that joins it. The setup page opens by itself (or go to
`http://192.168.4.1`). There you can set:

- up to three Wi-Fi networks (home + phone hotspot)
- your callsign, the tracker to follow, home coordinates
- which screens to show, search radius, rotation time
- language (English / Türkçe), units, brightness and night dimming, time zone
- GPS, trail recording, buzzer, and **demo drive**: for shows and for testing without a GPS module, it drives a loop on
  real roads around you, routed by the public [OSRM](https://project-osrm.org) server on OpenStreetMap data

Once connected, the same page is at `http://aprs-radar.local`, together with **drive trails** to download as GPX or
JSON.

Buttons: a short press shows the next screen and pauses rotation for a minute. Holding for 3 s opens the setup access
point; holding again restarts.

## Privacy

Requests carry your position rounded to about 1 km (two decimals). They go only to `api.aprs.world`, `aprs.world`
(the connection-location lookup) and `api.adsb.lol`. Demo drive also asks `router.project-osrm.org` once per boot, over
plain HTTP because that server only speaks TLS 1.3; the position it sends is rounded to about 100 m. Drive trails stay on the device until you download them.

## Development

- `pio run -e sim && .pio/build/sim/program` renders every screen with the firmware's own parsers and UI code into
  `sim/out/*.png`, using captured responses in `sim/data/`. It needs SDL2 headers. `BENCH=1` prints the draw cost per
  screen.
- `tools/make_fonts.py` regenerates `src/fonts_data.h`: Inter with Turkish letters, drawn by `Canvas::text` straight
  into the frame buffer.
- `tools/make_roots.py` regenerates `src/ca_roots.h`, the trusted roots for aprs.world.
- The 240×240 frame is drawn in two 120-row strips: a full RGB565 frame (115 KB) is larger than the biggest free heap
  block of a WROOM-32.

## Licence

MIT, see [LICENSE](LICENSE). Aircraft data © adsb.lol contributors, ODbL 1.0. Demo routes © OpenStreetMap contributors
(ODbL), routed by OSRM. Font: Inter, SIL OFL 1.1.
