#include "library/Publisher.h"
#include "library/Library.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;
using json = nlohmann::ordered_json;

namespace palette {

std::string slugify(const std::string& title) {
    std::string s; bool us = false;
    for (char ch : title) {
        if (std::isalnum((unsigned char)ch)) { s += (char)std::tolower((unsigned char)ch); us = false; }
        else if (!us && !s.empty()) { s += '_'; us = true; }
    }
    while (!s.empty() && s.back() == '_') s.pop_back();
    return s.empty() ? "untitled" : s;
}

static bool writeAtomic(const fs::path& path, const std::string& text) {
    fs::path tmp = path; tmp += ".tmp";
    { std::ofstream f(tmp, std::ios::binary); if (!f) return false; f << text; }
    std::error_code ec; fs::rename(tmp, path, ec);
    return !ec;
}

static std::string homeDir() { const char* h = std::getenv("HOME"); return h ? h : "."; }

PublishResult publishToLibrary(const Library& lib, const PublishRequest& req) {
    PublishResult r;
    if (lib.root().empty()) { r.error = "library not connected"; return r; }
    fs::path root(lib.root());
    std::string stem = req.stem.empty() ? slugify(req.title) : req.stem;
    fs::path target = root / (stem + ".fs");
    for (int n = 1; fs::exists(target) && n < 100; ++n) target = root / (stem + "_" + std::to_string(n) + ".fs");
    if (fs::exists(target)) { r.error = "could not find a free filename"; return r; }
    std::string finalStem = target.stem().string();

    // 1. shader file
    if (!writeAtomic(target, req.isfSource)) { r.error = "could not write " + target.string(); return r; }

    // 2. manifest (lenient read → strict write)
    fs::path manifestPath = root / "manifest.json";
    json arr = json::array();
    if (fs::exists(manifestPath)) {
        try { std::ifstream f(manifestPath); arr = json::parse(f, nullptr, true, true); } catch (...) { arr = json::array(); }
        if (!arr.is_array()) arr = json::array();
    }
    int id = lib.nextFreeId();
    for (auto& e : arr) if (e.contains("id") && e["id"].is_number_integer()) id = std::max(id, e["id"].get<int>() + 1);
    json entry;  // keys sorted alphabetically, as the existing entries are
    entry["categories"] = req.categories.empty() ? json::array({"Generator"}) : json(req.categories);
    entry["credit"] = req.credit;
    entry["description"] = req.description;
    entry["file"] = finalStem + ".fs";
    entry["hasVertex"] = false;
    entry["hidden"] = req.hidden;
    entry["id"] = id;
    if (req.basedOnId >= 0) entry["palette"] = json{{"basedOn", req.basedOnId}};
    entry["title"] = req.title;
    entry["type"] = std::find(req.categories.begin(), req.categories.end(), "Effect") != req.categories.end() ? "effect" : "generator";
    arr.push_back(entry);
    if (!writeAtomic(manifestPath, arr.dump(2) + "\n")) { r.error = "could not write manifest.json"; return r; }

    // 3. packs.json group
    fs::path packsPath = root / "packs.json";
    try {
        json packs = json::object();
        if (fs::exists(packsPath)) { std::ifstream f(packsPath); packs = json::parse(f, nullptr, true, true); }
        if (!packs.is_object()) packs = json::object();
        packs[finalStem] = json{{"group", req.pack}, {"surfaces", json::array({"outside"})}};
        writeAtomic(packsPath, packs.dump(2) + "\n");
    } catch (...) { /* packs are optional */ }

    // 4. thumbnail into Easel's cache so the gallery shows it immediately
    if (!req.thumbnailPng.empty() && fs::exists(req.thumbnailPng)) {
        fs::path thumbDir = fs::path(homeDir()) / ".easel" / "shader_thumbs";
        std::error_code ec; fs::create_directories(thumbDir, ec);
        fs::copy_file(req.thumbnailPng, thumbDir / (finalStem + ".png"), fs::copy_options::overwrite_existing, ec);
    }

    r.ok = true; r.id = id; r.path = target.string();
    return r;
}

} // namespace palette
