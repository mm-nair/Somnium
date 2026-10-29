#include "Ble.h"

#include <string.h>

#include <BLE2902.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <Preferences.h>
#include <esp_gap_ble_api.h>
#include <sys/time.h>

#include "Config.h"
#include "Console.h"
#include "Power.h"

namespace Somnium {
namespace {

constexpr char kService[] = "6f4a0001-8c3e-4b7d-9a1e-5d2c7b6e1040";
constexpr char kStatus[] = "6f4a0002-8c3e-4b7d-9a1e-5d2c7b6e1040";
constexpr char kControl[] = "6f4a0003-8c3e-4b7d-9a1e-5d2c7b6e1040";
constexpr char kPreview[] = "6f4a0004-8c3e-4b7d-9a1e-5d2c7b6e1040";
constexpr char kSession[] = "6f4a0005-8c3e-4b7d-9a1e-5d2c7b6e1040";

struct Line {
  char text[48];
};

BLEServer *sServer = nullptr;
BLECharacteristic *sStatus = nullptr;
BLECharacteristic *sPreviewChar = nullptr;
BLECharacteristic *sSession = nullptr;
BLECharacteristic *sBatteryLevel = nullptr;
QueueHandle_t sLines = nullptr;

bool sUp = false;
bool sConnected = false;
bool sPaired = false;
bool sPairPending = false;
bool sBonded = false;
uint8_t sBond[6] = {};
uint8_t sPeer[6] = {};

char sMode[16] = "popcomic";
int sQueue = 0;
uint32_t sPreviewId = 0;
uint32_t sPreviewCrc = 0;
size_t sPreviewLen = 0;
size_t sPreviewOff = 0;
uint8_t *sPreviewBytes = nullptr;

Ble::Pending sCmds[4];
uint8_t sCmdHead = 0;
uint8_t sCmdTail = 0;
uint32_t sPulls[4] = {};
uint8_t sPullHead = 0;
uint8_t sPullTail = 0;

bool sameAddr(const uint8_t *a, const uint8_t *b) { return memcmp(a, b, 6) == 0; }

void loadBond() {
  Preferences prefs;
  prefs.begin("somnium", true);
  sBonded = prefs.getBytes("peer", sBond, sizeof(sBond)) == sizeof(sBond);
  prefs.end();
}

void storeBond(const uint8_t *addr) {
  memcpy(sBond, addr, sizeof(sBond));
  sBonded = true;
  Preferences prefs;
  prefs.begin("somnium", false);
  prefs.putBytes("peer", sBond, sizeof(sBond));
  prefs.end();
}

void clearBond() {
  memset(sBond, 0, sizeof(sBond));
  sBonded = false;
  sPaired = false;
  Preferences prefs;
  prefs.begin("somnium", false);
  prefs.remove("peer");
  prefs.end();
}

void pushCmd(Ble::Command command, const char *arg) {
  const uint8_t next = static_cast<uint8_t>((sCmdHead + 1) % 4);
  if (next == sCmdTail) {
    return;
  }
  sCmds[sCmdHead].command = command;
  sCmds[sCmdHead].arg[0] = '\0';
  if (arg != nullptr) {
    strncpy(sCmds[sCmdHead].arg, arg, sizeof(sCmds[sCmdHead].arg) - 1);
    sCmds[sCmdHead].arg[sizeof(sCmds[sCmdHead].arg) - 1] = '\0';
  }
  sCmdHead = next;
}

void pushPull(uint32_t id) {
  const uint8_t next = static_cast<uint8_t>((sPullHead + 1) % 4);
  if (next == sPullTail) {
    return;
  }
  sPulls[sPullHead] = id;
  sPullHead = next;
}

void publishStatus() {
  if (sStatus == nullptr) {
    return;
  }
  char preview[40] = "-";
  if (sPreviewBytes != nullptr) {
    snprintf(preview, sizeof(preview), "%08lx,%u,%08lx",
             static_cast<unsigned long>(sPreviewId),
             static_cast<unsigned>(sPreviewLen),
             static_cast<unsigned long>(sPreviewCrc));
  }
  int packMv = -1;
  const int pct = Power::batteryPercent();
  Power::batteryMillivolts(packMv);
  char batt[16] = "batt=? mv=?";
  if (pct >= 0) {
    snprintf(batt, sizeof(batt), "batt=%d mv=%d", pct, packMv);
  }
  char body[180];
  snprintf(body, sizeof(body),
           "paired=%d conn=%d queue=%d %s chg=? mode=%s preview=%s",
           sPaired ? 1 : 0, sConnected ? 1 : 0, sQueue, batt, sMode, preview);
  sStatus->setValue(reinterpret_cast<uint8_t *>(body), strlen(body));
  if (sConnected) {
    sStatus->notify();
  }
  if (sBatteryLevel != nullptr && pct >= 0) {
    uint8_t level = static_cast<uint8_t>(pct);
    sBatteryLevel->setValue(&level, 1);
    if (sConnected) {
      sBatteryLevel->notify();
    }
  }
}

void applyClock(unsigned long epoch) {
  timeval tv{};
  tv.tv_sec = static_cast<time_t>(epoch);
  tv.tv_usec = 0;
  settimeofday(&tv, nullptr);
  Console::printf("TIME synced %lu\n", epoch);
}

void handleLine(char *text) {
  while (*text == ' ' || *text == '\r' || *text == '\n') {
    ++text;
  }
  size_t n = strlen(text);
  while (n > 0 && (text[n - 1] == '\r' || text[n - 1] == '\n' || text[n - 1] == ' ')) {
    text[--n] = '\0';
  }
  if (n == 0) {
    return;
  }
  for (size_t i = 0; text[i] != '\0' && text[i] != ' '; ++i) {
    if (text[i] >= 'a' && text[i] <= 'z') {
      text[i] = static_cast<char>(text[i] - 'a' + 'A');
    }
  }
  if (strcmp(text, "PAIR") == 0) {
    if (sConnected && !sPaired) {
      sPairPending = true;
    }
    return;
  }
  if (!sPaired) {
    Console::printf("BLE ignore %s\n", text);
    return;
  }
  if (strcmp(text, "SHUTTER") == 0) {
    pushCmd(Ble::Command::Shutter, nullptr);
    return;
  }
  if (strcmp(text, "SELECT") == 0) {
    pushCmd(Ble::Command::Select, nullptr);
    return;
  }
  if (strcmp(text, "FORGET") == 0) {
    clearBond();
    sPairPending = sConnected;
    publishStatus();
    Console::println("BLE forget");
    return;
  }
  unsigned long epoch = 0;
  if (sscanf(text, "TIME %lu", &epoch) == 1 && epoch > 1700000000UL) {
    applyClock(epoch);
    return;
  }
  char mode[16] = {};
  if (sscanf(text, "MODE %15s", mode) == 1) {
    pushCmd(Ble::Command::Mode, mode);
    return;
  }
  unsigned id = 0;
  if (sscanf(text, "PULL %8x", &id) == 1 || sscanf(text, "PULL %x", &id) == 1) {
    pushPull(id);
  }
}

class ControlCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *characteristic) override {
    if (sLines == nullptr) {
      return;
    }
    std::string value = characteristic->getValue();
    if (value.empty()) {
      return;
    }
    Line line{};
    size_t n = value.size();
    if (n >= sizeof(line.text)) {
      n = sizeof(line.text) - 1;
    }
    memcpy(line.text, value.data(), n);
    line.text[n] = '\0';
    xQueueSend(sLines, &line, 0);
  }
};

class ServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer *server, esp_ble_gatts_cb_param_t *param) override {
    (void)server;
    memcpy(sPeer, param->connect.remote_bda, sizeof(sPeer));
    sConnected = true;
    sPaired = sBonded && sameAddr(sPeer, sBond);
    sPairPending = !sPaired;
    Console::printf("BLE connect paired=%d\n", sPaired ? 1 : 0);
    publishStatus();
  }

  void onDisconnect(BLEServer *server) override {
    (void)server;
    sConnected = false;
    sPaired = false;
    sPairPending = false;
    Console::println("BLE disconnect");
    BLEDevice::startAdvertising();
  }
};

void startStack() {
  if (sLines == nullptr) {
    sLines = xQueueCreate(8, sizeof(Line));
  }
  loadBond();
  BLEDevice::init(Defaults::kBleName);
  BLEDevice::setMTU(185);
  static ServerCallbacks serverCallbacks;
  static ControlCallbacks controlCallbacks;
  sServer = BLEDevice::createServer();
  sServer->setCallbacks(&serverCallbacks);
  BLEService *service = sServer->createService(kService);

  sStatus = service->createCharacteristic(
      kStatus, BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);
  sStatus->addDescriptor(new BLE2902());

  BLECharacteristic *control = service->createCharacteristic(
      kControl, BLECharacteristic::PROPERTY_WRITE);
  control->setCallbacks(&controlCallbacks);

  sPreviewChar = service->createCharacteristic(
      kPreview, BLECharacteristic::PROPERTY_NOTIFY);
  sPreviewChar->addDescriptor(new BLE2902());

  sSession = service->createCharacteristic(
      kSession, BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);
  sSession->addDescriptor(new BLE2902());

  service->start();

  // Standard Battery Service. Level is a uint8 percent, same number as batt=.
  BLEService *battery = sServer->createService(BLEUUID(static_cast<uint16_t>(0x180F)));
  sBatteryLevel = battery->createCharacteristic(
      BLEUUID(static_cast<uint16_t>(0x2A19)),
      BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);
  sBatteryLevel->addDescriptor(new BLE2902());
  battery->start();

  BLEAdvertising *advertising = BLEDevice::getAdvertising();
  advertising->addServiceUUID(kService);
  advertising->addServiceUUID(BLEUUID(static_cast<uint16_t>(0x180F)));
  advertising->setScanResponse(true);
  BLEDevice::startAdvertising();
  sUp = true;
  publishStatus();
  Console::println("BLE advertise Somnium");
}

