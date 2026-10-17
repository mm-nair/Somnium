#pragma once

#include "Canvas.h"
#include "Types.h"

namespace Somnium {
namespace Ui {

enum class NavAction : uint8_t {
  None,
  Back,
  Select,
  Left,
  Right,
  Up,
  Down,
};

class Page {
 public:
  virtual ~Page() = default;

  virtual void onEnter() {}
  virtual void onExit() {}
  virtual void update(uint32_t dtMs) { (void)dtMs; }
  virtual void draw(Canvas &canvas) = 0;
  virtual bool handleAction(NavAction action) {
    (void)action;
    return false;
  }

  void markDirty() { dirty_ = true; }
  bool consumeDirty() {
    const bool was = dirty_;
    dirty_ = false;
    return was;
  }
  bool isDirty() const { return dirty_; }

 protected:
  bool dirty_ = true;
};

}  // namespace Ui
}  // namespace Somnium
