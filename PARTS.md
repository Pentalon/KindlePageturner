# Teileliste (Bill of Materials)

## Elektronik
| Pos | Anzahl | Bauteil | Spezifikation | Anmerkung |
|---|---|---|---|---|
| 1 | 1 | Seeed XIAO ESP32-S3 | ESP32-S3, 8 MB Flash, LiPo-Lader onboard | nicht die „Sense“-Variante nötig |
| 2 | 1 | 2,4-GHz-Antenne | FPC-Klebeantenne mit U.FL-Stecker | liegt dem XIAO bei; für Bluetooth erforderlich |
| 3 | 1 | Folientastatur 1×4 | Membrantastatur, selbstklebend, 5-polig (1× gemeinsam + 4 Tasten) | Taste 1–4 → D0, D1, D4, D5; gemeinsam → GND |
| 4 | 1 | LiPo-Akku 1S | 3,7 V, 300 mAh, mit Schutzschaltung (PCM) | an BAT+/BAT− auf der XIAO-Rückseite; Polarität prüfen |
| 5 | 2 | Widerstand | 220 kΩ, 1 %, 0603/0805 oder THT | Spannungsteiler BAT+ → D9 → GND |
| 6 | 1 | Kondensator | 100 nF, keramisch | D9 gegen GND |
| 7 | – | Litze | dünn, z. B. 0,14 mm² / AWG 26–30 | Verdrahtung |
| 8 | – | Schrumpfschlauch | passend zur Litze | Isolierung Spannungsteiler |

## Mechanik
| Pos | Anzahl | Bauteil | Spezifikation | Anmerkung |
|---|---|---|---|---|
| 9 | 1 | Gehäuse | 3D-Druck, PLA/PETG | Aussparung USB-C zum Laden und Flashen |
| 10 | – | Doppelseitiges Klebeband | dünn | Akku und Antenne im Gehäuse fixieren |

## Werkzeug / Software
- Lötstation, Multimeter (Akkupolarität, Kalibrierung `BAT_DIVIDER`)
- Arduino IDE 2.x, esp32 Core 3.3.x, NimBLE-Arduino 2.5.1

## Optional (Entwicklung)
| Bauteil | Anmerkung |
|---|---|
| ESP32-S3-N16R8 DevKit | Breadboard-Aufbau, `#define BOARD_DEVKIT` |
| 4 Taster, Breadboard, Jumperkabel | Tasten an GPIO 1, 2, 5, 6 gegen GND |
