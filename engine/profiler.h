#pragma once
// ══════════════════════════════════════════════════════════════════════════════
// TIGERFISH PROFILER INTERFACE (engine/profiler.h)
// Zero-cost conditional instrumentation.
// When compiled with -DTRACY_ENABLE, connects to Tracy Profiler GUI.
// When TRACY_ENABLE is not defined, all macros expand to no-ops (0% overhead).
// ══════════════════════════════════════════════════════════════════════════════

#ifdef TRACY_ENABLE

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "../tools/tracy/tracy/Tracy.hpp"

#define PROFILE_FRAME(name)             FrameMarkNamed(name)
#define PROFILE_FRAME_END               FrameMark
#define PROFILE_ZONE(name)              ZoneScopedN(name)
#define PROFILE_ZONE_C(name, color)     ZoneScopedNC(name, color)
#define PROFILE_ZONE_AUTO               ZoneScoped
#define PROFILE_PLOT(name, val)         TracyPlot(name, static_cast<int64_t>(val))
#define PROFILE_PLOT_F(name, val)       TracyPlot(name, static_cast<double>(val))
#define PROFILE_MESSAGE(msg, len)       TracyMessage(msg, len)
#define PROFILE_MESSAGE_L(literal)      TracyMessageL(literal)

#else

#define PROFILE_FRAME(name)
#define PROFILE_FRAME_END
#define PROFILE_ZONE(name)
#define PROFILE_ZONE_C(name, color)
#define PROFILE_ZONE_AUTO
#define PROFILE_PLOT(name, val)
#define PROFILE_PLOT_F(name, val)
#define PROFILE_MESSAGE(msg, len)
#define PROFILE_MESSAGE_L(literal)

#endif
