#include "navigator.h"

#include <algorithm>
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
    currentSubPage_ = 0;
    selectedLink_ = 0;
}

void Navigator::stepPage(const PageStore& store, int direction) {
    entry_.clear();
    selectedLink_ = 0;
    if (direction > 0) {
        if (currentSubPage_ + 1 < store.subPageCount(current_)) {
            ++currentSubPage_;
            return;
        }
        current_ = store.nextPage(current_);
    } else {
        if (currentSubPage_ > 0) {
            --currentSubPage_;
            return;
        }
        current_ = store.prevPage(current_);
    }
    currentSubPage_ = 0;
}

void Navigator::selectLink(int direction, int linkCount) {
    entry_.clear();
    if (linkCount <= 0) {
        selectedLink_ = 0;
        return;
    }
    selectedLink_ = std::clamp(selectedLink_ + direction, 0, linkCount - 1);
}

void Navigator::digit(int value, double now) {
    if (value < 0 || value > 9) {
        return;
    }
    entry_.push_back(static_cast<char>('0' + value));
    lastDigitTime_ = now;
    if (entry_.size() == 3) {
        current_ = std::stoi(entry_);
        currentSubPage_ = 0;
        entry_.clear();
        selectedLink_ = 0;
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
    const TeletextPage* page = store.find(current_, currentSubPage_);
    // A page that doesn't exist (typed number, or evicted by a refresh) has
    // no recorded parent; the index is the sensible way out of it. Every
    // sub-page of a number shares that number's parent (see ard_pages.cpp),
    // so this doesn't need to special-case currentSubPage_ > 0.
    const int parent = page ? page->parentPage : (current_ != kIndexPage ? kIndexPage : 0);
    if (parent == 0) {
        return false;
    }
    current_ = parent;
    currentSubPage_ = 0;
    selectedLink_ = 0;
    return true;
}

}  // namespace teletext
