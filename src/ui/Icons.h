#pragma once
#include <imgui.h>

namespace palette::ui {

// The dock and More icons, drawn as strokes so they stay crisp at any DPI.
// All on a 24-unit grid, 1.6 stroke, round caps.
enum class Icon { Source, Shader, Controls, Live, Publish, More, Back, Code, Quality, Link, Export, Pause, Play, Search, Check, Close, Plus, Reset };

void icon(ImDrawList* dl, Icon which, ImVec2 center, float size, ImU32 col, float stroke = 1.6f);

} // namespace palette::ui
