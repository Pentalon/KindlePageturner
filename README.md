# KindlePageturner

DIY Bluetooth-Pageturner für Kindle-Modelle ab 2024 (Firmware ≥ 5.20.1), kompatibel zur Amazon Kindle Click.

Status: v0.1.4 – Seite vor/zurück, Helligkeit +/−, Sleep-Geste und Akkuanzeige am Kindle (FW 5.20.1) getestet.

## Bedienung
| Taste | Funktion |
|---|---|
| Vorne oben | Seite vor |
| Vorne unten | Seite zurück |
| Seite oben (kurz / gehalten) | Helligkeit + |
| Seite unten (kurz / gehalten) | Helligkeit − |
| Beide Seitentasten 1 s | Kindle schlafen legen |

Der Pageturner kann den Kindle nicht aufwecken (der Kindle schaltet im Ruhezustand Bluetooth ab). Nach dem Aufwecken verbindet er sich in 2–4 s neu.

## Hardware
Board-Auswahl im Sketch per `#define BOARD_DEVKIT` bzw. `#define BOARD_XIAO`.

| Funktion | GPIO | XIAO-Pin |
|---|---|---|
| Seite vor | 1 | D0 |
| Seite zurück | 2 | D1 |
| Helligkeit + | 5 | D4 |
| Helligkeit − | 6 | D5 |
| Akkuspannung (nur XIAO) | 9 | D10 |
| Status-LED | 48 (RGB, DevKit) | 21 (XIAO, active low) |

Taster jeweils gegen GND (interne Pull-ups).

### Seeed XIAO ESP32-S3 (Zielhardware)
- externe Antenne am U.FL anstecken
- 1-zelliger LiPo an den BAT-Pads
- Spannungsteiler 2× 220 kΩ von BAT+ auf D10, 100 nF gegen GND

### ESP32-S3-N16R8 DevKit (Entwicklung, Breadboard)
- Versorgung über USB, keine Akkumessung (meldet 100 %)

## Firmware
- `Firmware/KindlePageturner/KindlePageturner.ino`
- Arduino IDE 2.x, esp32 Core 3.3.x
- Library: NimBLE-Arduino 2.5.1 (h2zero)

| Einstellung | DevKit | XIAO |
|---|---|---|
| Board | ESP32S3 Dev Module | XIAO_ESP32S3 |
| Flash Size | 16MB | 8MB |
| USB CDC On Boot | Enabled (Port „USB“) | Enabled |

Nach Änderungen an Report Map oder GATT-Services: am Kindle „Vergessen“ und neu koppeln.

## Protokoll (Kurzfassung)
- BLE HOGP, Just Works, Bonding; Pairing startet nur der Kindle
- Advertising (ADV_IND): Flags `0x06`, Manufacturer Data `71 01 04 43` (VID 0x0171 LE, PID 0x0443 BE), Intervall 20 ms
- DIS PnP ID `01 71 01 43 04 00 01`, Firmware Revision `0x2A26`
- Battery Level `0x2A19` per Notify
- Report ID 1, eine Application Collection: KEY_PAGEDOWN, KEY_PAGEUP (Keyboard), Brightness +/−, Power (Consumer)

Referenz: https://github.com/zuyan9/Open-Kindle-Click-nRF52840 (PROTOCOL.md)
