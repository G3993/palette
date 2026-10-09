#pragma once
#include <string>
#include <vector>

namespace palette {

class Library;

struct PublishRequest {
    std::string title;          // "Chrome Ripple v3"
    std::string stem;           // "chrome_ripple_v3" (no extension)
    std::string description;
    std::string credit = "Palette";
    std::vector<std::string> categories; // ["Effect","Audio Reactive"]
    std::string pack = "mine";  // packs.json group
    int basedOnId = -1;
    bool hidden = false;
    std::string isfSource;      // full .fs text (header + GLSL)
    std::string thumbnailPng;   // optional path to a PNG to copy into Easel's thumb cache
};

struct PublishResult {
    bool ok = false;
    int id = -1;
    std::string path;           // written .fs
    std::string error;
};

// The one writer of manifest.json. Reads leniently, writes strictly:
// sorted keys, two-space indent, atomic rename. Never overwrites an existing
// .fs (appends _1, _2 like Easel's import). Also adds the packs.json group
// and drops the thumbnail where Easel caches its own.
PublishResult publishToLibrary(const Library& lib, const PublishRequest& req);

// Utility: slugify a title into a snake_case stem.
std::string slugify(const std::string& title);

} // namespace palette
