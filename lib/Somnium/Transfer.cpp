#include "Transfer.h"

#include <string.h>
#include <strings.h>

#include <SD.h>
#include <SPI.h>
#include <WiFi.h>
#include <esp_mac.h>
#include <esp_rom_crc.h>
#include <img_converters.h>

#include "Ble.h"
#include "Config.h"
#include "Console.h"
#include "Image.h"
#include "Power.h"
#include "Storage.h"

namespace Somnium {
namespace {

constexpr int kQueueMax = 16;
constexpr size_t kMaxPut = 2500000;

struct Item {
  uint32_t id = 0;
  char style[16] = {};
  char orig[72] = {};
  char dream[72] = {};
  uint32_t crc = 0;
  size_t length = 0;
  bool haveDream = false;
  bool acked = false;
  bool freshDream = false;
};

enum class Phase : uint8_t { Idle, Headers, Send, Recv };

Item sItems[kQueueMax];
int sCount = 0;
char sSsid[20] = {};
char sPass[9] = {};
bool sRadio = false;
bool sHold = false;
unsigned long sActivity = 0;
WiFiServer sServer(Defaults::kLinkPort);
WiFiClient sClient;
Phase sPhase = Phase::Idle;
char sHead[640];
size_t sHeadLen = 0;
unsigned long sPhaseAt = 0;
File sFile;
size_t sSendPos = 0;
size_t sSendEnd = 0;
size_t sRecvGot = 0;
size_t sRecvNeed = 0;
uint32_t sPutId = 0;

Item *findItem(uint32_t id) {
  for (int i = 0; i < sCount; ++i) {
    if (sItems[i].id == id) {
      return &sItems[i];
    }
  }
  return nullptr;
}

void writeMeta(const Item &item) {
  char path[40];
  snprintf(path, sizeof(path), "/somnium/queue/%08lx.meta",
           static_cast<unsigned long>(item.id));
  char body[240];
  snprintf(body, sizeof(body),
           "id=%08lx\nstyle=%s\ncrc=%08lx\nlen=%u\norig=%s\ndream=%s\nhaveDream=%d\nacked=%d\n",
           static_cast<unsigned long>(item.id), item.style,
           static_cast<unsigned long>(item.crc), static_cast<unsigned>(item.length),
           item.orig, item.dream, item.haveDream ? 1 : 0, item.acked ? 1 : 0);
  if (!Storage::ready()) {
    return;
  }
  SD.remove(path);
  File file = SD.open(path, FILE_WRITE);
  if (!file) {
    return;
  }
  file.print(body);
  file.close();
}

bool addItem(const Item &item) {
  Item *existing = findItem(item.id);
  if (existing != nullptr) {
    *existing = item;
    writeMeta(*existing);
    return true;
  }
  if (sCount >= kQueueMax) {
    memmove(&sItems[0], &sItems[1], sizeof(Item) * (kQueueMax - 1));
    sCount = kQueueMax - 1;
  }
  sItems[sCount++] = item;
  writeMeta(item);
  return true;
}

void loadMetaFile(File &file) {
  char body[240] = {};
  const size_t n = file.read(reinterpret_cast<uint8_t *>(body), sizeof(body) - 1);
  body[n] = '\0';
  Item item;
  unsigned id = 0;
  unsigned crc = 0;
  unsigned len = 0;
  int have = 0;
  int acked = 0;
  if (sscanf(body, "id=%8x\nstyle=%15s\ncrc=%8x\nlen=%u\norig=%71s\ndream=%71s\nhaveDream=%d\nacked=%d",
             &id, item.style, &crc, &len, item.orig, item.dream, &have, &acked) < 6) {
    return;
  }
  item.id = id;
  item.crc = crc;
  item.length = len;
  item.haveDream = have != 0;
  item.acked = acked != 0;
  if (findItem(item.id) == nullptr && sCount < kQueueMax) {
    sItems[sCount++] = item;
  }
}

void loadQueue() {
  sCount = 0;
  if (!Storage::ready()) {
    return;
  }
  File dir = SD.open("/somnium/queue");
  if (!dir || !dir.isDirectory()) {
    return;
  }
  File entry = dir.openNextFile();
  while (entry) {
    const char *name = entry.name();
    if (name != nullptr && strstr(name, ".meta") != nullptr && !entry.isDirectory()) {
      loadMetaFile(entry);
    }
    entry.close();
    entry = dir.openNextFile();
  }
  dir.close();
  Console::printf("QUEUE %d\n", sCount);
}

void makePassword() {
  static const char kAlphabet[] = "abcdefghijkmnpqrstuvwxyz23456789";
  for (int i = 0; i < 8; ++i) {
    sPass[i] = kAlphabet[esp_random() % (sizeof(kAlphabet) - 1)];
  }
  sPass[8] = '\0';
}

void closeHttp() {
  if (sFile) {
    sFile.close();
  }
  if (sClient) {
    sClient.stop();
  }
  sPhase = Phase::Idle;
  sHeadLen = 0;
  sRecvGot = 0;
  sRecvNeed = 0;
}

bool startRadio(uint32_t photoId) {
  if (!sRadio) {
    makePassword();
  }
  // Tell the phone before the radio mode change. Bringing up the AP can
  // stall the BLE notify on some controllers.
  Ble::notifySession(sSsid, sPass, photoId);
  if (sRadio) {
    sActivity = millis();
    return true;
  }
  WiFi.persistent(false);
  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(IPAddress(192, 168, 4, 1), IPAddress(192, 168, 4, 1),
                    IPAddress(255, 255, 255, 0));
  if (!WiFi.softAP(sSsid, sPass, 6, 0, 1)) {
    Console::println("ERR softap");
    WiFi.mode(WIFI_OFF);
    return false;
  }
  sServer.begin();
  sServer.setNoDelay(true);
  sRadio = true;
  sActivity = millis();
  Console::printf("AP %s up\n", sSsid);
  return true;
}

void sendText(const char *status, const char *type, const char *body) {
  const size_t length = body != nullptr ? strlen(body) : 0;
  sClient.printf("HTTP/1.1 %s\r\nContent-Type: %s\r\nContent-Length: %u\r\n"
                 "Connection: close\r\n\r\n",
                 status, type, static_cast<unsigned>(length));
  if (length > 0) {
    sClient.write(reinterpret_cast<const uint8_t *>(body), length);
  }
  closeHttp();
}

bool jpegMarkers(File &file) {
  if (file.size() < 4) {
    return false;
  }
  uint8_t mark[2];
  file.seek(0);
  if (file.read(mark, 2) != 2 || mark[0] != 0xFF || mark[1] != 0xD8) {
    return false;
  }
  file.seek(file.size() - 2);
  if (file.read(mark, 2) != 2 || mark[0] != 0xFF || mark[1] != 0xD9) {
    return false;
  }
  return true;
}

void finishPut() {
  sFile.close();
  File written = SD.open("/somnium/queue/incoming.jpg", FILE_READ);
  const bool ok = written && jpegMarkers(written);
  if (written) {
    written.close();
  }
  Item *item = findItem(sPutId);
  if (!ok || item == nullptr) {
    SD.remove("/somnium/queue/incoming.jpg");
    sendText("400 Bad Request", "text/plain", "bad jpeg");
    return;
  }
  SD.remove(item->dream);
  if (!SD.rename("/somnium/queue/incoming.jpg", item->dream)) {
    sendText("500 Error", "text/plain", "rename");
    return;
  }
  item->haveDream = true;
  item->freshDream = true;
  writeMeta(*item);
  sendText("201 Created", "text/plain", "ok");
  Console::printf("HTTP dream %08lx\n", static_cast<unsigned long>(item->id));
}

void beginSend(Item &item, bool dream) {
  const char *path = dream ? item.dream : item.orig;
  sFile = SD.open(path, FILE_READ);
  if (!sFile) {
    sendText("404 Not Found", "text/plain", "missing");
    return;
  }
  const size_t total = sFile.size();
  size_t start = 0;
  size_t end = total == 0 ? 0 : total - 1;
  const char *range = strcasestr(sHead, "bytes=");
  if (range != nullptr) {
    unsigned a = 0;
    unsigned b = 0;
    const char *spec = range + 6;
    if (sscanf(spec, "%u-%u", &a, &b) == 2) {
      start = a;
      end = b;
    } else if (sscanf(spec, "%u-", &a) == 1) {
      start = a;
    }
  }
  if (start >= total || end >= total || end < start) {
    sFile.close();
    sClient.printf("HTTP/1.1 416 Range Not Satisfiable\r\nContent-Range: bytes */%u\r\n"
                   "Connection: close\r\n\r\n",
                   static_cast<unsigned>(total));
    closeHttp();
    return;
  }
  sSendPos = start;
  sSendEnd = end + 1;
  sFile.seek(start);
  sClient.printf(
      "HTTP/1.1 %s\r\nAccept-Ranges: bytes\r\nContent-Type: image/jpeg\r\n"
      "Content-Range: bytes %u-%u/%u\r\nContent-Length: %u\r\nConnection: close\r\n\r\n",
      (start == 0 && sSendEnd == total) ? "200 OK" : "206 Partial Content",
      static_cast<unsigned>(start), static_cast<unsigned>(end),
      static_cast<unsigned>(total), static_cast<unsigned>(sSendEnd - start));
  sPhase = Phase::Send;
  sActivity = millis();
}

void dispatch() {
  char method[8] = {};
  char path[80] = {};
  if (sscanf(sHead, "%7s %79s", method, path) != 2) {
    sendText("400 Bad Request", "text/plain", "bad request");
    return;
  }
  if (strcmp(method, "GET") == 0 && strcmp(path, "/v1/health") == 0) {
    sendText("200 OK", "text/plain", "ok");
    return;
  }
  if (strcmp(method, "GET") == 0 && strcmp(path, "/v1/queue") == 0) {
    static char body[1024];
    size_t used = 0;
    body[0] = '\0';
    for (int i = 0; i < sCount && used + 96 < sizeof(body); ++i) {
      const int n = snprintf(
          body + used, sizeof(body) - used,
          "id=%08lx style=%s crc=%08lx bytes=%u dream=%d acked=%d\n",
          static_cast<unsigned long>(sItems[i].id), sItems[i].style,
          static_cast<unsigned long>(sItems[i].crc),
          static_cast<unsigned>(sItems[i].length), sItems[i].haveDream ? 1 : 0,
          sItems[i].acked ? 1 : 0);
      if (n < 0) {
        break;
      }
      used += static_cast<size_t>(n);
    }
    sendText("200 OK", "text/plain", body);
    return;
  }
  unsigned id = 0;
  if (strcmp(method, "GET") == 0 &&
      sscanf(path, "/v1/photos/%8x/original.jpg", &id) == 1) {
    Item *item = findItem(id);
    if (item == nullptr) {
      sendText("404 Not Found", "text/plain", "unknown");
      return;
    }
    beginSend(*item, false);
    return;
  }
  if (strcmp(method, "GET") == 0 &&
      sscanf(path, "/v1/photos/%8x/dream.jpg", &id) == 1) {
    Item *item = findItem(id);
    if (item == nullptr || !item->haveDream) {
      sendText("404 Not Found", "text/plain", "unknown");
      return;
    }
    beginSend(*item, true);
    return;
  }
  if (strcmp(method, "POST") == 0 && sscanf(path, "/v1/photos/%8x/ack", &id) == 1) {
    Item *item = findItem(id);
    if (item == nullptr) {
      sendText("404 Not Found", "text/plain", "unknown");
      return;
    }
    item->acked = true;
    writeMeta(*item);
    sendText("200 OK", "text/plain", "ok");
    return;
  }
  if (strcmp(method, "PUT") == 0 &&
      sscanf(path, "/v1/photos/%8x/dream.jpg", &id) == 1) {
    Item *item = findItem(id);
    const char *lengthHeader = strcasestr(sHead, "Content-Length:");
    unsigned length = 0;
    if (item == nullptr || lengthHeader == nullptr ||
        sscanf(lengthHeader, "Content-Length: %u", &length) != 1 || length < 4 ||
        length > kMaxPut) {
      sendText("400 Bad Request", "text/plain", "length");
      return;
    }
    SD.remove("/somnium/queue/incoming.jpg");
    sFile = SD.open("/somnium/queue/incoming.jpg", FILE_WRITE);
    if (!sFile) {
      sendText("500 Error", "text/plain", "sd");
      return;
    }
    sPutId = id;
    sRecvNeed = length;
    sRecvGot = 0;
    sPhase = Phase::Recv;
    sActivity = millis();
    return;
  }
  sendText("404 Not Found", "text/plain", "no");
}

void serviceHttp() {
  if (!sClient || !sClient.connected()) {
    sClient = sServer.accept();
    if (sClient) {
      sClient.setTimeout(2);
      sPhase = Phase::Headers;
      sHeadLen = 0;
      sPhaseAt = millis();
      sActivity = sPhaseAt;
    }
  }
  if (!sClient || !sClient.connected()) {
    return;
  }
  if (sPhase == Phase::Headers) {
    while (sClient.available() > 0 && sHeadLen + 1 < sizeof(sHead)) {
      sHead[sHeadLen++] = static_cast<char>(sClient.read());
      sHead[sHeadLen] = '\0';
    }
    if (strstr(sHead, "\r\n\r\n") != nullptr) {
      dispatch();
      return;
    }
    if (millis() - sPhaseAt > 3000 || sHeadLen + 1 >= sizeof(sHead)) {
      sendText("408 Timeout", "text/plain", "headers");
    }
    return;
  }
  if (sPhase == Phase::Send) {
    if (!sFile) {
      closeHttp();
      return;
    }
    uint8_t buf[2048];
    const size_t want = sSendEnd - sSendPos;
    const size_t n = sFile.read(buf, want > sizeof(buf) ? sizeof(buf) : want);
    if (n == 0) {
      closeHttp();
      return;
    }
    sClient.write(buf, n);
    sSendPos += n;
    sActivity = millis();
    if (sSendPos >= sSendEnd) {
      closeHttp();
    }
    return;
  }
  if (sPhase == Phase::Recv) {
    uint8_t buf[1024];
    while (sClient.available() > 0 && sRecvGot < sRecvNeed) {
      const size_t want = sRecvNeed - sRecvGot;
      const size_t n = sClient.read(buf, want > sizeof(buf) ? sizeof(buf) : want);
      if (n == 0) {
        break;
      }
      sFile.write(buf, n);
      sRecvGot += n;
      sActivity = millis();
    }
    if (sRecvGot >= sRecvNeed) {
      finishPut();
    } else if (!sClient.connected() && sClient.available() == 0) {
      sFile.close();
      SD.remove("/somnium/queue/incoming.jpg");
      closeHttp();
    }
  }
}

bool consumeDream(uint32_t id, JpegBuffer &out) {
  Item *item = findItem(id);
  if (item == nullptr || !item->freshDream) {
    return false;
  }
  item->freshDream = false;
  uint8_t *data = nullptr;
  size_t length = 0;
  if (!Storage::loadJpeg(item->dream, &data, &length)) {
    return false;
  }
  out = JpegBuffer{};
  out.data = data;
  out.length = length;
  out.crc32 = esp_rom_crc32_le(0, data, length);
  out.width = 0;
  out.height = 0;
  return true;
}

void sendPreview(uint32_t id, const uint8_t *jpeg, size_t length) {
  if (!Ble::paired() || jpeg == nullptr || length == 0) {
    return;
  }
  uint16_t *rgb = static_cast<uint16_t *>(
      malloc(static_cast<size_t>(Defaults::kPreviewWidth) * Defaults::kPreviewHeight *
             sizeof(uint16_t)));
  if (rgb == nullptr) {
    return;
  }
  const bool decoded =
      Image::decodeRgb565(jpeg, length, rgb, Defaults::kPreviewWidth,
                          Defaults::kPreviewHeight);
  if (!decoded) {
    free(rgb);
    return;
  }
  camera_fb_t frame{};
  frame.buf = reinterpret_cast<uint8_t *>(rgb);
  frame.len = static_cast<size_t>(Defaults::kPreviewWidth) * Defaults::kPreviewHeight * 2;
  frame.width = Defaults::kPreviewWidth;
  frame.height = Defaults::kPreviewHeight;
  frame.format = PIXFORMAT_RGB565;
  uint8_t *preview = nullptr;
  size_t previewLen = 0;
  if (frame2jpg(&frame, Defaults::kPreviewJpegQuality, &preview, &previewLen) &&
      preview != nullptr) {
    const uint32_t crc = esp_rom_crc32_le(0, preview, previewLen);
    Ble::startPreview(id, preview, previewLen, crc);
    free(preview);
  }
  free(rgb);
}

}  // namespace

void Transfer::begin() {
  WiFi.persistent(false);
  WiFi.mode(WIFI_OFF);
  uint8_t mac[6] = {};
  esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
  snprintf(sSsid, sizeof(sSsid), "%s%02X%02X", Defaults::kSoftApPrefix, mac[4], mac[5]);
  loadQueue();
  Console::printf("AP name %s (down until a phone pull)\n", sSsid);
}

void Transfer::poll() {
  uint32_t pull = 0;
  if (Ble::takePull(pull)) {
    startRadio(pull);
  }
  if (!sRadio) {
    return;
  }
  serviceHttp();
  const bool client = WiFi.softAPgetStationNum() > 0;
  const bool moving = sPhase != Phase::Idle;
  if (!sHold && !client && !moving && millis() - sActivity > Defaults::kSoftApIdleMs) {
    Console::println("AP idle");
    shutdown();
  }
}

void Transfer::shutdown() {
  sHold = false;
  closeHttp();
  sServer.stop();
  if (sRadio) {
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_OFF);
    sRadio = false;
    Console::println("AP down");
  }
}

