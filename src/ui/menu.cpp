#include "menu.h"

#include <algorithm>

void Menu::setItems(std::vector<Item> items) {
    items_ = std::move(items);
    selectedIndex_ = 0;
}

void Menu::moveUp() {
    if (items_.empty()) return;
    selectedIndex_ = std::max(0, selectedIndex_ - 1);
}

void Menu::moveDown() {
    if (items_.empty()) return;
    selectedIndex_ = std::min(static_cast<int>(items_.size()) - 1, selectedIndex_ + 1);
}
