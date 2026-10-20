# KindlePageturner

DIY Bluetooth-Pageturner für Kindle-Modelle ab 2024 (Firmware ≥ 5.20.1), kompatibel zur Amazon Kindle Click.

## Hardware
- Seeed XIAO ESP32-S3 (externe Antenne am U.FL anstecken)
- 4 Taster gegen GND, 1-zelliger LiPo an den BAT-Pads
- Spannungsteiler 2× 220 kΩ von BAT+ auf D10, 100 nF gegen GND

| Funktion | Pin | GPIO |
|---|---|---|
| Seite vor (obere Taste) | D0 | 1 |
| Seite zurück (untere Taste) | D1 | 2 |
| Helligkeit + | D4 | 5 |
| Helligkeit − | D5 | 6 |
| Akkuspannung | D10 | 9 |
| Status-LED (onboard, active low) | – | 21 |

Sleep: beide Seitentasten 1 s halten.

## Firmware
- `Firmware/KindlePageturner/KindlePageturner.ino`
- Arduino IDE 2.x, esp32 Core 3.3.x, Board `XIAO_ESP32S3`, USB CDC On Boot: Enabled
- Library: NimBLE-Arduino 2.5.0

## Protokoll (Kurzfassung)
- BLE HOGP, Just Works, Bonding
- Advertising: Flags `0x06`, Manufacturer Data `71 01 04 43` (VID 0x0171, PID 0x0443)
- DIS PnP ID `01 71 01 43 04 00 01`
- Tasten: KEY_PAGEDOWN, KEY_PAGEUP, KEY_BRIGHTNESSUP, KEY_BRIGHTNESSDOWN, KEY_POWER

Referenz: https://github.com/zuyan9/Open-Kindle-Click-nRF52840 (PROTOCOL.md)