bool Transfer::radioUp() { return sRadio; }

int Transfer::queueDepth() {
  int depth = 0;
  for (int i = 0; i < sCount; ++i) {
    if (!sItems[i].haveDream) {
      ++depth;
    }
  }
  return depth;
}

bool Transfer::relay(const uint8_t *jpeg, size_t length, uint32_t crc, const char *style,
                     const char *origPath, const char *dreamPath, RelayOutcome &out,
                     void (*pump)()) {
  PowerBusy busy;
  out = RelayOutcome{};
  if (jpeg == nullptr || length == 0 || style == nullptr || origPath == nullptr ||
      dreamPath == nullptr) {
    strncpy(out.message, "empty", sizeof(out.message) - 1);
    return false;
  }
  Item item;
  item.id = esp_random();
  if (item.id == 0) {
    item.id = 1;
  }
  strncpy(item.style, style, sizeof(item.style) - 1);
  strncpy(item.orig, origPath, sizeof(item.orig) - 1);
  strncpy(item.dream, dreamPath, sizeof(item.dream) - 1);
  item.crc = crc;
  item.length = length;
  addItem(item);
  sendPreview(item.id, jpeg, length);

  if (!Ble::paired()) {
    strncpy(out.message, "queued no phone", sizeof(out.message) - 1);
    return false;
  }
  if (!startRadio(item.id)) {
    strncpy(out.message, "ap failed", sizeof(out.message) - 1);
    return false;
  }
  sHold = true;
  const unsigned long started = millis();
  while (millis() - started < Defaults::kCookTimeoutMs) {
    Ble::poll();
    poll();
    if (pump != nullptr) {
      pump();
    }
    if (consumeDream(item.id, out.dream)) {
      sHold = false;
      strncpy(out.message, "dream", sizeof(out.message) - 1);
      return true;
    }
    delay(1);
  }
  sHold = false;
  strncpy(out.message, "phone timeout", sizeof(out.message) - 1);
  return false;
}

}  // namespace Somnium
