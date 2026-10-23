// KindlePageturner v0.2.0
// BLE HID page turner, Kindle-Click-kompatibel (Kindle FW >= 5.20.1)
// Arduino Core esp32:esp32 3.3.x, Library NimBLE-Arduino 2.5.x (h2zero)
//
// Board-Auswahl unten per #define:
//   BOARD_DEVKIT : ESP32-S3-N16R8 DevKit, Breadboard, Versorgung USB  -> IDE-Board "ESP32S3 Dev Module"
//   BOARD_XIAO   : Seeed XIAO ESP32-S3, LiPo + Spannungsteiler an D9   -> IDE-Board "XIAO_ESP32S3"
//
// v0.1.1: Board-Umschaltung, DevKit ohne Akkumessung (meldet 100 %), RGB-LED am DevKit,
//         startServices() entfernt (in NimBLE 2.x deprecated, Server startet Services selbst).
// v0.1.2: struct Button / enum LedState vor die erste Funktion verschoben (Arduino-Prototypen).
// v0.1.3: setup() wartet bis 3 s auf den USB-CDC-Port, damit die Startmeldung sichtbar ist.
// v0.1.4: Sleep-Geste ändert die Helligkeit nicht mehr (Helligkeit erst nach 150-ms-Fenster).
// v0.1.5: XIAO aktiv, Akku an D9/GPIO8, Kalibrierfaktor BAT_DIVIDER, Akku-Ausgabe im Serial Monitor.
// v0.2.0: Deep Sleep mit EXT1-Wake auf allen 4 Tasten:
//         - 60 s ohne Kindle-Verbindung, 5 min ohne Tastendruck, oder nach der Sleep-Geste
//         - Wecktaste Seite vor/zurück wird nach dem Reconnect nachgeliefert
//         - gehaltene Taste nach dem Puffern unterdrückt (kein doppeltes Blättern)
//         - CPU 80 MHz, USB-Wartezeit nur nach Power-on

#include <Arduino.h>
#include <NimBLEDevice.h>
#include <NimBLEHIDDevice.h>
#include "esp_sleep.h"
#include "driver/rtc_io.h"

// #define BOARD_DEVKIT
#define BOARD_XIAO

// ---------------- Pins ----------------
#if defined(BOARD_DEVKIT)
  #define PIN_FWD     1    // GPIO1  Seite vor    (KEY_PAGEDOWN)
  #define PIN_BACK    2    // GPIO2  Seite zurück (KEY_PAGEUP)
  #define PIN_BR_UP   5    // GPIO5  Helligkeit +
  #define PIN_BR_DN   6    // GPIO6  Helligkeit -
  #define PIN_RGB     48   // onboard WS2812 (manche DevKitC v1.0: GPIO38)
  #define HAS_BATTERY 0
#elif defined(BOARD_XIAO)
  #define PIN_FWD     1    // D0
  #define PIN_BACK    2    // D1
  #define PIN_BR_UP   5    // D4
  #define PIN_BR_DN   6    // D5
  #define PIN_BAT     8    // D9 (GPIO8), Teiler 2x 220k
  #define PIN_LED     21   // User-LED, active low
  #define HAS_BATTERY 1
#else
  #error "BOARD_DEVKIT oder BOARD_XIAO definieren"
#endif

// ---------------- Timing ----------------
#define DEBOUNCE_MS        20
#define SLEEP_CHORD_MS     1000     // beide Helligkeitstasten halten -> KEY_POWER
#define CHORD_WINDOW_MS    150      // Zeitfenster, in dem die zweite Seitentaste als Geste zählt
#define READY_DELAY_MS     2500     // nach Connect warten, bis Kindle HID geöffnet hat
#define BAT_INTERVAL_MS    60000    // Akkustand regelmäßig neu senden
#define ADV_INTERVAL_UNITS 32       // 32 * 0.625 ms = 20 ms

#define ADV_TIMEOUT_MS     60000UL  // so lange ohne Verbindung werben, dann Deep Sleep
#define IDLE_TIMEOUT_MS    300000UL // verbunden, aber so lange keine Taste -> trennen + Deep Sleep
#define AFTER_POWER_MS     1000UL   // nach Sleep-Geste und Loslassen -> Deep Sleep

#define FW_VERSION  "0.2.0"
#define BAT_DIVIDER 2.00f   // Teilerfaktor, bei Abweichung zum Multimeter anpassen
#define DEVICE_NAME "Kindle Pageturner"

