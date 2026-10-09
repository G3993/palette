#ifndef __APPLE__
#include "live/SystemTap.h"
namespace palette::live {
bool SystemTap::available() { return false; }
bool SystemTap::start(Sink s) { sink = std::move(s); m_error = "no system tap on this platform"; return false; }
void SystemTap::stop() { m_running = false; }
}
#endif
