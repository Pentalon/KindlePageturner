// KindlePageturner v0.1.0
// Seeed XIAO ESP32-S3 – BLE HID page turner, Kindle-Click-kompatibel (Kindle FW >= 5.20.1)
// Arduino Core esp32:esp32 3.3.x, Board "XIAO_ESP32S3"
// Library: NimBLE-Arduino 2.5.0 (h2zero)
//
// v0.1: BLE HID, Tasten, Sleep-Geste, Akkuanzeige, Wake-Puffer. Noch KEIN Deep Sleep (Test an USB).

#include <Arduino.h>
#include <NimBLEDevice.h>
#include <NimBLEHIDDevice.h>

// ---------------- Pins ----------------
#define PIN_FWD     1    // D0  obere Taste   -> Seite vor   (KEY_PAGEDOWN)
#define PIN_BACK    2    // D1  untere Taste  -> Seite zurück (KEY_PAGEUP)
#define PIN_BR_UP   5    // D4  Seite oben    -> Helligkeit +
#define PIN_BR_DN   6    // D5  Seite unten   -> Helligkeit -
#define PIN_BAT     9    // D10 Akkuspannung über 2x 220k Teiler
#define PIN_LED     21   // User-LED, active low

// ---------------- Timing ----------------
#define DEBOUNCE_MS        20
#define SLEEP_CHORD_MS     1000   // beide Seitentasten halten -> KEY_POWER
#define READY_DELAY_MS     2500   // nach Connect warten, bis Kindle HID geöffnet hat
#define BAT_INTERVAL_MS    60000  // Akkustand regelmäßig neu senden
#define ADV_INTERVAL_UNITS 32     // 32 * 0.625 ms = 20 ms

#define FW_VERSION "0.1.0"

// ---------------- Report Bits ----------------
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

// ---------------- BLE ----------------
static NimBLEServer*         server   = nullptr;
static NimBLEHIDDevice*      hid      = nullptr;
static NimBLECharacteristic* inReport = nullptr;

static volatile bool     connected   = false;
static volatile uint32_t connectedAt = 0;

class ServerCallbacks : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer* s, NimBLEConnInfo& info) override {
    connected   = true;
    connectedAt = millis();
    Serial.printf("Connected: %s\n", info.getAddress().toString().c_str());
  }
  void onDisconnect(NimBLEServer* s, NimBLEConnInfo& info, int reason) override {
    connected = false;
    Serial.printf("Disconnected, reason %d\n", reason);
    NimBLEDevice::startAdvertising();
  }
  void onAuthenticationComplete(NimBLEConnInfo& info) override {
    Serial.printf("Auth: encrypted=%d bonded=%d\n", info.isEncrypted(), info.isBonded());
  }
};

// ---------------- Tasten ----------------
struct Button {
  uint8_t  pin;
  bool     state;    // entprellt, true = gedrückt
  bool     raw;
  uint32_t changedAt;
};

static Button bFwd  = {PIN_FWD,   false, false, 0};
static Button bBack = {PIN_BACK,  false, false, 0};
static Button bUp   = {PIN_BR_UP, false, false, 0};
static Button bDn   = {PIN_BR_DN, false, false, 0};

// liefert true bei neuem Druck (Flanke)
static bool updateButton(Button& b, uint32_t now) {
  bool r = (digitalRead(b.pin) == LOW);
  if (r != b.raw) { b.raw = r; b.changedAt = now; }
  if ((now - b.changedAt) >= DEBOUNCE_MS && b.state != b.raw) {
    b.state = b.raw;
    return b.state;
  }
  return false;
}

// ---------------- Akku ----------------
static uint8_t readBatteryPercent() {
  uint32_t mv = 0;
  for (int i = 0; i < 16; i++) mv += analogReadMilliVolts(PIN_BAT);
  mv = (mv / 16) * 2;                       // Teiler 1:2
  if (mv >= 4150) return 100;
  if (mv <= 3300) return 0;
  return (uint8_t)((mv - 3300) * 100 / (4150 - 3300));
}

// ---------------- Senden ----------------
static uint8_t lastSent = 0;

static void sendReport(uint8_t bits) {
  inReport->setValue(&bits, 1);
  inReport->notify();
  lastSent = bits;
}

static void tap(uint8_t bit) {
  sendReport(lastSent | bit);
  delay(30);
  sendReport(lastSent & ~bit);
}

