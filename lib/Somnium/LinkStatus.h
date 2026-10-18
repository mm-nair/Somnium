#pragma once

namespace Somnium {
namespace LinkStatus {

// Header-only slot so the mode page can show Setup / WiFi* / WiFi. without
// linking the Wi-Fi stack. WifiLink writes the pointer on each transition.
// The strings are literals owned by Link::headerLabel.

inline const char *&slot() {
  static const char *label = "WiFi.";
  return label;
}

inline const char *label() { return slot(); }

inline void set(const char *text) {
  slot() = text != nullptr && text[0] != '\0' ? text : "WiFi.";
}

}  // namespace LinkStatus
}  // namespace Somnium