// ---------------- Report ----------------
// Eine Application Collection, Report ID 1, 1 Byte:
// Bit0 PageDown, Bit1 PageUp, Bit2 BrightUp, Bit3 BrightDown, Bit4 Power
#define BIT_FWD     0x01
#define BIT_BACK    0x02
#define BIT_BR_UP   0x04
#define BIT_BR_DN   0x08
#define BIT_POWER   0x10

static const uint8_t reportMap[] = {
  0x05, 0x01,        // Usage Page (Generic Desktop)
  0x09, 0x06,        // Usage (Keyboard)
  0xA1, 0x01,        // Collection (Application)
  0x85, 0x01,        //   Report ID (1)
  0x15, 0x00,        //   Logical Minimum (0)
  0x25, 0x01,        //   Logical Maximum (1)
  0x75, 0x01,        //   Report Size (1)
  0x05, 0x07,        //   Usage Page (Keyboard/Keypad)
  0x09, 0x4E,        //   Usage (Page Down)  -> KEY_PAGEDOWN
  0x09, 0x4B,        //   Usage (Page Up)    -> KEY_PAGEUP
  0x95, 0x02,        //   Report Count (2)
  0x81, 0x02,        //   Input (Data,Var,Abs)
  0x05, 0x0C,        //   Usage Page (Consumer)
  0x09, 0x6F,        //   Usage (Brightness Increment) -> KEY_BRIGHTNESSUP
  0x09, 0x70,        //   Usage (Brightness Decrement) -> KEY_BRIGHTNESSDOWN
  0x09, 0x30,        //   Usage (Power)                -> KEY_POWER
  0x95, 0x03,        //   Report Count (3)
  0x81, 0x02,        //   Input (Data,Var,Abs)
  0x95, 0x03,        //   Report Count (3)
  0x81, 0x03,        //   Input (Const) – Padding
  0xC0               // End Collection
};

static const uint8_t wakePins[] = {PIN_FWD, PIN_BACK, PIN_BR_UP, PIN_BR_DN};

// ---------------- Typen ----------------
// Müssen vor der ersten Funktion stehen: die Arduino-IDE fügt dort automatisch
// Funktionsprototypen ein, die diese Typen bereits verwenden.
struct Button {
  uint8_t  pin;
  bool     state;    // entprellt, true = gedrückt
  bool     raw;
  uint32_t changedAt;
};

enum LedState { LED_OFF, LED_ADV, LED_CONN, LED_KEY };

enum SideState { SIDE_IDLE, SIDE_PENDING, SIDE_ACTIVE, SIDE_CHORD };

// ---------------- LED ----------------
static void ledSet(LedState s) {
#if defined(BOARD_DEVKIT)
  switch (s) {
    case LED_OFF:  rgbLedWrite(PIN_RGB, 0, 0, 0);   break;
    case LED_ADV:  rgbLedWrite(PIN_RGB, 0, 0, 40);  break;   // blau
    case LED_CONN: rgbLedWrite(PIN_RGB, 0, 8, 0);   break;   // grün gedimmt
    case LED_KEY:  rgbLedWrite(PIN_RGB, 0, 60, 0);  break;   // grün hell
  }
#else
  digitalWrite(PIN_LED, (s == LED_ADV || s == LED_KEY) ? LOW : HIGH);
#endif
}

// ---------------- BLE ----------------
static NimBLEServer*         server   = nullptr;
static NimBLEHIDDevice*      hid      = nullptr;
static NimBLECharacteristic* inReport = nullptr;

static volatile bool     connected   = false;
static volatile uint32_t connectedAt = 0;
static volatile uint16_t connHandle  = 0;
static volatile uint32_t advStart    = 0;      // Beginn der aktuellen Werbephase
static volatile bool     goingToSleep = false;

class ServerCallbacks : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer* s, NimBLEConnInfo& info) override {
    connected   = true;
    connectedAt = millis();
    connHandle  = info.getConnHandle();
    Serial.printf("Connected: %s\n", info.getAddress().toString().c_str());
  }
  void onDisconnect(NimBLEServer* s, NimBLEConnInfo& info, int reason) override {
    connected = false;
    Serial.printf("Disconnected, reason 0x%X\n", reason);
    if (!goingToSleep) {
      advStart = millis();
      NimBLEDevice::startAdvertising();
    }
  }
  void onAuthenticationComplete(NimBLEConnInfo& info) override {
    Serial.printf("Auth: encrypted=%d bonded=%d\n", info.isEncrypted(), info.isBonded());
  }
};

