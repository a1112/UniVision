#pragma once

namespace WindowEffectsPolicy {
inline bool materialAllowed(bool enabled, bool reduceTransparency, bool highContrast)
{
    return enabled && !reduceTransparency && !highContrast;
}
inline int cornerRadius(bool maximized, bool fullscreen)
{
    return maximized || fullscreen ? 0 : 10;
}
}
