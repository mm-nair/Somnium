#include "Ble.h"

#include <string.h>

#if SOMNIUM_BLE

#include <Arduino.h>
#include <esp_heap_caps.h>

#include "Console.h"
#include "Image.h"
#include "Inputs.h"

// NimBLE-Arduino 1.4.x reads these before its own defaults. Peripheral only,
// one connection, host buffers in PSRAM. The 64 KB USB RX buffer and the
// camera driver already sit in internal RAM; the dream JPEG lives in PSRAM.
#ifndef CONFIG_BT_NIMBLE_MAX_CONNECTIONS
#define CONFIG_BT_NIMBLE_MAX_CONNECTIONS 1
#endif
#ifndef CONFIG_BT_NIMBLE_MAX_BONDS
#define CONFIG_BT_NIMBLE_MAX_BONDS 1
#endif
#ifndef CONFIG_BT_NIMBLE_MAX_CCCDS
#define CONFIG_BT_NIMBLE_MAX_CCCDS 6
#endif
#ifndef CONFIG_BT_NIMBLE_ROLE_CENTRAL_DISABLED
#define CONFIG_BT_NIMBLE_ROLE_CENTRAL_DISABLED
#endif
#ifndef CONFIG_BT_NIMBLE_ROLE_OBSERVER_DISABLED
#define CONFIG_BT_NIMBLE_ROLE_OBSERVER_DISABLED
#endif
#ifndef CONFIG_BT_NIMBLE_ATT_PREFERRED_MTU
#define CONFIG_BT_NIMBLE_ATT_PREFERRED_MTU 185
#endif
#ifndef CONFIG_BT_NIMBLE_MEM_ALLOC_MODE_EXTERNAL
#define CONFIG_BT_NIMBLE_MEM_ALLOC_MODE_EXTERNAL 1
#endif
#ifndef CONFIG_BT_NIMBLE_LOG_LEVEL
#define CONFIG_BT_NIMBLE_LOG_LEVEL 5
#endif
#ifndef CONFIG_NIMBLE_CPP_LOG_LEVEL
#define CONFIG_NIMBLE_CPP_LOG_LEVEL 0
#endif

#include <NimBLEDevice.h>

namespace Somnium {
namespace {

constexpr int kMaxModes = 8;
constexpr uint16_t kPreferredMtu = 185;
constexpr unsigned long kChunkIntervalMs = 40;

Ble::ModeEntry sEntries[kMaxModes];
BleProto::ModeView sViews[kMaxModes];
uint8_t sModeCount = 0;
portMUX_TYPE sModeMux = portMUX_INITIALIZER_UNLOCKED;

volatile uint8_t sGate = 0;
volatile uint8_t sConnected = 0;
volatile uint16_t sMtu = 23;
volatile uint8_t sForceNotify = 0;
volatile uint8_t sPreviewSubscribed = 0;
volatile uint8_t sPreviewRestart = 0;

bool sActive = false;
uint8_t sStatusPack[BleProto::kStatusBytes];
bool sStatusDirty = false;
bool sModesDirty = false;
uint8_t sModeSelected = 0;
uint8_t sDreamPack[BleProto::kDreamBytes];
bool sDreamDirty = false;
BleProto::DreamNotice sDream{};
uint16_t sGeneration = 0;

uint8_t *sThumb = nullptr;
size_t sThumbBytes = 0;
bool sThumbReady = false;
bool sPreviewPending = false;
BleProto::ChunkCursor sCursor{};
unsigned long sLastChunkMs = 0;

NimBLEServer *sServer = nullptr;
NimBLECharacteristic *sModesChr = nullptr;
NimBLECharacteristic *sModeSelectChr = nullptr;
NimBLECharacteristic *sShutterChr = nullptr;
NimBLECharacteristic *sStatusChr = nullptr;
NimBLECharacteristic *sDreamChr = nullptr;
NimBLECharacteristic *sPreviewChr = nullptr;

uint8_t *thumbBuffer() {
  if (sThumb != nullptr) {
    return sThumb;
  }
  const size_t bytes = static_cast<size_t>(BleProto::kPreviewWidth) *
                       static_cast<size_t>(BleProto::kPreviewHeight) * 2;
  sThumb = static_cast<uint8_t *>(
      heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (sThumb == nullptr) {
    sThumb = static_cast<uint8_t *>(malloc(bytes));
  }
  if (sThumb != nullptr) {
    memset(sThumb, 0, bytes);
  }
  return sThumb;
}

void notifyIfSubscribed(NimBLECharacteristic *chr, const uint8_t *data,
                        size_t len) {
  if (chr == nullptr || data == nullptr || len == 0) {
    return;
  }
  chr->setValue(data, len);
  if (sConnected && chr->getSubscribedCount() > 0) {
    chr->notify(data, len, true);
  }
}

void sendAbortIfNeeded() {
  if (sPreviewChr == nullptr || sCursor.sequence == 0 || sCursor.finished) {
    return;
  }
  BleProto::Chunk chunk;
  if (!BleProto::abortPreviewChunk(sCursor.sequence, sMtu, chunk)) {
    return;
  }
  if (sConnected && sPreviewChr->getSubscribedCount() > 0) {
    sPreviewChr->notify(chunk.bytes, chunk.length, true);
  }
  BleProto::resetCursor(sCursor);
}

class ServerCallbacks : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer *server) override {
    sConnected = 1;
    sForceNotify = 1;
    if (server != nullptr && server->getConnectedCount() > 0) {
      const uint16_t mtu = server->getPeerMTU(server->getPeerDevices()[0]);
      if (mtu >= 23) {
        sMtu = mtu;
      }
    }
  }