// ---------------- Tasten ----------------
static Button bFwd  = {PIN_FWD,   false, false, 0};
static Button bBack = {PIN_BACK,  false, false, 0};
static Button bUp   = {PIN_BR_UP, false, false, 0};
static Button bDn   = {PIN_BR_DN, false, false, 0};

static uint32_t lastActivity = 0;   // letzte Tastenänderung

// true bei neuem Druck (Flanke)
static bool updateButton(Button& b, uint32_t now) {
  bool r = (digitalRead(b.pin) == LOW);
  if (r != b.raw) { b.raw = r; b.changedAt = now; }
  if ((now - b.changedAt) >= DEBOUNCE_MS && b.state != b.raw) {
    b.state = b.raw;
    lastActivity = now;
    return b.state;
  }
  return false;
}

static bool anyButtonHeld() {
  for (uint8_t p : wakePins) if (digitalRead(p) == LOW) return true;
  return false;
}

// ---------------- Akku ----------------
static uint8_t readBatteryPercent() {
#if HAS_BATTERY
  uint32_t adc = 0;
  for (int i = 0; i < 16; i++) adc += analogReadMilliVolts(PIN_BAT);
  uint32_t mv = (uint32_t)((adc / 16) * BAT_DIVIDER);
  uint8_t pct;
  if (mv >= 4150)      pct = 100;
  else if (mv <= 3300) pct = 0;
  else                 pct = (uint8_t)((mv - 3300) * 100 / (4150 - 3300));
  Serial.printf("Battery: %u mV, %u %%\n", (unsigned)mv, (unsigned)pct);
  return pct;
#else
  return 100;                               // USB-Versorgung
#endif
}

// ---------------- Senden ----------------
static uint8_t lastSent = 0;

static void sendReport(uint8_t bits) {
  inReport->setValue(&bits, 1);
  inReport->notify();
  lastSent = bits;
  Serial.printf("Report 0x%02X\n", bits);
}

static void tap(uint8_t bit) {
  sendReport(lastSent | bit);
  delay(30);
  sendReport(lastSent & ~bit);
}

// ---------------- Deep Sleep ----------------
static void goDeepSleep(const char* why) {
  Serial.printf("Deep Sleep: %s\n", why);
  goingToSleep = true;

  if (connected) {
    if (lastSent) sendReport(0);                // nichts gedrückt hinterlassen
    server->disconnect(connHandle);
    for (uint32_t t = millis(); connected && (millis() - t) < 500; ) delay(10);
  }
  NimBLEDevice::stopAdvertising();

  // Warten, bis alle Tasten losgelassen sind – sonst sofortiger Wake
  while (anyButtonHeld()) delay(10);
  delay(DEBOUNCE_MS * 2);

  // LED: 2x kurz blinken, dann aus
  for (int i = 0; i < 2; i++) { ledSet(LED_KEY); delay(60); ledSet(LED_OFF); delay(120); }
#if !defined(BOARD_DEVKIT)
  pinMode(PIN_LED, INPUT);                     // LED-Pin hochohmig im Schlaf
#endif

  // EXT1: Wake, sobald eine Taste nach GND gezogen wird.
  // Pull-ups im RTC-Bereich halten, sonst floaten die Pins (Fehl-Wakeups).
  uint64_t mask = 0;
  for (uint8_t p : wakePins) {
    gpio_num_t g = (gpio_num_t)p;
    rtc_gpio_init(g);
    rtc_gpio_set_direction(g, RTC_GPIO_MODE_INPUT_ONLY);
    rtc_gpio_pullup_en(g);
    rtc_gpio_pulldown_dis(g);
    rtc_gpio_hold_en(g);
    mask |= (1ULL << p);
  }
  esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON);
  esp_sleep_enable_ext1_wakeup(mask, ESP_EXT1_WAKEUP_ANY_LOW);

  Serial.flush();
  esp_deep_sleep_start();
}

// ---------------- Zustände Loop ----------------
static uint8_t   pending     = 0;      // gepufferter Seitenwechsel bis Kindle bereit
static uint8_t   suppress    = 0;      // gehaltene Tasten ignorieren, bis losgelassen
static bool      wasReady    = false;
static SideState side        = SIDE_IDLE;
static uint8_t   sideFirst   = 0;
static uint32_t  sideStart   = 0;
static bool      chordFired  = false;
static uint32_t  powerSentAt = 0;
static uint32_t  lastBatSent = 0;
static uint32_t  lastBlink   = 0;

