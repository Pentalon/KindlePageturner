# KindlePageturner

DIY Bluetooth-Pageturner für Kindle-Modelle ab 2024 (Firmware ≥ 5.20.1), kompatibel zur Amazon Kindle Click.

Status: v0.2.0 – Seite vor/zurück, Helligkeit +/−, Sleep-Geste, Akkuanzeige und Deep Sleep am Kindle (FW 5.20.1) getestet.

## Bedienung
4 Tasten auf einer Folientastatur (Membrantastatur 1×4):

| Taste | Funktion |
|---|---|
| 1 | Seite vor |
| 2 | Seite zurück |
| 3 (kurz / gehalten) | heller |
| 4 (kurz / gehalten) | dunkler |
| 3 + 4 gemeinsam 1 s | Kindle schlafen legen |

- Der Pageturner kann den Kindle nicht aufwecken (der Kindle schaltet im Ruhezustand Bluetooth ab).
- Nach dem Aufwecken des Kindle: eine Taste drücken. Der Pageturner verbindet sich in 3–5 s neu; ein Druck auf 1 oder 2 wird danach nachgeliefert.
- Seitenrichtung tauschbar am Kindle unter Einstellungen › Zubehör.

## Deep Sleep
| Bedingung | Zeit | `#define` |
|---|---|---|
| kein Kindle verbunden | 60 s | `ADV_TIMEOUT_MS` |
| verbunden, keine Taste | 5 min | `IDLE_TIMEOUT_MS` |
| nach der Sleep-Geste | 1 s nach Loslassen | `AFTER_POWER_MS` |

Jede Taste weckt den Pageturner (EXT1). LED blinkt vor dem Schlafen 2× kurz.

## Hardware
- Seeed XIAO ESP32-S3
- Folientastatur 1×4, gemeinsame Leitung an GND, Tasten gegen GND (interne Pull-ups)
- 1-zelliger LiPo an den BAT-Pads, Laden über USB-C
- Spannungsteiler 2× 220 kΩ von BAT+ auf D9, 100 nF von D9 gegen GND
- Antenne am U.FL anstecken (auch für Bluetooth nötig; XIAO hat keine Chipantenne)

| Funktion | Taste | XIAO-Pin | GPIO |
|---|---|---|---|
| Seite vor | 1 | D0 | 1 |
| Seite zurück | 2 | D1 | 2 |
| heller | 3 | D4 | 5 |
| dunkler | 4 | D5 | 6 |
| Akkuspannung | – | D9 | 8 |
| Status-LED (onboard, active low) | – | – | 21 |

### LED
| Zustand | LED |
|---|---|
| wartet auf Kindle | blinkt kurz 1×/s |
| verbunden | aus, leuchtet bei Tastendruck |
| vor Deep Sleep | 2× kurz |

### Akku kalibrieren
Serial Monitor zeigt `Battery: xxxx mV, yy %`. Mit Multimeter an den BAT-Pads vergleichen (ohne USB, da sonst Ladespannung gemessen wird) und bei Abweichung `BAT_DIVIDER` anpassen: neu = 2,00 × Multimeter ÷ Firmware.

### Entwicklung am DevKit
ESP32-S3-N16R8 DevKit am Breadboard: `#define BOARD_DEVKIT` statt `BOARD_XIAO`. Gleiche GPIOs (1, 2, 5, 6), keine Akkumessung (meldet 100 %), RGB-LED an GPIO48.

## Firmware
- `Firmware/KindlePageturner/KindlePageturner.ino`
- Arduino IDE 2.x, esp32 Core 3.3.x
- Library: NimBLE-Arduino 2.5.1 (h2zero)

| Einstellung | XIAO | DevKit |
|---|---|---|
| Board | XIAO_ESP32S3 | ESP32S3 Dev Module |
| Flash Size | 8MB | 16MB |
| USB CDC On Boot | Enabled | Enabled (Port „USB“) |

- Upload klappt nicht: BOOT halten, USB anstecken, BOOT loslassen, hochladen, danach RESET.
- Nach Änderungen an Report Map oder GATT-Services: am Kindle „Vergessen“ und neu koppeln.

## Protokoll (Kurzfassung)
- BLE HOGP, Just Works, Bonding; Pairing startet nur der Kindle
- Advertising (ADV_IND): Flags `0x06`, Manufacturer Data `71 01 04 43` (VID 0x0171 LE, PID 0x0443 BE), Intervall 20 ms
- DIS PnP ID `01 71 01 43 04 00 01`, Firmware Revision `0x2A26`
- Battery Level `0x2A19` per Notify
- Report ID 1, eine Application Collection: KEY_PAGEDOWN, KEY_PAGEUP (Keyboard), Brightness +/−, Power (Consumer)
- Sleep-Geste (KEY_POWER) muss der Pageturner selbst erkennen; der Kindle wertet keine Tastenkombination aus

Referenz: https://github.com/zuyan9/Open-Kindle-Click-nRF52840 (PROTOCOL.md)
