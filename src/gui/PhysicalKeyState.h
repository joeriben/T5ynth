#pragma once

namespace t5
{
// True if the given macOS virtual keycode (kVK_ANSI_*, the physical key POSITION)
// is currently held. Layout-INDEPENDENT on macOS (reads hardware key state by
// position); falls back to JUCE's layout-dependent character state elsewhere.
bool physicalKeyDown (int virtualKeyCode);
}