// ---------------- Setup ----------------
void setup() {
  setCpuFrequencyMhz(80);

  // Haltefunktion aus dem letzten Deep Sleep aufheben
  for (uint8_t p : wakePins) {
    rtc_gpio_hold_dis((gpio_num_t)p);
    rtc_gpio_deinit((gpio_num_t)p);
  }

  esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
  bool fromSleep = (cause == ESP_SLEEP_WAKEUP_EXT1);

  Serial.begin(115200);
  // Native USB: nur nach Power-on/Reset auf den COM-Port warten.
  // Nach Deep-Sleep-Wake nicht, sonst kostet jeder Wake im Akkubetrieb 3 s.
  if (!fromSleep) {
    for (uint32_t t = millis(); !Serial && (millis() - t) < 3000; ) delay(10);
    delay(200);
  }

  pinMode(PIN_FWD,   INPUT_PULLUP);
  pinMode(PIN_BACK,  INPUT_PULLUP);
  pinMode(PIN_BR_UP, INPUT_PULLUP);
  pinMode(PIN_BR_DN, INPUT_PULLUP);
#if !defined(BOARD_DEVKIT)
  pinMode(PIN_LED, OUTPUT);
#endif
  ledSet(LED_OFF);

  // Wecktaste auswerten
  if (fromSleep) {
    uint64_t st = esp_sleep_get_ext1_wakeup_status();
    if      (st & (1ULL << PIN_FWD))   { pending = BIT_FWD;  suppress = BIT_FWD;  }
    else if (st & (1ULL << PIN_BACK))  { pending = BIT_BACK; suppress = BIT_BACK; }
    else if (st & (1ULL << PIN_BR_UP)) { suppress = BIT_BR_UP; }
    else if (st & (1ULL << PIN_BR_DN)) { suppress = BIT_BR_DN; }
    Serial.printf("Wake EXT1, pending 0x%02X\n", pending);
  }

  NimBLEDevice::init(DEVICE_NAME);
  // Just Works, Bonding, Secure Connections, kein MITM.
  // Keine eigene Security Request – der Kindle startet das Pairing selbst.
  NimBLEDevice::setSecurityAuth(true, false, true);
  NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);

  server = NimBLEDevice::createServer();
  server->setCallbacks(new ServerCallbacks());

  hid = new NimBLEHIDDevice(server);
  hid->setManufacturer("DIY");
  hid->setPnp(0x01, 0x0171, 0x0443, 0x0100);   // -> 01 71 01 43 04 00 01
  hid->setHidInfo(0x00, 0x02);                  // Country 0, normally connectable
  hid->setReportMap((uint8_t*)reportMap, sizeof(reportMap));
  inReport = hid->getInputReport(1);

  // Firmware Revision 0x2A26 – Anzeige unter Einstellungen > Zubehör
  NimBLECharacteristic* fwRev = hid->getDeviceInfoService()->createCharacteristic(
      NimBLEUUID((uint16_t)0x2A26), NIMBLE_PROPERTY::READ);
  fwRev->setValue(FW_VERSION);

  hid->setBatteryLevel(readBatteryPercent());

  // Advertising: Flags + Manufacturer Data 71 01 04 43 im ADV_IND, Name in Scan Response
  NimBLEAdvertisementData advData;
  advData.setFlags(BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP);
  const uint8_t mfg[] = {0x71, 0x01, 0x04, 0x43};   // VID LE, PID BE
  advData.setManufacturerData(mfg, sizeof(mfg));
  advData.addServiceUUID(NimBLEUUID((uint16_t)0x1812));

  NimBLEAdvertisementData scanData;
  scanData.setName(DEVICE_NAME);

  NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
  adv->setAdvertisementData(advData);
  adv->setScanResponseData(scanData);
  adv->enableScanResponse(true);
  adv->setMinInterval(ADV_INTERVAL_UNITS);
  adv->setMaxInterval(ADV_INTERVAL_UNITS);
  adv->start();                                  // startet auch den GATT-Server

  advStart     = millis();
  lastActivity = millis();

  Serial.printf("KindlePageturner %s advertising, MAC %s\n",
                FW_VERSION, NimBLEDevice::getAddress().toString().c_str());
}

