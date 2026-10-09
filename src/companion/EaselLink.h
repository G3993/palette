#pragma once
#include <string>
#include <vector>
#include "companion/Osc.h"

namespace palette::companion {

// Palette → Easel. Easel listens for OSC on 9000 and hot-reloads any shader
// file it has loaded from the ShaderClaw3 folder. So: write a live file into
// that folder, ask Easel to put it on a zone, then rewrite the file on every
// edit and push parameter changes as they happen.
class EaselLink {
public:
    bool connect(const std::string& shadersRoot, const std::string& zone, const std::string& slot,
                 const std::string& host = "127.0.0.1", int port = 9000);
    void disconnect();
    bool connected() const { return m_connected; }
    const std::string& zone() const { return m_zone; }
    const std::string& slot() const { return m_slot; }
    const std::string& livePath() const { return m_livePath; }

    // Writes the ISF text to the live file and (first time) ensures the layer on the zone.
    bool pushCode(const std::string& isfSource);
    void pushParam(const std::string& name, float v);
    void pushParam(const std::string& name, bool v);
    void pushColor(const std::string& name, float r, float g, float b, float a);
    void pushAudioBind(const std::string& param, const std::string& signal, float amount, float smoothing);
    void showShader(const std::string& path); // put an arbitrary library shader on the zone
    const std::vector<std::string>& log() const { return m_log; }

private:
    Osc m_osc;
    bool m_connected = false, m_ensured = false;
    std::string m_zone, m_slot, m_livePath;
    std::vector<std::string> m_log;
    void logLine(const std::string& s);
};

} // namespace palette::companion
