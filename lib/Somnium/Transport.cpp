#include "Transport.h"

#include <math.h>
#include <string.h>

#include "Console.h"
#include "Host.h"
#include "HttpCook.h"
#include "Image.h"
#include "LinkPlan.h"
#include "LinkStatus.h"
#include "NetConfig.h"
#include "WifiLink.h"

namespace Somnium {
namespace {

Transport::StatusFn gStatus = nullptr;
TransportKind gLastPath = TransportKind::None;

void note(const char *text) {
  if (text == nullptr) {
    return;
  }
  Console::printf("LINK %s\n", text);
  if (gStatus != nullptr) {
    gStatus(text);
  }
}

bool onLine(const char *line) {
  if (line == nullptr) {
    return false;
  }
  if (strcmp(line, "link") == 0) {
    WifiLink::printStatus();
    return true;
  }
  if (strcmp(line, "portal") == 0) {
    WifiLink::requestPortal();
    Serial.println("OK portal");
    return true;
  }
  if (strcmp(line, "wificlear") == 0) {
    NetConfig::clear();
    WifiLink::reload();
    Serial.println("OK wificlear");
    return true;
  }
  if (strcmp(line, "wificook") == 0) {
    JpegBuffer photo;
    if (!Image::takePhoto(photo)) {
      Serial.println("ERR capture");
      return true;
    }
    JpegBuffer dream{};
    String error;
    const bool ok = Transport::cook(photo.data, photo.length, "anime", nullptr, NAN, NAN,
                                    dream, &error);
    Image::freePhoto(photo);
    if (!ok) {
      Serial.printf("ERR wificook %s\n", error.c_str());
      return true;
    }
    Serial.printf("OK wificook %s %u\n",
                  gLastPath == TransportKind::Wifi ? "wifi" : "usb",
                  static_cast<unsigned>(dream.length));
    free(dream.data);
    return true;
  }
  return false;
}

}  // namespace

void Transport::begin() {
  HttpCook::setNote(note);
  Host::setLineHook(onLine);
  WifiLink::begin();
}

void Transport::poll() {
  WifiLink::poll();
  Host::poll();
}

void Transport::setPump(PumpFn fn) {
  Host::setPump(fn);
  HttpCook::setPump(fn);
}

void Transport::setStatus(StatusFn fn) { gStatus = fn; }

void Transport::requestPortal() { WifiLink::requestPortal(); }

const char *Transport::headerLabel() { return LinkStatus::label(); }

TransportKind Transport::lastPath() { return gLastPath; }

bool Transport::cook(const uint8_t *jpeg, size_t jpegLen, const char *styleId,
                     const char *takenAt, float lat, float lon, JpegBuffer &outDream,
                     String *err) {
  outDream = JpegBuffer{};
  gLastPath = TransportKind::None;

  if (WifiLink::phase() == Link::Phase::Associating && !WifiLink::isOnline()) {
    const unsigned long started = millis();
    while (!WifiLink::isOnline() && millis() - started < Link::kAssocWaitMs) {
      WifiLink::poll();
      delay(20);
    }
  }

  String wifiError;
  const bool wifiUp = WifiLink::isOnline();
  if (wifiUp) {
    WifiLink::setBusy(true);
    const bool cooked = HttpCook::process(jpeg, jpegLen, styleId, takenAt, lat, lon,
                                          outDream, &wifiError);
    WifiLink::setBusy(false);
    if (cooked) {
      gLastPath = TransportKind::Wifi;
      return true;
    }
    Console::printf("LINK wifi failed %s\n", wifiError.c_str());
  }

  note("usb > thinkpad...");
  WifiLink::setBusy(true);
  String usbError;
  const bool cooked =
      Host::cook(jpeg, jpegLen, styleId, takenAt, lat, lon, outDream, &usbError);
  WifiLink::setBusy(false);
  if (cooked) {
    gLastPath = TransportKind::Usb;
    if (!wifiUp) {
      Console::println("LINK usb fallback");
    }
    return true;
  }

  gLastPath = TransportKind::None;
  if (err != nullptr) {
    if (wifiUp) {
      *err = String("wifi ") + wifiError + "; usb " + usbError;
    } else {
      *err = usbError;
    }
  }
  return false;
}

}  // namespace Somnium