// ---------------- Loop ----------------
void loop() {
  uint32_t now = millis();

  bool fwdPressed  = updateButton(bFwd,  now);
  bool backPressed = updateButton(bBack, now);
  updateButton(bUp, now);
  updateButton(bDn, now);

  // Unterdrückung aufheben, sobald die Taste losgelassen ist
  if ((suppress & BIT_FWD)   && !bFwd.state)  suppress &= ~BIT_FWD;
  if ((suppress & BIT_BACK)  && !bBack.state) suppress &= ~BIT_BACK;
  if ((suppress & BIT_BR_UP) && !bUp.state)   suppress &= ~BIT_BR_UP;
  if ((suppress & BIT_BR_DN) && !bDn.state)   suppress &= ~BIT_BR_DN;

  bool ready = connected && (now - connectedAt) >= READY_DELAY_MS;

  // --- Deep-Sleep-Bedingungen ---
  if (!connected && (now - advStart) >= ADV_TIMEOUT_MS) {
    goDeepSleep("kein Kindle");
  }
  if (connected && (now - lastActivity) >= IDLE_TIMEOUT_MS) {
    goDeepSleep("keine Taste");
  }
  if (powerSentAt && side == SIDE_IDLE && (now - powerSentAt) >= AFTER_POWER_MS) {
    goDeepSleep("Sleep-Geste");
  }

  // --- LED ---
  if (!connected) {                                   // blinkt beim Advertising
    if (now - lastBlink >= 1000) lastBlink = now;
    ledSet((now - lastBlink) < 80 ? LED_ADV : LED_OFF);
  } else {
    ledSet(lastSent ? LED_KEY : LED_OFF);             // verbunden: LED aus (Strom sparen)
  }

  // --- nicht bereit: letzten Seitenwechsel puffern ---
  if (!ready) {
    if (fwdPressed)  { pending = BIT_FWD;  suppress |= BIT_FWD;  }
    if (backPressed) { pending = BIT_BACK; suppress |= BIT_BACK; }
    wasReady = false;
    delay(5);
    return;
  }

  // --- gerade bereit geworden ---
  if (!wasReady) {
    wasReady = true;
    Serial.println("Ready");
    lastSent = 0;
    sendReport(0);
    hid->setBatteryLevel(readBatteryPercent(), true);
    lastBatSent  = now;
    lastActivity = now;
    if (pending) { tap(pending); pending = 0; }
  }

  // --- Seitentasten: Helligkeit oder Sleep-Geste ---
  // Eine Helligkeitstaste wird erst nach CHORD_WINDOW_MS an den Kindle gemeldet.
  // Kommt in dieser Zeit die zweite Seitentaste dazu, ist es die Sleep-Geste
  // und es wird keine Helligkeit gesendet.
  bool upHeld = bUp.state && !(suppress & BIT_BR_UP);
  bool dnHeld = bDn.state && !(suppress & BIT_BR_DN);
  uint8_t brBits = 0;

  switch (side) {
    case SIDE_IDLE:
      if (upHeld || dnHeld) {
        side = SIDE_PENDING; sideStart = now;
        sideFirst = upHeld ? BIT_BR_UP : BIT_BR_DN;
      }
      break;

    case SIDE_PENDING:
      if (upHeld && dnHeld) {
        side = SIDE_CHORD; sideStart = now; chordFired = false;
      } else if (!upHeld && !dnHeld) {
        tap(sideFirst);                       // kurzer Tipp innerhalb des Fensters
        side = SIDE_IDLE;
      } else if ((now - sideStart) >= CHORD_WINDOW_MS) {
        side = SIDE_ACTIVE;
      }
      break;

    case SIDE_ACTIVE:
      if (upHeld && dnHeld) {                 // zweite Taste spät dazu -> Geste
        side = SIDE_CHORD; sideStart = now; chordFired = false;
      } else if (!upHeld && !dnHeld) {
        side = SIDE_IDLE;
      } else {
        if (upHeld) brBits |= BIT_BR_UP;
        if (dnHeld) brBits |= BIT_BR_DN;
      }
      break;

    case SIDE_CHORD:
      if (upHeld && dnHeld && !chordFired && (now - sideStart) >= SLEEP_CHORD_MS) {
        chordFired = true;
        tap(BIT_POWER);
        powerSentAt = now;
        Serial.println("KEY_POWER");
      }
      if (!upHeld && !dnHeld) side = SIDE_IDLE;   // erst nach Loslassen beider Tasten
      break;
  }

  // --- Report zusammenbauen ---
  uint8_t bits = brBits;
  if (bFwd.state  && !(suppress & BIT_FWD))  bits |= BIT_FWD;
  if (bBack.state && !(suppress & BIT_BACK)) bits |= BIT_BACK;
  if (bits != lastSent) sendReport(bits);

  // --- Akkustand periodisch (Kindle nutzt nur Notifications) ---
  if (now - lastBatSent >= BAT_INTERVAL_MS) {
    hid->setBatteryLevel(readBatteryPercent(), true);
    lastBatSent = now;
  }

  delay(5);
}
