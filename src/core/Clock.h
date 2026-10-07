#pragma once
#include <chrono>

namespace palette {

// Monotonic seconds since first call. Replaces glfwGetTime() in the lifted ISF
// runtime so the renderer has no window-system dependency.
inline double clockSeconds() {
    using clock = std::chrono::steady_clock;
    static const auto t0 = clock::now();
    return std::chrono::duration<double>(clock::now() - t0).count();
}

} // namespace palette
