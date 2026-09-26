#include "file_browser.h"

#include <algorithm>
#include <cctype>
#include <filesystem>

namespace fs = std::filesystem;

namespace {

std::string toLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                    [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

bool matchesFilter(const fs::path& path, const std::vector<std::string>& extensions) {
    if (extensions.empty()) {
        return true;
    }
    const std::string ext = toLower(path.extension().string());
    return std::find(extensions.begin(), extensions.end(), ext) != extensions.end();
}

bool byNameCaseInsensitive(const FileBrowser::Entry& a, const FileBrowser::Entry& b) {
    return toLower(a.name) < toLower(b.name);
}

bool isDotfile(const std::string& name) {
    return !name.empty() && name[0] == '.';
}

// Resolves to a clean, absolute path so repeated parent_path() calls (for
// the ".." entry) walk predictably up to the real filesystem root, even
// when the configured root was given as a relative path (e.g. "media").
fs::path normalizeDir(const fs::path& p) {
    std::error_code ec;
    fs::path canon = fs::weakly_canonical(p, ec);
    if (!ec && !canon.empty()) {
        return canon;
    }
    return fs::absolute(p);
}

}  // namespace

void FileBrowser::open(std::vector<std::string> roots, std::vector<std::string> extensions, bool showHidden) {
    roots_.clear();
    roots_.reserve(roots.size());
    for (const auto& root : roots) {
        roots_.push_back(normalizeDir(root).string());
    }
    extensions_ = std::move(extensions);
    showHidden_ = showHidden;
    pickerMode_ = false;
    pathStack_.clear();
    if (roots_.size() == 1) {
        pathStack_.push_back(roots_[0]);
    }
    selectedIndex_ = 0;
    refresh();
}

void FileBrowser::openPicker(const std::string& startDir, bool showHidden) {
    roots_ = {normalizeDir(startDir).string()};
    extensions_.clear();
    showHidden_ = showHidden;
    pickerMode_ = true;
    pathStack_.clear();
    pathStack_.push_back(roots_[0]);
    selectedIndex_ = 0;
    refresh();
}

bool FileBrowser::selectedIsPickHere() const {
    if (entries_.empty()) return false;
    return entries_[selectedIndex_].isPickHere;
}

void FileBrowser::refresh() {
    entries_.clear();
    selectedIndex_ = 0;

    if (pathStack_.empty()) {
        // Top-level root picker; only reachable with multiple configured
        // roots (see open()).
        for (const auto& root : roots_) {
            std::string label = fs::path(root).filename().string();
            if (label.empty()) {
                label = root;
            }
            entries_.push_back({label, root, /*isDirectory=*/true});
        }
        return;
    }

    // ".." lets browsing go above the configured root(s) -- up to the
    // user's home directory and beyond, not just nested subfolders of a
    // configured root. Shown even if the current directory turns out to be
    // unreadable below, so there's always a way back out.
    const fs::path current(pathStack_.back());
    const fs::path parent = current.parent_path();
    if (!parent.empty() && parent != current) {
        entries_.push_back({"..", parent.string(), /*isDirectory=*/true});
    }

    if (pickerMode_) {
        Entry pickHere;
        pickHere.name = "[Select This Folder]";
        pickHere.isDirectory = false;
        pickHere.isPickHere = true;
        entries_.push_back(pickHere);
    }

    std::error_code ec;
    fs::directory_iterator it(current, fs::directory_options::skip_permission_denied, ec);
    if (ec) {
        return;
    }

    std::vector<Entry> dirs, files;
    for (const auto& de : it) {
        const std::string name = de.path().filename().string();
        if (!showHidden_ && isDotfile(name)) {
            continue;
        }
        std::error_code typeEc;
        if (de.is_directory(typeEc) && !typeEc) {
            dirs.push_back({name, de.path().string(), true});
        } else if (!pickerMode_ && de.is_regular_file(typeEc) && !typeEc) {
            if (matchesFilter(de.path(), extensions_)) {
                files.push_back({name, de.path().string(), false});
            }
        }
    }
    std::sort(dirs.begin(), dirs.end(), byNameCaseInsensitive);
    std::sort(files.begin(), files.end(), byNameCaseInsensitive);

    entries_.reserve(dirs.size() + files.size());
    entries_.insert(entries_.end(), dirs.begin(), dirs.end());
    entries_.insert(entries_.end(), files.begin(), files.end());
}

void FileBrowser::moveUp() {
    if (entries_.empty()) return;
    selectedIndex_ = std::max(0, selectedIndex_ - 1);
}

void FileBrowser::moveDown() {
    if (entries_.empty()) return;
    selectedIndex_ = std::min(static_cast<int>(entries_.size()) - 1, selectedIndex_ + 1);
}

void FileBrowser::moveBy(int delta) {
    if (entries_.empty()) return;
    selectedIndex_ = std::clamp(selectedIndex_ + delta, 0, static_cast<int>(entries_.size()) - 1);
}

bool FileBrowser::selectedIsDirectory() const {
    if (entries_.empty()) return false;
    return entries_[selectedIndex_].isDirectory;
}

void FileBrowser::enterSelectedDirectory() {
    if (entries_.empty() || !entries_[selectedIndex_].isDirectory) {
        return;
    }
    pathStack_.push_back(entries_[selectedIndex_].fullPath);
    refresh();
}

bool FileBrowser::goBack() {
    if (pathStack_.empty()) {
        return false;  // at the top (roots picker, or single-root with nothing above it)
    }
    pathStack_.pop_back();
    if (pathStack_.empty() && roots_.size() == 1) {
        // Single configured root: no meaningful "roots picker" level to
        // fall back to -- leave the file browser entirely.
        return false;
    }
    refresh();
    return true;
}

std::string FileBrowser::selectedFilePath() const {
    if (entries_.empty() || entries_[selectedIndex_].isDirectory) {
        return "";
    }
    return entries_[selectedIndex_].fullPath;
}

std::string FileBrowser::currentPathLabel() const {
    if (pathStack_.empty()) {
        return "Select Folder";
    }
    return pathStack_.back();
}