  void onConnect(NimBLEServer *server, ble_gap_conn_desc *desc) override {
    if (desc != nullptr) {
      const uint16_t mtu = server->getPeerMTU(desc->conn_handle);
      if (mtu >= 23) {
        sMtu = mtu;
      }
    }
    onConnect(server);
  }

  void onDisconnect(NimBLEServer *server) override {
    (void)server;
    sConnected = 0;
    sPreviewSubscribed = 0;
    sMtu = 23;
  }

  void onDisconnect(NimBLEServer *server, ble_gap_conn_desc *desc) override {
    (void)desc;
    onDisconnect(server);
  }

  void onMTUChange(uint16_t mtu, ble_gap_conn_desc *desc) override {
    (void)desc;
    if (mtu >= 23) {
      sMtu = mtu;
    }
  }
};

class ServiceCallbacks : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic *characteristic) override {
    if (characteristic == nullptr) {
      return;
    }
    uint8_t buf[32];
    const NimBLEAttValue value = characteristic->getValue();
    size_t len = value.size();
    if (len > sizeof(buf)) {
      len = sizeof(buf);
    }
    if (len > 0 && value.data() != nullptr) {
      memcpy(buf, value.data(), len);
    }
    const uint8_t gate = sGate;
    if (characteristic == sShutterChr) {
      if ((gate & Ble::kGateProcess) != 0) {
        return;
      }
      if (BleProto::parseShutter(buf, len)) {
        Inputs::postShutter();
      }
      return;
    }
    if (characteristic == sModeSelectChr) {
      if ((gate & Ble::kGateCooking) != 0) {
        return;
      }
      int index = -1;
      portENTER_CRITICAL(&sModeMux);
      index = BleProto::parseModeSelect(buf, len, sViews, sModeCount);
      portEXIT_CRITICAL(&sModeMux);
      if (index >= 0) {
        Inputs::postMode(static_cast<int8_t>(index));
      }
    }
  }

  void onSubscribe(NimBLECharacteristic *characteristic,
                   ble_gap_conn_desc *desc, uint16_t subValue) override {
    (void)desc;
    if (characteristic != sPreviewChr) {
      return;
    }
    sPreviewSubscribed = subValue != 0 ? 1 : 0;
    if (subValue != 0) {
      sPreviewRestart = 1;
    }
  }
};

ServerCallbacks sServerCallbacks;
ServiceCallbacks sServiceCallbacks;

void refreshModesValue() {
  uint8_t packed[200];
  size_t n = 0;
  portENTER_CRITICAL(&sModeMux);
  n = BleProto::packModes(sModeSelected, sViews, sModeCount, packed,
                          sizeof(packed));
  portEXIT_CRITICAL(&sModeMux);
  if (n == 0 || sModesChr == nullptr) {
    return;
  }
  // Read Blob serves the full list at any MTU. A notification only carries
  // the list when it fits; otherwise one byte tells the phone to read.
  sModesChr->setValue(packed, n);
  if (!sConnected || sModesChr->getSubscribedCount() == 0) {
    return;
  }
  const size_t budget = sMtu > 3 ? static_cast<size_t>(sMtu) - 3 : 0;
  if (n <= budget) {
    sModesChr->notify(packed, n, true);
  } else {
    const uint8_t ping = BleProto::kModesUpdated;
    sModesChr->notify(&ping, 1, true);
  }
}

void refreshDreamValue() {
  notifyIfSubscribed(sDreamChr, sDreamPack, BleProto::kDreamBytes);
}

}  // namespace

