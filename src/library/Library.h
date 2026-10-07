#pragma once
#include <string>
#include <vector>

namespace palette {

// One shader in the ShaderClaw3 library.
struct LibraryEntry {
    int id = -1;                       // -1 = none or non-integer id in the manifest
    std::string title, file, path, type, description;
    std::vector<std::string> categories;
    bool hidden = false;
    bool inManifest = true;
    bool matches(const std::string& lowerQuery) const;
};

// Reads ~/ShaderClaw3/shaders/manifest.json LENIENTLY (duplicate ids, string
// ids, missing ids, files on disk with no entry) and offers the strict writer
// the Publish sheet needs (Cut 3). The reader never rewrites the manifest.
class Library {
public:
    // First existing root wins. Returns false if none exists.
    bool connect(const std::vector<std::string>& candidateRoots);
    void refresh();
    const std::string& root() const { return m_root; }
    const std::vector<LibraryEntry>& entries() const { return m_entries; }
    const LibraryEntry* find(const std::string& stemOrTitle) const;
    int nextFreeId() const;            // max(1000, max integer id + 1), Easel's rule
    void touch(const std::string& path); // recent list (session only for now)
    const std::vector<std::string>& recent() const { return m_recent; }

private:
    std::string m_root;
    std::vector<LibraryEntry> m_entries;
    std::vector<std::string> m_recent;
};

} // namespace palette
