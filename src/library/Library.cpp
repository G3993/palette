#include "library/Library.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace palette {

static std::string lower(std::string s) { std::transform(s.begin(), s.end(), s.begin(), ::tolower); return s; }

static std::string titleCase(std::string stem) {
    std::string out; bool up = true;
    for (char ch : stem) {
        if (ch == '_' || ch == '-') { out += ' '; up = true; continue; }
        out += up ? (char)toupper(ch) : ch; up = false;
    }
    return out;
}

bool LibraryEntry::matches(const std::string& q) const {
    if (lower(title).find(q) != std::string::npos) return true;
    if (lower(file).find(q) != std::string::npos) return true;
    if (lower(description).find(q) != std::string::npos) return true;
    for (auto& c : categories) if (lower(c).find(q) != std::string::npos) return true;
    return false;
}

bool Library::connect(const std::vector<std::string>& roots) {
    for (auto& r : roots) {
        if (fs::is_directory(r)) { m_root = r; refresh(); return true; }
    }
    return false;
}

void Library::refresh() {
    m_entries.clear();
    if (m_root.empty()) return;
    std::set<std::string> seen;
    fs::path manifest = fs::path(m_root) / "manifest.json";
    if (fs::exists(manifest)) {
        try {
            std::ifstream f(manifest);
            json arr = json::parse(f, nullptr, true, true);
            for (auto& e : arr) {
                LibraryEntry le;
                le.title = e.value("title", "");
                le.file = e.value("file", "");
                le.type = e.value("type", "");
                le.description = e.value("description", "");
                le.hidden = e.value("hidden", false);
                if (e.contains("id") && e["id"].is_number_integer()) le.id = e["id"].get<int>();
                if (e.contains("categories") && e["categories"].is_array())
                    for (auto& c : e["categories"]) if (c.is_string()) le.categories.push_back(c.get<std::string>());
                if (le.file.empty()) continue;
                std::string folder = e.value("folder", "");
                le.path = folder.empty() ? (fs::path(m_root) / le.file).string()
                                         : (fs::path(m_root).parent_path() / folder / le.file).string();
                if (le.title.empty()) le.title = titleCase(fs::path(le.file).stem().string());
                seen.insert(le.file);
                m_entries.push_back(std::move(le));
            }
        } catch (const std::exception& ex) {
            std::cerr << "[library] manifest parse failed: " << ex.what() << "\n";
        }
    }
    // Files on disk with no manifest entry still show (the SDK does the same).
    for (auto& d : fs::directory_iterator(m_root)) {
        if (!d.is_regular_file() || d.path().extension() != ".fs") continue;
        std::string file = d.path().filename().string();
        if (seen.count(file)) continue;
        LibraryEntry le;
        le.file = file; le.path = d.path().string(); le.title = titleCase(d.path().stem().string());
        le.inManifest = false; le.type = "generator";
        m_entries.push_back(std::move(le));
    }
    std::sort(m_entries.begin(), m_entries.end(), [](auto& a, auto& b) { return lower(a.title) < lower(b.title); });
}

const LibraryEntry* Library::find(const std::string& q) const {
    std::string lq = lower(q);
    for (auto& e : m_entries) if (lower(fs::path(e.file).stem().string()) == lq || lower(e.title) == lq) return &e;
    for (auto& e : m_entries) if (lower(e.file).find(lq) != std::string::npos) return &e;
    return nullptr;
}

int Library::nextFreeId() const {
    int mx = 999;
    for (auto& e : m_entries) mx = std::max(mx, e.id);
    return mx + 1;
}

void Library::touch(const std::string& path) {
    m_recent.erase(std::remove(m_recent.begin(), m_recent.end(), path), m_recent.end());
    m_recent.insert(m_recent.begin(), path);
    if (m_recent.size() > 12) m_recent.resize(12);
}

} // namespace palette