bool Ble::compiled() { return true; }

bool Ble::begin() {
  if (sActive) {
    return true;
  }
  NimBLEDevice::init("Somnium");
  NimBLEDevice::setMTU(kPreferredMtu);
  sServer = NimBLEDevice::createServer();
  if (sServer == nullptr) {
    return false;
  }
  sServer->setCallbacks(&sServerCallbacks, false);
  sServer->advertiseOnDisconnect(true);

  NimBLEService *service =
      sServer->createService(BleProto::kServiceUuid);
  if (service == nullptr) {
    return false;
  }

  sModesChr = service->createCharacteristic(
      BleProto::kModesUuid, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY,
      200);
  sModeSelectChr = service->createCharacteristic(
      BleProto::kModeSelectUuid,
      NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR, 32);
  sShutterChr = service->createCharacteristic(
      BleProto::kShutterUuid,
      NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR, 8);
  sStatusChr = service->createCharacteristic(
      BleProto::kStatusUuid, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY,
      BleProto::kStatusBytes);
  sDreamChr = service->createCharacteristic(
      BleProto::kDreamUuid, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY,
      BleProto::kDreamBytes);
  sPreviewChr = service->createCharacteristic(
      BleProto::kPreviewUuid, NIMBLE_PROPERTY::NOTIFY, BleProto::kChunkMaxBytes);
  if (sModesChr == nullptr || sModeSelectChr == nullptr ||
      sShutterChr == nullptr || sStatusChr == nullptr || sDreamChr == nullptr ||
      sPreviewChr == nullptr) {
    return false;
  }

  sModeSelectChr->setCallbacks(&sServiceCallbacks);
  sShutterChr->setCallbacks(&sServiceCallbacks);
  sPreviewChr->setCallbacks(&sServiceCallbacks);

  BleProto::Status idle{};
  idle.batteryPct = BleProto::kBatteryAbsent;
  const size_t statusN =
      BleProto::packStatus(idle, sStatusPack, sizeof(sStatusPack));
  sStatusChr->setValue(sStatusPack, statusN);
  memset(&sDream, 0, sizeof(sDream));
  const size_t dreamN =
      BleProto::packDream(sDream, sDreamPack, sizeof(sDreamPack));
  sDreamChr->setValue(sDreamPack, dreamN);
  uint8_t modes[2] = {0, 0};
  sModesChr->setValue(modes, sizeof(modes));

  service->start();
  NimBLEAdvertising *advertising = NimBLEDevice::getAdvertising();
  advertising->addServiceUUID(BleProto::kServiceUuid);
  advertising->setScanResponse(true);
  advertising->setName("Somnium");
  advertising->start();
  sActive = true;
  return true;
}

bool Ble::active() { return sActive; }

void Ble::setGate(uint8_t gate) { sGate = gate; }

void Ble::setModes(const ModeEntry *modes, uint8_t count) {
  if (count > kMaxModes) {
    count = kMaxModes;
  }
  portENTER_CRITICAL(&sModeMux);
  sModeCount = count;
  for (uint8_t i = 0; i < kMaxModes; ++i) {
    memset(&sEntries[i], 0, sizeof(sEntries[i]));
    sViews[i].id = sEntries[i].id;
    sViews[i].label = sEntries[i].label;
  }
  for (uint8_t i = 0; i < count; ++i) {
    if (modes != nullptr) {
      memcpy(sEntries[i].id, modes[i].id, sizeof(sEntries[i].id));
      memcpy(sEntries[i].label, modes[i].label, sizeof(sEntries[i].label));
      sEntries[i].id[sizeof(sEntries[i].id) - 1] = '\0';
      sEntries[i].label[sizeof(sEntries[i].label) - 1] = '\0';
    }
  }
  portEXIT_CRITICAL(&sModeMux);
  sModesDirty = true;
}

void Ble::setStatus(const BleProto::Status &status) {
  uint8_t packed[BleProto::kStatusBytes];
  if (BleProto::packStatus(status, packed, sizeof(packed)) !=
      BleProto::kStatusBytes) {
    return;
  }
  if (memcmp(packed, sStatusPack, BleProto::kStatusBytes) != 0) {
    memcpy(sStatusPack, packed, BleProto::kStatusBytes);
    sStatusDirty = true;
  }
  if (status.modeIndex != sModeSelected) {
    sModeSelected = status.modeIndex;
    sModesDirty = true;
  }
}

