#pragma once
#include <JuceHeader.h>

/*  The instrument's name, spelled once.

    "akróasys" carries a non-ASCII character. A raw UTF-8 literal handed to
    juce::String's const char* constructor mojibakes (JUCE documents that
    constructor as ASCII-only), so the name is written as UTF-8 escapes and
    converted with fromUTF8. The escape is split before "asys" because \x is
    greedy in C++ — "\xc3\xb3asys" would parse the "a" as a third hex digit.

    Use productName() in anything the user reads. Use kProductNameAscii where
    only ASCII is safe or meaningful: filenames, filesystem paths, HTTP headers,
    thread names. The two are deliberately different words, not a fallback pair —
    the app bundle really is called "akroasys".
*/
inline juce::String productName()
{
    return juce::String::fromUTF8("akr\xc3\xb3" "asys");
}

inline constexpr const char* kProductNameAscii = "akroasys";
