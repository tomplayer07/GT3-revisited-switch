#pragma once
#include "input/virtual_pad.h"
namespace gt3r {
// Developer-tool prompts use physical A/B on Switch; game bindings stay untouched.
#if defined(__SWITCH__)
inline constexpr PadButton tool_confirm=PadButton::Circle, tool_back=PadButton::Cross;
#else
inline constexpr PadButton tool_confirm=PadButton::Cross, tool_back=PadButton::Circle;
#endif
}
