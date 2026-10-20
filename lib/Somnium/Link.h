#pragma once

#include <stddef.h>
#include <stdint.h>

namespace Somnium {

// Byte pipe for one COOK / DREAM exchange. USB serial and the Wi-Fi client
// both speak this shape so the handshake stays in one place.
struct CookPort {
  void *ctx;
  int (*available)(void *ctx);
  int (*read)(void *ctx);
  size_t (*readBytes)(void *ctx, uint8_t *dst, size_t len);
  size_t (*write)(void *ctx, const uint8_t *data, size_t len);
  void (*flush)(void *ctx);
};

struct LinkOps {
  void (*begin)();
  void (*poll)();
  void (*setBusy)(bool busy);
  bool (*claim)(CookPort *port);
  int (*idleRead)();
  void (*drop)();
};

// Link.cpp installs the Wi-Fi ops. Until then the calls are no-ops and
// cooks stay on USB serial.
void linkBind(const LinkOps &ops);

// Referenced from startup so this translation unit is linked. The Wi-Fi
// ops are installed by a constructor in the same file.
void linkAnchor();

void linkBegin();
void linkPoll();
void linkSetBusy(bool busy);
bool linkClaim(CookPort *port);
int linkIdleRead();
void linkDrop();

// True while a laptop holds the TCP session (cook will use it).
bool linkConnected();
void linkRegister(bool (*isUp)());

}  // namespace Somnium