void pumpPreview() {
  if (!sPaired || sPreviewChar == nullptr || sPreviewBytes == nullptr ||
      sPreviewOff >= sPreviewLen) {
    return;
  }
  // Default ATT payload is 20 bytes until the phone raises the MTU.
  // 19 bytes leaves the stack a byte of headroom on a 23-byte MTU.
  for (int n = 0; n < 4 && sPreviewOff < sPreviewLen; ++n) {
    uint8_t packet[19] = {};
    const size_t chunk = sPreviewLen - sPreviewOff > 11 ? 11 : sPreviewLen - sPreviewOff;
    const uint16_t seq = static_cast<uint16_t>(sPreviewOff / 11);
    packet[0] = 0;
    if (sPreviewOff == 0) {
      packet[0] |= 0x01;
    }
    if (sPreviewOff + chunk >= sPreviewLen) {
      packet[0] |= 0x02;
    }
    packet[1] = static_cast<uint8_t>(seq & 0xff);
    packet[2] = static_cast<uint8_t>((seq >> 8) & 0xff);
    packet[3] = static_cast<uint8_t>(sPreviewId & 0xff);
    packet[4] = static_cast<uint8_t>((sPreviewId >> 8) & 0xff);
    packet[5] = static_cast<uint8_t>((sPreviewId >> 16) & 0xff);
    packet[6] = static_cast<uint8_t>((sPreviewId >> 24) & 0xff);
    packet[7] = static_cast<uint8_t>(chunk);
    memcpy(packet + 8, sPreviewBytes + sPreviewOff, chunk);
    sPreviewChar->setValue(packet, sizeof(packet));
    sPreviewChar->notify();
    sPreviewOff += chunk;
  }
  if (sPreviewOff >= sPreviewLen) {
    free(sPreviewBytes);
    sPreviewBytes = nullptr;
    sPreviewLen = 0;
    sPreviewOff = 0;
  }
}

}  // namespace

