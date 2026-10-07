# Palette

A light, cross-platform editor for real-time shaders, simulations, particles, SDF 3D scenes and image effects. Palette is the sibling of [Easel](https://github.com/G3993/easel): it edits the same ISF shaders Easel plays, publishes them into the [ShaderClaw3](https://github.com/G3993/ShaderClaw3) library, and can attach to a running Easel to edit a shader live on a zone.

UI model: every tool lives in a bottom rail, parameters pop up in a sheet over a full-bleed canvas (the Instagram Edits pattern). The same ISF `INPUTS` schema drives the phone sheet, the tablet side sheet and the desktop dock.

Design review, research and mockups: https://claude.ai/artifact/CcAQ4QzK5cAHxb2ucHBMaJ

## Status — Cut 1 vertical slice (2026-10-07)

- SDL3 window, OpenGL 3.3 core, Dear ImGui (docking branch, SDL3 + GL3 backends)
- ISF runtime lifted from Easel (`src/sources/ShaderSource.*`, `src/render/*`): multi-pass, persistent buffers, full `audio*` uniform bus, MOTION kit
- Library reader for `~/ShaderClaw3/shaders/manifest.json`, lenient about duplicate, string and missing ids; lists unregistered `.fs` files too
- Rail with the twelve tools, sheets for Shader (search + open), Params (generated from INPUTS, grouped, bind dots), Source (image file → `inputImage`, drag-and-drop), Code (edit + ⌘⏎ recompile with last-good fallback), Quality (tier → render scale)
- HUD: fps, frame ms, tier, render size, compile status

Not yet: Bind sheet, Make Parameter, Sim/3D/Light/Sound/Motion/Finish tools, Publish writer, Easel Connect, mobile builds. See the review page for the plan.

## Build (macOS)

```bash
# Xcode-license shim: use the Command Line Tools and Homebrew's Python for glad's generator
export DEVELOPER_DIR=/Library/Developer/CommandLineTools
cmake -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_POLICY_VERSION_MINIMUM=3.5
cmake --build build -j10
./build/palette [path/to/shader.fs]
```

Dependencies are fetched by CMake (SDL3 3.2.10, GLM, stb, nlohmann/json, glad 0.1.36, Dear ImGui docking). Windows and Linux build with the same CMake; iOS and Android come in Cut 4.

## Keys

`1`–`9`, `0`, `-`, `=` select rail tools · `Space` pause · `Esc` close sheet · `⌘⏎` compile in Code · drop a `.fs` or an image onto the window

## Shader rules (so one file runs on Easel, the web host and phones)

No `#version` in the file (the host prepends it), `IMG_NORM_PIXEL` not `texture2D`, bounded loops, clamped denominators, 16-bit float state buffers by default. Palette-only metadata lives under a `"PALETTE"` key in the ISF header that other hosts ignore.

## Related

- [Easel](https://github.com/G3993/easel) — projection mapping and live visuals host; Palette shares its ISF runtime and design tokens
- [ShaderClaw3](https://github.com/G3993/ShaderClaw3) — the shader library and web host Palette publishes into
- [3DClaw](https://github.com/G3993/3DClaw) — the binding-engine idea Palette's Bind sheet carries forward
