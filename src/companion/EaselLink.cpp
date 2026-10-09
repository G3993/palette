#include "companion/EaselLink.h"
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

namespace palette::companion {

void EaselLink::logLine(const std::string& s) { m_log.push_back(s); if (m_log.size() > 40) m_log.erase(m_log.begin()); }

bool EaselLink::connect(const std::string& shadersRoot, const std::string& zone, const std::string& slot, const std::string& host, int port) {
    disconnect();
    if (!m_osc.open(host, port)) { logLine("could not open UDP socket"); return false; }
    m_zone = zone.empty() ? "Main" : zone;
    m_slot = slot.empty() ? "palette:1" : slot;
    m_livePath = (fs::path(shadersRoot) / "_palette_live.fs").string();
    m_connected = true; m_ensured = false;
    logLine("→ " + host + ":" + std::to_string(port) + " · zone " + m_zone + " · slot " + m_slot);
    return true;
}

void EaselLink::disconnect() {
    if (m_connected) {
        m_osc.send("/easel/layer/remove-managed", {m_slot});
        logLine("→ remove-managed " + m_slot);
    }
    m_osc.close();
    m_connected = false; m_ensured = false;
}

bool EaselLink::pushCode(const std::string& isf) {
    if (!m_connected) return false;
    { std::ofstream f(m_livePath, std::ios::binary); if (!f) { logLine("could not write live file"); return false; } f << isf; }
    if (!m_ensured) {
        m_osc.send("/easel/layer/ensure/shader", {m_slot, m_livePath});
        m_osc.send("/easel/zone/layer", {m_zone, m_slot});
        m_ensured = true;
        logLine("→ ensure/shader " + m_slot);
        logLine("→ zone/layer " + m_zone + " " + m_slot);
    } else {
        logLine("→ live file rewritten · Easel hot-reloads");
    }
    return true;
}

void EaselLink::pushParam(const std::string& name, float v) {
    if (!m_connected) return;
    m_osc.send("/easel/layer/param", {m_slot, name, v});
}
void EaselLink::pushParam(const std::string& name, bool v) {
    if (!m_connected) return;
    m_osc.send("/easel/layer/param", {m_slot, name, v ? 1 : 0});
}
void EaselLink::pushColor(const std::string& name, float r, float g, float b, float a) {
    if (!m_connected) return;
    m_osc.send("/easel/layer/param", {m_slot, name, r, g, b, a});
}
void EaselLink::pushAudioBind(const std::string& param, const std::string& signal, float amount, float smoothing) {
    if (!m_connected) return;
    m_osc.send("/easel/layer/audiobind", {m_slot, param, signal, amount, smoothing});
    logLine("→ audiobind " + param + " ← " + signal);
}
void EaselLink::showShader(const std::string& path) {
    if (!m_connected) return;
    m_osc.send("/easel/layer/ensure/shader", {m_slot, path});
    m_osc.send("/easel/zone/layer", {m_zone, m_slot});
    m_ensured = false; // next pushCode re-ensures the live file
    logLine("→ show " + fs::path(path).filename().string());
}

} // namespace palette::companion