// ---------------- Setup ----------------
void setup() {
  Serial.begin(115200);

  pinMode(PIN_FWD,   INPUT_PULLUP);
  pinMode(PIN_BACK,  INPUT_PULLUP);
  pinMode(PIN_BR_UP, INPUT_PULLUP);
  pinMode(PIN_BR_DN, INPUT_PULLUP);
  pinMode(PIN_LED,   OUTPUT);
  digitalWrite(PIN_LED, HIGH);              // aus

  NimBLEDevice::init("Kindle Pageturner");
  // Just Works, Bonding, Secure Connections, kein MITM.
  // Keine eigene Security Request senden – der Kindle startet das Pairing selbst.
  NimBLEDevice::setSecurityAuth(true, false, true);
  NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);

  server = NimBLEDevice::createServer();
  server->setCallbacks(new ServerCallbacks());

  hid = new NimBLEHIDDevice(server);
  hid->setManufacturer("DIY");
  hid->setPnp(0x01, 0x0171, 0x0443, 0x0100);   // BT SIG, VID 0x0171, PID 0x0443, Version 1.00
  hid->setHidInfo(0x00, 0x02);                  // Country 0, normally connectable
  hid->setReportMap((uint8_t*)reportMap, sizeof(reportMap));
  inReport = hid->getInputReport(1);

  // Firmware Revision 0x2A26 – wird am Kindle unter Zubehör angezeigt
  NimBLECharacteristic* fwRev = hid->getDeviceInfoService()->createCharacteristic(
      (uint16_t)0x2A26, NIMBLE_PROPERTY::READ);
  fwRev->setValue(FW_VERSION);

  hid->setBatteryLevel(readBatteryPercent());
  hid->startServices();

  // Advertising: Flags + Manufacturer Data 71 01 04 43 im ADV_IND, Name in Scan Response
  NimBLEAdvertisementData advData;
  advData.setFlags(BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP);
  const uint8_t mfg[] = {0x71, 0x01, 0x04, 0x43};   // VID LE, PID BE
  advData.setManufacturerData(std::string((const char*)mfg, sizeof(mfg)));
  advData.addServiceUUID(NimBLEUUID((uint16_t)0x1812));

  NimBLEAdvertisementData scanData;
  scanData.setName("Kindle Pageturner");

  NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
  adv->setAdvertisementData(advData);
  adv->setScanResponseData(scanData);
  adv->enableScanResponse(true);
  adv->setMinInterval(ADV_INTERVAL_UNITS);
  adv->setMaxInterval(ADV_INTERVAL_UNITS);
  adv->start();

  Serial.println("KindlePageturner " FW_VERSION " advertising");
}

// ---------------- Loop ----------------
static uint8_t  pending      = 0;      // gepufferter Seitenwechsel bis Kindle bereit
static bool     wasReady     = false;
static bool     chordLock    = false;
static bool     chordFired   = false;
static uint32_t chordStart   = 0;
static uint32_t lastBatSent  = 0;
static uint32_t lastBlink    = 0;

void loop() {
  uint32_t now = millis();

  bool fwdPressed  = updateButton(bFwd,  now);
  bool backPressed = updateButton(bBack, now);
  updateButton(bUp, now);
  updateButton(bDn, now);

  bool ready = connected && (now - connectedAt) >= READY_DELAY_MS;

  // --- LED: blinkt beim Advertising, aus wenn verbunden ---
  if (!connected) {
    if (now - lastBlink >= 1000) lastBlink = now;
    digitalWrite(PIN_LED, (now - lastBlink) < 80 ? LOW : HIGH);
  } else {
    digitalWrite(PIN_LED, HIGH);
  }

  // --- Nicht bereit: letzten Seitenwechsel puffern ---
  if (!ready) {
    if (fwdPressed)  pending = BIT_FWD;
    if (backPressed) pending = BIT_BACK;
    wasReady = false;
    delay(5);
    return;
  }

  // --- gerade bereit geworden ---
  if (!wasReady) {
    wasReady = true;
    lastSent = 0;
    sendReport(0);
    hid->setBatteryLevel(readBatteryPercent(), true);
    lastBatSent = now;
    if (pending) { tap(pending); pending = 0; }
  }

  // --- Sleep-Geste: beide Seitentasten 1 s ---
  if (bUp.state && bDn.state) {
    if (!chordLock) { chordLock = true; chordFired = false; chordStart = now; }
    if (!chordFired && (now - chordStart) >= SLEEP_CHORD_MS) {
      chordFired = true;
      sendReport(lastSent & ~(BIT_BR_UP | BIT_BR_DN));  // Helligkeit sicher loslassen
      tap(BIT_POWER);
      Serial.println("KEY_POWER");
    }
  } else if (!bUp.state && !bDn.state) {
    chordLock = false;
  }

  // --- Report zusammenbauen ---
  uint8_t bits = 0;
  if (bFwd.state)  bits |= BIT_FWD;
  if (bBack.state) bits |= BIT_BACK;
  if (!chordLock) {                       // während der Geste keine Helligkeit
    if (bUp.state) bits |= BIT_BR_UP;
    if (bDn.state) bits |= BIT_BR_DN;
  }
  if (bits != lastSent) sendReport(bits);

  // --- Akkustand periodisch ---
  if (now - lastBatSent >= BAT_INTERVAL_MS) {
    hid->setBatteryLevel(readBatteryPercent(), true);
    lastBatSent = now;
  }

  delay(5);
}
