#include "navigator.h"

#include <cstdio>

namespace teletext {

std::string Navigator::targetLabel() const {
    if (entry_.empty()) {
        char buf[8];
        std::snprintf(buf, sizeof(buf), "%03d", current_);
        return buf;
    }
    std::string label = entry_;
    label.resize(3, '-');
    return label;
}

void Navigator::goTo(int page) {
    entry_.clear();
    current_ = page;
}

void Navigator::up(const PageStore& store) {
    entry_.clear();
    current_ = store.nextPage(current_);
}

void Navigator::down(const PageStore& store) {
    entry_.clear();
    current_ = store.prevPage(current_);
}

void Navigator::left(const PageStore& store) {
    entry_.clear();
    current_ = store.prevArticle(current_);
}

void Navigator::right(const PageStore& store) {
    entry_.clear();
    current_ = store.nextArticle(current_);
}

void Navigator::digit(int value, double now) {
    if (value < 0 || value > 9) {
        return;
    }
    entry_.push_back(static_cast<char>('0' + value));
    lastDigitTime_ = now;
    if (entry_.size() == 3) {
        current_ = std::stoi(entry_);
        entry_.clear();
    }
}

void Navigator::update(double now) {
    if (!entry_.empty() && now - lastDigitTime_ > kEntryTimeoutSeconds) {
        entry_.clear();
    }
}

bool Navigator::cancelEntry() {
    const bool had = !entry_.empty();
    entry_.clear();
    return had;
}

bool Navigator::back(const PageStore& store) {
    if (cancelEntry()) {
        return true;
    }
    const TeletextPage* page = store.find(current_);
    // A page that doesn't exist (typed number, or evicted by a refresh) has
    // no recorded parent; the index is the sensible way out of it.
    const int parent = page ? page->parentPage : (current_ != kIndexPage ? kIndexPage : 0);
    if (parent == 0) {
        return false;
    }
    current_ = parent;
    return true;
}

}  // namespace teletext
