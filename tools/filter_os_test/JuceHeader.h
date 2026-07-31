// Minimal JUCE shim — just enough for LadderFilter.h / CutoffWarpFilter.h to
// compile standalone (offline aliasing test). NOT for use in the plugin build.
#pragma once
#include <algorithm>
#include <cmath>

namespace juce
{
template <typename T>
struct MathConstants
{
    static constexpr T pi     = static_cast<T>(3.14159265358979323846);
    static constexpr T halfPi = static_cast<T>(1.57079632679489661923);
    static constexpr T twoPi  = static_cast<T>(6.28318530717958647692);
};

template <typename T> inline T jlimit (T lo, T hi, T v) { return v < lo ? lo : (v > hi ? hi : v); }
template <typename T> inline T jmax   (T a, T b)        { return a > b ? a : b; }
template <typename T> inline T jmin   (T a, T b)        { return a < b ? a : b; }
}