void Ble::offerDream(const uint8_t *jpeg, size_t jpegLen, uint8_t modeIndex) {
  if (!sActive) {
    return;
  }
  sendAbortIfNeeded();
  sGeneration = static_cast<uint16_t>(sGeneration + 1);
  uint8_t *thumb = thumbBuffer();
  const size_t thumbBytes = static_cast<size_t>(BleProto::kPreviewWidth) *
                            static_cast<size_t>(BleProto::kPreviewHeight) * 2;
  bool decoded = false;
  if (thumb != nullptr && jpeg != nullptr && jpegLen > 0) {
    decoded = Image::decodeRgb565(
        jpeg, jpegLen, reinterpret_cast<uint16_t *>(thumb),
        static_cast<int16_t>(BleProto::kPreviewWidth),
        static_cast<int16_t>(BleProto::kPreviewHeight));
  }
  sThumbReady = decoded;
  sThumbBytes = decoded ? thumbBytes : 0;
  memset(&sDream, 0, sizeof(sDream));
  sDream.state = BleProto::kDreamReady;
  sDream.modeIndex = modeIndex;
  sDream.dreamBytes = static_cast<uint32_t>(jpegLen);
  sDream.generation = sGeneration;
  if (decoded) {
    sDream.thumbWidth = BleProto::kPreviewWidth;
    sDream.thumbHeight = BleProto::kPreviewHeight;
    sDream.format = BleProto::kFormatRgb565;
    sDream.thumbBytes = static_cast<uint32_t>(thumbBytes);
  }
  BleProto::packDream(sDream, sDreamPack, sizeof(sDreamPack));
  sDreamDirty = true;
  BleProto::resetCursor(sCursor);
  sPreviewPending = decoded;
  sLastChunkMs = 0;
  Console::printf("BLE dream %u B thumb=%u\n",
                  static_cast<unsigned>(jpegLen),
                  static_cast<unsigned>(sThumbBytes));
}

void Ble::noteDreamFailed(uint8_t modeIndex) {
  if (!sActive) {
    return;
  }
  sendAbortIfNeeded();
  sGeneration = static_cast<uint16_t>(sGeneration + 1);
  sThumbReady = false;
  sThumbBytes = 0;
  sPreviewPending = false;
  BleProto::resetCursor(sCursor);
  memset(&sDream, 0, sizeof(sDream));
  sDream.state = BleProto::kDreamFailed;
  sDream.modeIndex = modeIndex;
  sDream.generation = sGeneration;
  BleProto::packDream(sDream, sDreamPack, sizeof(sDreamPack));
  sDreamDirty = true;
}

void Ble::poll() {
  if (!sActive) {
    return;
  }
  if (sForceNotify) {
    sForceNotify = 0;
    sStatusDirty = true;
    sModesDirty = true;
    sDreamDirty = true;
  }
  if (sModesDirty) {
    sModesDirty = false;
    refreshModesValue();
  }
  if (sDreamDirty) {
    sDreamDirty = false;
    refreshDreamValue();
  }
  if (sStatusDirty) {
    sStatusDirty = false;
    notifyIfSubscribed(sStatusChr, sStatusPack, BleProto::kStatusBytes);
  }

  if (sPreviewRestart) {
    sPreviewRestart = 0;
    if (sThumbReady) {
      sendAbortIfNeeded();
      BleProto::resetCursor(sCursor);
      sPreviewPending = true;
      sLastChunkMs = 0;
    }
  }
  if (!sPreviewPending || !sThumbReady || sThumb == nullptr || !sConnected ||
      !sPreviewSubscribed) {
    return;
  }
  const unsigned long now = millis();
  if (sLastChunkMs != 0 && now - sLastChunkMs < kChunkIntervalMs) {
    return;
  }
  BleProto::Chunk chunk;
  if (!BleProto::nextPreviewChunk(
          sThumb, sThumbBytes, BleProto::kPreviewWidth, BleProto::kPreviewHeight,
          BleProto::kFormatRgb565, sMtu, sCursor, chunk)) {
    sPreviewPending = false;
    return;
  }
  sPreviewChr->notify(chunk.bytes, chunk.length, true);
  sLastChunkMs = now;
  if (sCursor.finished) {
    sPreviewPending = false;
  }
}

}  // namespace Somnium

#else

namespace Somnium {

bool Ble::compiled() { return false; }
bool Ble::begin() { return false; }
bool Ble::active() { return false; }
void Ble::poll() {}
void Ble::setGate(uint8_t) {}
void Ble::setModes(const ModeEntry *, uint8_t) {}
void Ble::setStatus(const BleProto::Status &) {}
void Ble::offerDream(const uint8_t *, size_t, uint8_t) {}
void Ble::noteDreamFailed(uint8_t) {}

}  // namespace Somnium

#endif
