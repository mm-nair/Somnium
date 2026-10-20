#include "LinkPlan.h"

#include <cstdio>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

namespace {

int gFailures = 0;

void check(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "FAIL " << message << "\n";
    gFailures += 1;
  }
}

void testLabels() {
  using Somnium::Link::Phase;
  check(std::strcmp(Somnium::Link::headerLabel(Phase::Portal), "Setup") == 0, "setup");
  check(std::strcmp(Somnium::Link::headerLabel(Phase::Online), "WiFi*") == 0, "online");
  check(std::strcmp(Somnium::Link::headerLabel(Phase::Associating), "WiFi.") == 0,
        "joining");
  check(std::strcmp(Somnium::Link::headerLabel(Phase::Backoff), "WiFi.") == 0, "backoff");
  check(std::strcmp(Somnium::Link::kPortalSsid, "Somnium-Setup") == 0, "portal ssid");
  char status[40];
  Somnium::Link::formatCookStatus(status, sizeof(status), 1);
  check(std::strcmp(status, "wifi > server...") == 0, "status first");
  Somnium::Link::formatCookStatus(status, sizeof(status), 3);
  check(std::strcmp(status, "wifi retry 3...") == 0, "status retry");
}

void testBackoff() {
  check(Somnium::Link::joinBackoffMs(1) == 1000, "join 1");
  check(Somnium::Link::joinBackoffMs(2) == 2000, "join 2");
  check(Somnium::Link::joinBackoffMs(5) == 16000, "join 5");
  check(Somnium::Link::joinBackoffMs(6) == 30000, "join cap");
  check(Somnium::Link::joinBackoffMs(9) == 30000, "join cap stays");
  check(Somnium::Link::retryDelayMs(0) == 500, "retry 0");
  check(Somnium::Link::retryDelayMs(1) == 1000, "retry 1");
  check(Somnium::Link::retryDelayMs(2) == 2000, "retry 2");
  check(Somnium::Link::retryDelayMs(3) == 4000, "retry 3");
  using Somnium::Link::FailClass;
  check(Somnium::Link::classifyHttp(0, false) == FailClass::Connect, "class connect");
  check(Somnium::Link::classifyHttp(200, true) == FailClass::Timeout, "class timeout");
  check(Somnium::Link::classifyHttp(502, false) == FailClass::HttpRetry, "class 502");
  check(Somnium::Link::classifyHttp(400, false) == FailClass::HttpFatal, "class 400");
  check(Somnium::Link::shouldRetry(FailClass::HttpRetry, 0, 4), "retry 502");
  check(!Somnium::Link::shouldRetry(FailClass::HttpFatal, 0, 4), "no retry 400");
  check(!Somnium::Link::shouldRetry(FailClass::Connect, 3, 4), "attempts exhausted");
}

void testTransitions() {
  using Somnium::Link::Event;
  using Somnium::Link::Phase;
  using Somnium::Link::transition;
  const Somnium::Link::Transition saved =
      transition(Phase::Portal, Event::PortalSaved);
  check(saved.phase == Phase::Associating && saved.stopPortal && saved.startJoin,
        "portal saved");
  const Somnium::Link::Transition joined =
      transition(Phase::Associating, Event::Associated);
  check(joined.phase == Phase::Online && !joined.startJoin, "associated");
  const Somnium::Link::Transition timed =
      transition(Phase::Associating, Event::JoinTimeout);
  check(timed.phase == Phase::Backoff && !timed.startJoin, "join timeout");
  const Somnium::Link::Transition again =
      transition(Phase::Backoff, Event::BackoffElapsed);
  check(again.phase == Phase::Associating && again.startJoin, "backoff elapsed");
  const Somnium::Link::Transition lost = transition(Phase::Online, Event::LinkLost);
  check(lost.phase == Phase::Backoff, "link lost");
  const Somnium::Link::Transition portal =
      transition(Phase::Online, Event::PortalRequested);
  check(portal.phase == Phase::Portal && portal.startPortal, "portal requested");
  const Somnium::Link::Transition boot =
      transition(Phase::Online, Event::BootNoCreds);
  check(boot.phase == Phase::Portal && boot.startPortal, "boot no creds");
  const Somnium::Link::Transition stay =
      transition(Phase::Portal, Event::Associated);
  check(stay.phase == Phase::Portal && !stay.startJoin, "portal ignores associate");
}

