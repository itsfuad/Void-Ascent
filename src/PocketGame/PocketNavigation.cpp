#include "PocketNavigation.h"

namespace pocketgame {

void MenuNavigator::reset(uint8_t itemCount, uint8_t selected) {
  itemCount_ = itemCount;
  setSelected(selected);
}

void MenuNavigator::setItemCount(uint8_t itemCount) {
  itemCount_ = itemCount;
  if (itemCount_ == 0) {
    selected_ = 0;
  } else if (selected_ >= itemCount_) {
    selected_ = itemCount_ - 1;
  }
}

void MenuNavigator::setSelected(uint8_t selected) {
  selected_ = itemCount_ == 0 ? 0 : selected % itemCount_;
}

bool MenuNavigator::cycle(int8_t direction) {
  if (itemCount_ < 2 || direction == 0) {
    return false;
  }

  int16_t next = (int16_t)selected_ + direction;
  while (next < 0) {
    next += itemCount_;
  }
  selected_ = (uint8_t)(next % itemCount_);
  return true;
}

MenuAction MenuNavigator::update(const ControlEvents &events) {
  if (events.cycle && cycle()) {
    return MenuAction::CYCLED;
  }

  if (events.select && itemCount_ > 0) {
    return MenuAction::SELECTED;
  }

  return MenuAction::NONE;
}

} // namespace pocketgame
