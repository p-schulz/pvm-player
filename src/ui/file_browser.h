#pragma once

#include <string>
#include <vector>

// Directory listing scoped to one or more configured media root folders,
// with nested navigation (descend into a subfolder, go back up) and
// extension filtering. Phase 3 was a flat single-directory version of
// this; Phase 4 adds multiple roots + nested descent.
class FileBrowser {
public:
    struct Entry {
        std::string name;      // display name (filename, dir name, or root label)
        std::string fullPath;  // resolved path this entry refers to
        bool isDirectory = false;
        bool isPickHere = false;  // synthetic "select this folder" marker (openPicker() mode only)
    };

    // (Re)starts browsing from the top level for the given configured media
    // roots, filtered to files whose extension (case-insensitive, with
    // leading dot) is in `extensions` (empty = accept all). With exactly
    // one root, browsing starts already descended into it -- no point
    // showing a one-item root picker. With several, the top level lists
    // the roots themselves as enterable "folders". `showHidden` controls
    // whether dotfiles/dot-directories are listed (the synthetic ".."
    // entry is never affected by this).
    void open(std::vector<std::string> roots, std::vector<std::string> extensions, bool showHidden);

    // Starts a directory-only "pick a folder" browsing session rooted at
    // `startDir`: no files are listed (only subfolders + the ".." parent
    // entry, same nested navigation as open()), and a synthetic "[Select
    // This Folder]" entry appears first so the *current* directory can be
    // chosen without navigating into a child -- see selectedIsPickHere().
    void openPicker(const std::string& startDir, bool showHidden);

    // True if the selected entry is the synthetic "[Select This Folder]"
    // row (openPicker() mode only). The directory to use on confirmation
    // is currentPathLabel(), not this entry's own (empty) fullPath.
    bool selectedIsPickHere() const;

    void moveUp();
    void moveDown();

    // Descends into the selected entry if it's a directory. No-op otherwise.
    void enterSelectedDirectory();

    // Moves up one level if possible (returns true, listing refreshed).
    // Returns false when already at the top of the browsing session --
    // the caller should leave the file browser entirely (e.g. back to the
    // root menu) rather than expect a refreshed listing.
    bool goBack();

    bool empty() const { return entries_.empty(); }
    int selectedIndex() const { return selectedIndex_; }
    const std::vector<Entry>& entries() const { return entries_; }

    bool selectedIsDirectory() const;
    // Full path of the selected entry, or "" if the selection isn't a file
    // (empty listing or a directory).
    std::string selectedFilePath() const;

    // Human-readable label for the current level, for the browser header.
    std::string currentPathLabel() const;

private:
    void refresh();

    std::vector<std::string> roots_;
    std::vector<std::string> extensions_;
    std::vector<std::string> pathStack_;  // descended directories, full paths
    std::vector<Entry> entries_;
    int selectedIndex_ = 0;
    bool showHidden_ = false;
    bool pickerMode_ = false;
};