void testEndpoints() {
  Somnium::Link::Endpoint endpoint{};
  check(Somnium::Link::normalizeServer("", "8080", false, "/v1/process", &endpoint),
        "empty host parses");
  check(std::strcmp(endpoint.host, "somnium.local") == 0 && endpoint.port == 8080 &&
            endpoint.mdns && !endpoint.tls &&
            std::strcmp(endpoint.path, "/v1/process") == 0,
        "empty host defaults");
  check(Somnium::Link::normalizeServer("192.168.1.9", "9090", false, "/v1/process",
                                       &endpoint),
        "ipv4 parses");
  check(std::strcmp(endpoint.host, "192.168.1.9") == 0 && endpoint.port == 9090 &&
            !endpoint.mdns,
        "ipv4 fields");
  check(Somnium::Link::normalizeServer("http://10.0.0.8:8080/v1/process", "1", true,
                                       "/ignored", &endpoint),
        "url parses");
  check(!endpoint.tls && endpoint.port == 8080 &&
            std::strcmp(endpoint.host, "10.0.0.8") == 0 &&
            std::strcmp(endpoint.path, "/v1/process") == 0,
        "url wins over checkbox");
  check(Somnium::Link::normalizeServer("http://somnium.local", "8080", false,
                                       "/v1/process", &endpoint),
        "scheme without port");
  check(endpoint.mdns && endpoint.port == 8080 && !endpoint.tls, "port field fills scheme");
  check(Somnium::Link::parseUrl("https://somnium-red.vercel.app/api/upload", true, 443,
                                "/api/upload", &endpoint),
        "gallery url");
  check(endpoint.tls && endpoint.port == 443 &&
            std::strcmp(endpoint.host, "somnium-red.vercel.app") == 0 &&
            std::strcmp(endpoint.path, "/api/upload") == 0 && !endpoint.mdns,
        "gallery fields");
  check(!Somnium::Link::normalizeServer("bad host", "8080", false, "/v1/process",
                                        &endpoint),
        "space rejected");
  check(!Somnium::Link::normalizeServer("somnium.local", "0", false, "/v1/process",
                                        &endpoint),
        "port 0 rejected");
  check(!Somnium::Link::normalizeServer("somnium.local", "99999", false, "/v1/process",
                                        &endpoint),
        "port range rejected");
  check(Somnium::Link::isMdnsHost("somnium.local"), "mdns host");
  check(!Somnium::Link::isMdnsHost("192.168.1.9"), "not mdns");
}

void testHeadersAndBody() {
  check(!Somnium::Link::headerValueSafe(""), "empty token");
  check(!Somnium::Link::headerValueSafe("bad\ntoken"), "newline token");
  check(!Somnium::Link::headerValueSafe("bad token"), "space token");
  check(Somnium::Link::headerValueSafe("abcDEF-123_."), "token chars");
  char headers[128];
  check(Somnium::Link::formatGalleryHeaders(headers, sizeof(headers), "abc"),
        "gallery headers");
  check(std::strcmp(headers,
                    "Authorization: Bearer abc\r\nX-Somnium-Token: abc\r\n") == 0,
        "gallery header bytes");

  const uint8_t jpeg[] = {0xff, 0xd8, 0x11, 0x22, 0xff, 0xd9};
  const Somnium::Link::TextPart texts[] = {{"style", "anime"}, {"source", "wifi"}};
  const Somnium::Link::FilePart files[] = {
      {"image", "capture.jpg", "image/jpeg", jpeg, sizeof(jpeg)}};
  size_t need = 0;
  check(Somnium::Link::buildMultipart(nullptr, 0, &need, "SomniumBoundary7f3a", texts, 2,
                                      files, 1),
        "size");
  std::vector<uint8_t> body(need);
  size_t written = 0;
  check(Somnium::Link::buildMultipart(body.data(), body.size(), &written,
                                      "SomniumBoundary7f3a", texts, 2, files, 1),
        "build");
  check(written == need && need > sizeof(jpeg), "length");
  const std::string text(reinterpret_cast<const char *>(body.data()), body.size());
  check(text.find("name=\"style\"") != std::string::npos, "style field");
  check(text.find("\r\nanime\r\n") != std::string::npos, "style value");
  check(text.find("name=\"source\"") != std::string::npos, "source field");
  check(text.find("filename=\"capture.jpg\"") != std::string::npos, "filename");
  check(text.find("Content-Type: image/jpeg\r\n\r\n") != std::string::npos, "jpeg type");
  check(text.find(std::string(reinterpret_cast<const char *>(jpeg), sizeof(jpeg))) !=
            std::string::npos,
        "jpeg bytes");
  check(text.compare(text.size() - 22, 22, "--SomniumBoundary7f3a--\r\n") == 0 ||
            text.find("--SomniumBoundary7f3a--\r\n") != std::string::npos,
        "closing boundary");
  check(!Somnium::Link::buildMultipart(body.data(), 4, &written, "SomniumBoundary7f3a",
                                       texts, 2, files, 1),
        "short buffer");
  const Somnium::Link::TextPart bad[] = {{"sty\"le", "anime"}};
  check(!Somnium::Link::buildMultipart(nullptr, 0, &need, "SomniumBoundary7f3a", bad, 1,
                                       nullptr, 0),
        "quoted name");
}

}  // namespace

int main() {
  testLabels();
  testBackoff();
  testTransitions();
  testEndpoints();
  testHeadersAndBody();
  if (gFailures != 0) {
    std::cerr << gFailures << " failure(s)\n";
    return 1;
  }
  std::cout << "link plan ok\n";
  return 0;
}