void Ble::begin() {
  if (sUp) {
    return;
  }
  startStack();
}

void Ble::poll() {
  if (!sUp || sLines == nullptr) {
    return;
  }
  Line line;
  while (xQueueReceive(sLines, &line, 0) == pdTRUE) {
    handleLine(line.text);
  }
  pumpPreview();
  static unsigned long sStatusAt = 0;
  const unsigned long now = millis();
  if (sConnected && now - sStatusAt > 2000) {
    sStatusAt = now;
    publishStatus();
  }
}

void Ble::suspend() {
  if (!sUp) {
    return;
  }
  BLEDevice::deinit(true);
  sServer = nullptr;
  sStatus = nullptr;
  sPreviewChar = nullptr;
  sSession = nullptr;
  sBatteryLevel = nullptr;
  sUp = false;
  sConnected = false;
  sPaired = false;
  sPairPending = false;
  Console::println("BLE off");
}

void Ble::resume() { begin(); }

bool Ble::connected() { return sConnected; }

bool Ble::paired() { return sPaired; }

bool Ble::pairPending() { return sPairPending; }

void Ble::acceptPair() {
  if (!sConnected) {
    return;
  }
  storeBond(sPeer);
  sPaired = true;
  sPairPending = false;
  publishStatus();
  Console::println("BLE paired");
}

bool Ble::previewBusy() {
  return sPreviewBytes != nullptr && sPreviewOff < sPreviewLen;
}

void Ble::setStatusContext(const char *modeId, int queueDepth) {
  if (modeId != nullptr && modeId[0] != '\0') {
    strncpy(sMode, modeId, sizeof(sMode) - 1);
    sMode[sizeof(sMode) - 1] = '\0';
  }
  sQueue = queueDepth;
}

void Ble::startPreview(uint32_t photoId, const uint8_t *jpeg, size_t length, uint32_t crc) {
  free(sPreviewBytes);
  sPreviewBytes = nullptr;
  sPreviewOff = 0;
  sPreviewLen = 0;
  sPreviewId = photoId;
  sPreviewCrc = crc;
  if (jpeg == nullptr || length == 0 || !sPaired) {
    publishStatus();
    return;
  }
  sPreviewBytes = static_cast<uint8_t *>(malloc(length));
  if (sPreviewBytes == nullptr) {
    publishStatus();
    return;
  }
  memcpy(sPreviewBytes, jpeg, length);
  sPreviewLen = length;
  publishStatus();
  Console::printf("BLE preview %08lx %u B\n", static_cast<unsigned long>(photoId),
                  static_cast<unsigned>(length));
}

void Ble::notifySession(const char *ssid, const char *password, uint32_t photoId) {
  if (sSession == nullptr || ssid == nullptr || password == nullptr) {
    return;
  }
  const char *suffix = ssid;
  const size_t prefix = strlen(Defaults::kSoftApPrefix);
  if (strncmp(ssid, Defaults::kSoftApPrefix, prefix) == 0) {
    suffix = ssid + prefix;
  }
  char body[96];
  snprintf(body, sizeof(body), "AP %s %s id=%08lx host=192.168.4.1 port=80",
           suffix, password, static_cast<unsigned long>(photoId));
  sSession->setValue(reinterpret_cast<uint8_t *>(body), strlen(body));
  if (sConnected && sPaired) {
    sSession->notify();
  }
  Console::printf("BLE session %s\n", body);
}

bool Ble::takeCommand(Pending &out) {
  if (sCmdTail == sCmdHead) {
    return false;
  }
  out = sCmds[sCmdTail];
  sCmdTail = static_cast<uint8_t>((sCmdTail + 1) % 4);
  return true;
}

bool Ble::takePull(uint32_t &photoId) {
  if (sPullTail == sPullHead) {
    return false;
  }
  photoId = sPulls[sPullTail];
  sPullTail = static_cast<uint8_t>((sPullTail + 1) % 4);
  return true;
}

}  // namespace Somnium
