#pragma once

#include <string>
#include <vector>

// Simple, keyboard-driven, single-column menu data model. Owns the list of
// labels and the current selection; drawing is the caller's responsibility
// (see App::renderMenu).
class Menu {
public:
    struct Item {
        std::string label;
    };

    void setItems(std::vector<Item> items);

    void moveUp();
    void moveDown();

    int selectedIndex() const { return selectedIndex_; }
    const std::vector<Item>& items() const { return items_; }

private:
    std::vector<Item> items_;
    int selectedIndex_ = 0;
};
