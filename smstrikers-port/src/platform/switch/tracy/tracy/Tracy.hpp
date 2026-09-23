#pragma once

// Tracy's client does not build for Horizon: no-op macros, and the one function Aurora calls.

#define ZoneScoped
#define ZoneScopedN(name)
#define ZoneScopedS(depth)
#define FrameMark
#define TracyPlot(name, value)
#define TracyPlotConfig(name, type, step, fill, color)

namespace tracy {
inline void SetThreadName(const char*) {}
} // namespace tracy
