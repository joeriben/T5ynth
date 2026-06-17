#pragma once
#include <JuceHeader.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <limits>
#include <vector>

// ── Color constants (shared across all GUI files) ──────────────────────────
static const auto kAccent  = juce::Colour(0xffe91e63);  // C — Pink (engine accent)
static const auto kTextPrimary   = juce::Colour(0xffe7edf5);
static const auto kTextSecondary = juce::Colour(0xffc2cad7);
static const auto kTextMuted     = juce::Colour(0xff9aa6b8);
static const auto kTextDisabled  = juce::Colour(0xff768294);
static const auto kDim     = kTextSecondary;
static const auto kDimmer  = kTextMuted;
static const auto kSurface = juce::Colour(0xff1e2130);  // Slider track bg, input fields
static const auto kCard    = juce::Colour(0xff1a1e2a);  // Card/section background
static const auto kBg      = juce::Colour(0xff0e1018);  // Main background (dark blue-gray, not black)
static const auto kBorder  = juce::Colour(0xff353a4a);  // Section borders (more visible)

static constexpr float kUiLabelFontMin = 11.0f;
static constexpr float kUiValueFontMin = 11.0f;
static constexpr float kUiControlFontMin = 11.0f;

// ── Type scale ─────────────────────────────────────────────────────────────
// Named typographic roles derived from ONE responsive base size (the `f` unit
// each panel already computes in resized()). A panel asks for a role instead of
// hand-tuning each label's point size, so captions / values / titles stay in
// step across the whole UI. Only the base flexes with panel height; the ratios
// are fixed. This is the systemic replacement for the per-label font fiddling
// that let the prompt captions drift out of sync.
enum class TextRole
{
    ModuleTitle,  // accent header-strip caption inside a ModuleBox
    Caption,      // a control's label (e.g. "Duration")
    Value,        // numeric read-out (e.g. "3.00s")
    Hint          // secondary / helper text
};

inline float uiFontSize(TextRole role, float base)
{
    switch (role)
    {
        case TextRole::ModuleTitle: return juce::jmax(kUiLabelFontMin, base * 0.92f);
        case TextRole::Caption:     return juce::jmax(kUiLabelFontMin, base);
        case TextRole::Value:       return juce::jmax(kUiValueFontMin, base);
        case TextRole::Hint:        return juce::jmax(10.0f,           base * 0.82f);
    }
    return base;
}

inline juce::Font uiFont(TextRole role, float base, bool bold = false)
{
    return juce::Font(juce::FontOptions(uiFontSize(role, base),
                                        bold ? juce::Font::bold : juce::Font::plain));
}

inline void setUiFont(juce::Label& l, TextRole role, float base, bool bold = false)
{
    l.setFont(uiFont(role, base, bold));
}

// Semantic axis colors
static const auto kAxis1   = juce::Colour(0xffe91e63);  // Pink
static const auto kAxis2   = juce::Colour(0xff2196f3);  // Blue
static const auto kAxis3   = juce::Colour(0xff4caf50);  // Green

// PCA axis colors (muted variants)
static const auto kPca1    = juce::Colour(0xffff9800);  // Orange
static const auto kPca2    = juce::Colour(0xff9c27b0);  // Purple
static const auto kPca3    = juce::Colour(0xff00bcd4);  // Cyan
static const auto kPca4    = juce::Colour(0xffcddc39);  // Lime
static const auto kPca5    = juce::Colour(0xffff5722);  // Deep orange
static const auto kPca6    = juce::Colour(0xff8bc34a);  // Light green

// Section accent colors — UCDCAE brand palette in logo order
// U=#667eea  C=#e91e63  D=#7C4DFF  C=#FF6F00  A=#4CAF50  E=#00BCD4
static const auto kFilterCol = juce::Colour(0xff7C4DFF);  // D — Violet (filter)
static const auto kEnvCol    = juce::Colour(0xffFF6F00);  // C₂ — Amber (envelopes)
static const auto kModCol    = juce::Colour(0xffFF6F00);  // C₂ — Amber (LFOs)
static const auto kLfoCol    = juce::Colour(0xffFF6F00);  // C₂ — Amber (LFOs)
static const auto kDriftCol  = juce::Colour(0xffe65100);  // C₂ darker variant (drift)

// Module lead colors (UCDCAE: C=Engine D=Filter U=Osc C₂=Mod A=Seq E=FX)
static const auto kOscCol    = juce::Colour(0xff667eea);  // U — Periwinkle (prompt/osc)
static const auto kSeqCol    = juce::Colour(0xff4CAF50);  // A — Green (sequencer)
static const auto kFxCol     = juce::Colour(0xff00BCD4);  // E — Cyan (effects)

// Impulse A/B identity — shared by the prompt fields, the A↔B slider gradient,
// and the Dimension Explorer bars. Periwinkle (A) / yellow (B): a complementary,
// colorblind-safe pair (verified WCAG/ΔE2000/CVD). A reuses the osc periwinkle =
// "the original" purple. The A *editor text* uses a lifted periwinkle for body-
// text legibility (≈6.4:1 on kCard) while saturated #667eea stays the accent/
// gradient; the gradient pivots through a dark neutral grey so the complementary
// pair never muddies to olive at the midpoint (a desaturated pivot avoids olive
// without the bright white centre reading as the dominant colour).
static const auto kImpulseA     = kOscCol;                   // periwinkle #667eea (identity / gradient / bars)
static const auto kImpulseAText = juce::Colour(0xff8A9BF7);  // lifted periwinkle for the A prompt text
static const auto kImpulseB     = juce::Colour(0xffFFD23F);  // warm gold (complementary; less green than neon yellow)
static const auto kImpulseMid   = juce::Colour(0xff3D4250);  // dark neutral-grey gradient pivot
// Warm amber waypoint in the grey→gold (B) half of the blend. A straight RGB
// lerp from the cool grey pivot to the gold passes through a dark desaturated
// yellow that reads as olive/green; routing through this dark amber keeps the
// transition warm (brown→gold) so the slider's lower half matches the gold text.
static const auto kImpulseBWarm = juce::Colour(0xffA66A22);  // dark amber (B-half waypoint)

/** Linear per-channel interpolation between two colours (t = 0→a, 1→b). */
inline juce::Colour lerpColour(juce::Colour a, juce::Colour b, float t)
{
    t = juce::jlimit(0.0f, 1.0f, t);
    return juce::Colour::fromFloatRGBA(
        a.getFloatRed()   + (b.getFloatRed()   - a.getFloatRed())   * t,
        a.getFloatGreen() + (b.getFloatGreen() - a.getFloatGreen()) * t,
        a.getFloatBlue()  + (b.getFloatBlue()  - a.getFloatBlue())  * t,
        a.getFloatAlpha() + (b.getFloatAlpha() - a.getFloatAlpha()) * t);
}

/** A↔B blend colour for normalized position t (0 = A, 1 = B), pivoting through a
 *  dark neutral grey so the complementary purple↔yellow pair never muddies to
 *  olive. Used by the A↔B slider track gradient and its position-coloured thumb. */
inline juce::Colour abBlendColour(float t)
{
    t = juce::jlimit(0.0f, 1.0f, t);
    // A→grey for the lower half; grey→amber→gold for the upper half so the
    // warm side never passes through olive (see kImpulseBWarm).
    if (t < 0.5f)  return lerpColour(kImpulseA, kImpulseMid, t * 2.0f);
    if (t < 0.75f) return lerpColour(kImpulseMid, kImpulseBWarm, (t - 0.5f) * 4.0f);
    return lerpColour(kImpulseBWarm, kImpulseB, (t - 0.75f) * 4.0f);
}

/** Configure a label as an inverted section header bar (colored bg, dark text). */
inline void paintSectionHeader(juce::Label& lbl, const juce::String& text, juce::Colour col)
{
    lbl.setText(" " + text, juce::dontSendNotification);
    lbl.setColour(juce::Label::textColourId, juce::Colour(0xff0e1018));
    lbl.setColour(juce::Label::backgroundColourId, col.withAlpha(0.7f));
    lbl.setJustificationType(juce::Justification::centredLeft);
}

/** Paint a card background with subtle border (sharp corners). */
inline void paintCard(juce::Graphics& g, juce::Rectangle<int> bounds)
{
    g.setColour(kCard);
    g.fillRect(bounds);
    g.setColour(kBorder);
    g.drawRect(bounds, 1);
}

/** Paint a border around a switchbox button group (sharp corners). */
inline void paintSwitchBoxBorder(juce::Graphics& g, juce::Rectangle<int> bounds)
{
    if (bounds.isEmpty())
        return;
    g.setColour(kBorder);
    // Stroke 1px OUTSIDE the button union. The group's segment buttons are
    // child components, painted AFTER this (their parent) and filling their
    // full bounds with kSurface — a frame drawn on the union edge is overdrawn
    // by them (the long-standing "no visible switchbox frame" bug). The 1px
    // expansion lands the stroke in the parent-only margin just outside the
    // buttons, so it survives. (paintCard stays visible because ModuleBox
    // insets its content instead.)
    g.drawRect(bounds.expanded(1), 1);
}

// ── Unified switchbox design system ──────────────────────────────────────────
// Every radio-style switchbox (a row/column of TextButtons backed by a hidden
// ComboBox/Slider for APVTS) shares ONE visual language:
//   • body         = kSurface
//   • whole group  = framed once by paintSwitchBoxBorder (1px kBorder)
//   • selected seg = filled with the section accent
//   • off text     = kDim
//   • on  text     = white on dark accents, dark ink on the bright ones
// Call styleSwitchButton() on each segment, lay the segments out adjacent (no
// internal gaps), and paint the frame on the union of their bounds. The global
// LookAndFeel draws the fill (drawButtonBackground) and honours these colour
// IDs, so the look stays in one place.

/** Selected-segment text colour: white on dark accents, dark ink (#0e1018) on
 *  the bright accents (green/cyan/amber) where white drops below the WCAG UI
 *  contrast floor. The threshold sits between drift-orange (stays white, as
 *  approved on the REGENERATE box) and sequencer-green (flips to dark ink). */
inline juce::Colour switchBoxSelectedTextColour(juce::Colour accent)
{
    return accent.getPerceivedBrightness() > 0.55f ? juce::Colour(0xff0e1018)
                                                   : juce::Colours::white;
}

/** Apply the unified switchbox colours to one segment button. */
inline void styleSwitchButton(juce::TextButton& b, juce::Colour accent)
{
    b.setColour(juce::TextButton::buttonColourId,   kSurface);
    b.setColour(juce::TextButton::buttonOnColourId, accent);
    b.setColour(juce::TextButton::textColourOffId,  kDim);
    b.setColour(juce::TextButton::textColourOnId,   switchBoxSelectedTextColour(accent));
}

// Drawn glyphs for switchbox segments where a symbol reads faster than a word:
// arp modes (arrows) and note divisions (note heads). Drawn as juce::Path so
// they never depend on a font carrying the Unicode musical symbols. A button
// opts in via setSwitchGlyph(); T5ynthLookAndFeel::drawButtonText renders it
// tinted with the button's resolved text colour (so it inherits the per-accent
// on/off colours above). Enumerator order MATCHES the APVTS choice order for
// the two parameters so a button index maps straight to its glyph.
enum class SwitchGlyph
{
    ArpOff, ArpUp, ArpDown, ArpUpDown, ArpRandom,                 // ArpMode:     Off,Up,Down,UpDown,Random
    NoteWhole, NoteHalf, NoteQuarter, NoteEighth, NoteSixteenth,  // SeqDivision: 1/1,1/2,1/4,1/8,1/16
    numGlyphs
};

inline void setSwitchGlyph(juce::TextButton& b, SwitchGlyph glyph)
{
    b.getProperties().set("glyphId", static_cast<int>(glyph));
}

/** Draw a switchbox glyph centred in `area`, tinted `colour`. Geometry lives in
 *  a 0..24 design box scaled to fit (the Icon-registry convention). */
inline void drawSwitchGlyph(juce::Graphics& g, SwitchGlyph glyph,
                            juce::Rectangle<float> area, juce::Colour colour)
{
    const float s = juce::jmin(area.getWidth(), area.getHeight());
    if (s <= 0.0f)
        return;
    // Geometry is authored in a 0..24 box and mapped by T; juce strokePath bakes
    // T into the vertices but takes the pen width RAW, so widths are scaled here
    // by the same factor (k) to keep the SVG-mockup proportions at any size.
    const float k = s / 24.0f;
    auto box = juce::Rectangle<float>(s, s).withCentre(area.getCentre());
    const auto T = juce::AffineTransform::scale(k).translated(box.getX(), box.getY());
    g.setColour(colour);

    auto stroke = [&](const juce::Path& p, float w)
    {
        g.strokePath(p, juce::PathStrokeType(w * k, juce::PathStrokeType::curved,
                                             juce::PathStrokeType::rounded), T);
    };

    switch (glyph)
    {
        case SwitchGlyph::ArpOff:
        {
            juce::Path p;
            p.addEllipse(4.5f, 4.5f, 15.0f, 15.0f);
            p.startNewSubPath(7.6f, 7.6f); p.lineTo(16.4f, 16.4f);
            stroke(p, 2.0f);
            break;
        }
        case SwitchGlyph::ArpUp:
        {
            juce::Path p;
            p.startNewSubPath(12.0f, 19.0f); p.lineTo(12.0f, 6.0f);
            p.startNewSubPath(7.3f, 10.7f);  p.lineTo(12.0f, 6.0f); p.lineTo(16.7f, 10.7f);
            stroke(p, 2.3f);
            break;
        }
        case SwitchGlyph::ArpDown:
        {
            juce::Path p;
            p.startNewSubPath(12.0f, 5.0f);  p.lineTo(12.0f, 18.0f);
            p.startNewSubPath(7.3f, 13.3f);  p.lineTo(12.0f, 18.0f); p.lineTo(16.7f, 13.3f);
            stroke(p, 2.3f);
            break;
        }
        case SwitchGlyph::ArpUpDown:
        {
            juce::Path p;
            p.startNewSubPath(12.0f, 4.0f);  p.lineTo(12.0f, 20.0f);
            p.startNewSubPath(8.3f, 7.7f);   p.lineTo(12.0f, 4.0f);  p.lineTo(15.7f, 7.7f);
            p.startNewSubPath(8.3f, 16.3f);  p.lineTo(12.0f, 20.0f); p.lineTo(15.7f, 16.3f);
            stroke(p, 2.1f);
            break;
        }
        case SwitchGlyph::ArpRandom:
        {
            juce::Path frame;
            frame.addRoundedRectangle(5.0f, 5.0f, 14.0f, 14.0f, 3.0f);
            stroke(frame, 1.8f);
            juce::Path dots;
            dots.addEllipse(7.7f,  7.7f,  2.6f, 2.6f);
            dots.addEllipse(10.7f, 10.7f, 2.6f, 2.6f);
            dots.addEllipse(13.7f, 13.7f, 2.6f, 2.6f);
            g.fillPath(dots, T);
            break;
        }
        case SwitchGlyph::NoteWhole:
        case SwitchGlyph::NoteHalf:
        case SwitchGlyph::NoteQuarter:
        case SwitchGlyph::NoteEighth:
        case SwitchGlyph::NoteSixteenth:
        {
            const bool open    = (glyph == SwitchGlyph::NoteWhole || glyph == SwitchGlyph::NoteHalf);
            const bool hasStem = (glyph != SwitchGlyph::NoteWhole);

            juce::Path head;
            if (glyph == SwitchGlyph::NoteWhole)
                head.addEllipse(6.6f, 9.0f, 10.8f, 6.4f);
            else
                head.addEllipse(5.2f, 13.2f, 7.8f, 5.4f);

            if (open)
                g.strokePath(head, juce::PathStrokeType(1.9f * k), T);
            else
                g.fillPath(head, T);

            if (hasStem)
            {
                juce::Path stem_;
                stem_.startNewSubPath(12.7f, 15.9f); stem_.lineTo(12.7f, 5.0f);
                stroke(stem_, 2.0f);
            }

            auto flag = [&](float yTop)
            {
                juce::Path f;
                f.startNewSubPath(12.7f, yTop);
                f.cubicTo(16.8f, yTop + 1.6f, 17.0f, yTop + 4.6f, 14.2f, yTop + 6.8f);
                stroke(f, 1.8f);
            };
            if (glyph == SwitchGlyph::NoteEighth)
                flag(5.0f);
            else if (glyph == SwitchGlyph::NoteSixteenth)
            {
                flag(5.0f);
                flag(8.4f);
            }
            break;
        }
        case SwitchGlyph::numGlyphs:
        default:
            break;
    }
}

inline int measureTextWidth(const juce::String& text, float fontSize)
{
    if (text.isEmpty())
        return 0;

    juce::GlyphArrangement glyphs;
    glyphs.addLineOfText(juce::Font(juce::FontOptions(fontSize)), text, 0.0f, 0.0f);
    return juce::roundToInt(std::ceil(glyphs.getBoundingBox(0, -1, true).getWidth()));
}

// ── Icon registry ──────────────────────────────────────────────────────────
// Central line-art icon set. Each glyph is a juce::Path in a 0..24 viewBox,
// built once and cached (same spirit as CurveButton's cached SVG Drawables),
// then stroked at any size/colour via getTransformToScaleToFit (the
// ClockButtonLnF pattern). Line-art + tint-on-draw lets one glyph serve both as
// a dark mark on an accent strip and as a coloured mark on a card. Extend by
// adding an enumerator (before numIcons) and a case in buildIconPath().
enum class Icon
{
    Clock,    // duration
    Shuffle,  // variation (category) / seed: auto
    Ban,      // seed: none — fixed base seed, no variation
    Lock,     // seed: last — reuse the previous seed
    numIcons
};

namespace icon_detail
{
    inline juce::Path buildIconPath(Icon id)
    {
        juce::Path p;
        constexpr float halfPi = juce::MathConstants<float>::halfPi;
        switch (id)
        {
            case Icon::Clock:
                p.addEllipse(2.5f, 2.5f, 19.0f, 19.0f);
                p.startNewSubPath(12.0f, 12.0f); p.lineTo(12.0f, 6.5f);    // minute hand (up)
                p.startNewSubPath(12.0f, 12.0f); p.lineTo(16.0f, 13.5f);   // hour hand (~4 o'clock)
                break;

            case Icon::Shuffle:
                // two arrows entering from the left, crossing, exiting right
                p.startNewSubPath(3.0f, 6.5f);  p.lineTo(8.0f, 6.5f);  p.lineTo(16.0f, 17.5f); p.lineTo(20.5f, 17.5f);
                p.startNewSubPath(17.5f, 14.5f); p.lineTo(20.5f, 17.5f); p.lineTo(17.5f, 20.5f); // head →↘
                p.startNewSubPath(3.0f, 17.5f); p.lineTo(8.0f, 17.5f); p.lineTo(16.0f, 6.5f);  p.lineTo(20.5f, 6.5f);
                p.startNewSubPath(17.5f, 3.5f);  p.lineTo(20.5f, 6.5f);  p.lineTo(17.5f, 9.5f);  // head →↗
                break;

            case Icon::Ban:
                p.addEllipse(3.0f, 3.0f, 18.0f, 18.0f);
                p.startNewSubPath(6.2f, 6.2f); p.lineTo(17.8f, 17.8f);     // diagonal slash
                break;

            case Icon::Lock:
                p.addRoundedRectangle(6.0f, 11.0f, 12.0f, 9.0f, 1.5f);     // body
                p.addCentredArc(12.0f, 11.0f, 3.6f, 4.2f, 0.0f,           // shackle (upper semicircle)
                                -halfPi, halfPi, true);
                break;

            case Icon::numIcons:
            default:
                break;
        }
        return p;
    }

    inline const juce::Path& iconPath(Icon id)
    {
        static std::array<juce::Path, static_cast<size_t>(Icon::numIcons)> cache;
        static std::array<bool,       static_cast<size_t>(Icon::numIcons)> built {};
        const auto i = static_cast<size_t>(
            juce::jlimit(0, static_cast<int>(Icon::numIcons) - 1, static_cast<int>(id)));
        if (!built[i]) { cache[i] = buildIconPath(static_cast<Icon>(i)); built[i] = true; }
        return cache[i];
    }
}

/** Stroke a registry icon centred within `area`, tinted `colour`. */
inline void drawIcon(juce::Graphics& g, Icon id, juce::Rectangle<float> area,
                     juce::Colour colour, float strokeWidth = 1.6f)
{
    const auto& path = icon_detail::iconPath(id);
    if (path.isEmpty() || area.isEmpty())
        return;
    g.setColour(colour);
    g.strokePath(path, juce::PathStrokeType(strokeWidth,
                                            juce::PathStrokeType::curved,
                                            juce::PathStrokeType::rounded),
                 path.getTransformToScaleToFit(area, true));
}

/**
 * Self-contained parameter module: a card with an accent header strip
 * (icon + title) above a content area the owner fills with controls. This is
 * the mid-level template the panels were missing — instead of hand-placing a
 * floating label + control + value (which drifts), a panel drops a ModuleBox,
 * configures it once, and lays its controls inside getContentBounds().
 *
 * Decorative only: it intercepts no mouse events and must sit BEHIND the
 * controls it frames — call toBack() after adding it. Header colours match
 * paintSectionHeader (dark glyph/text on accent.withAlpha(0.7)).
 */
class ModuleBox : public juce::Component
{
public:
    ModuleBox() { setInterceptsMouseClicks(false, false); }

    void configure(const juce::String& title, juce::Colour accent, Icon icon)
    {
        title_  = title;
        accent_ = accent;
        icon_   = icon;
        repaint();
    }

    /** Base font unit (the panel's responsive `f`); drives the title size. */
    void setBaseFont(float f)       { baseFont_   = f; }
    void setHeaderHeight(int h)     { headerH_    = juce::jmax(0, h); }
    void setContentPadding(int p)   { contentPad_ = juce::jmax(0, p); }
    int  getHeaderHeight() const    { return headerH_; }

    /** Region the owner places controls into (below the header, inset by pad).
     *  PARENT-relative on purpose: the owning panel lays out its controls as
     *  SIBLINGS of this box (the box is decorative and intercepts no mouse), so
     *  this rect must be in the parent's coordinate space — getBounds(), not
     *  getLocalBounds(). (Using local 0-based coords placed siblings at the
     *  panel's top-left instead of inside the box.) */
    juce::Rectangle<int> getContentBounds() const
    {
        auto b = getBounds();
        b.removeFromTop(headerH_);
        return b.reduced(contentPad_);
    }

    void paint(juce::Graphics& g) override
    {
        auto b = getLocalBounds();
        paintCard(g, b);

        auto header = b.removeFromTop(headerH_);
        g.setColour(accent_.withAlpha(0.7f));
        g.fillRect(header);

        const auto darkInk = juce::Colour(0xff0e1018);
        auto inner = header.reduced(headerPadX_, 0);
        if (icon_ != Icon::numIcons && header.getHeight() > 0)
        {
            auto iconCell = inner.removeFromLeft(header.getHeight());
            drawIcon(g, icon_, iconCell.toFloat().reduced(static_cast<float>(iconInset_)),
                     darkInk, 1.6f);
            inner.removeFromLeft(juce::jmax(2, headerPadX_ / 2));
        }
        g.setColour(darkInk);
        g.setFont(uiFont(TextRole::ModuleTitle, baseFont_, true));
        g.drawText(title_, inner, juce::Justification::centredLeft, false);
    }

private:
    juce::String title_;
    juce::Colour accent_ { kOscCol };
    Icon  icon_       { Icon::numIcons };
    float baseFont_   { 13.0f };
    int   headerH_    { 18 };
    int   headerPadX_ { 6 };
    int   iconInset_  { 3 };
    int   contentPad_ { 5 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ModuleBox)
};

/**
 * LookAndFeel for a compact icon button group (sharp-rect, on/off states).
 * Draws the registry Icon read from the button's "iconId" property; falls back
 * to the button text when no icon is set. Use for radio-style switchboxes where
 * a glyph reads faster than a word (e.g. the Variation none/last/auto selector).
 * Owners MUST declare the LnF instance BEFORE the buttons that use it (so it is
 * destroyed AFTER them) and must NEVER call setLookAndFeel(nullptr) — JUCE's
 * normal Component teardown is enough. (Same contract as ClockButtonLnF.)
 */
class IconButtonLnF : public juce::LookAndFeel_V4
{
public:
    juce::Colour onColour  { kOscCol };
    juce::Colour offColour { kSurface };

    void drawButtonBackground(juce::Graphics& g, juce::Button& b,
                              const juce::Colour&, bool over, bool down) override
    {
        const bool on = b.getToggleState();
        auto base = on ? onColour : offColour;
        if (down)      base = base.darker(0.15f);
        else if (over) base = base.brighter(0.10f);
        g.setColour(base);
        g.fillRect(b.getLocalBounds());
        g.setColour(kBorder);
        g.drawRect(b.getLocalBounds(), 1);
    }

    void drawButtonText(juce::Graphics& g, juce::TextButton& b,
                        bool over, bool /*down*/) override
    {
        const bool on = b.getToggleState();
        const auto col = on ? juce::Colours::white
                            : (over ? onColour.brighter(0.2f) : kDim);
        auto area = b.getLocalBounds().toFloat().reduced(3.0f);

        const int iconId = static_cast<int>(b.getProperties().getWithDefault("iconId", -1));
        if (iconId >= 0 && iconId < static_cast<int>(Icon::numIcons))
        {
            const float s = juce::jmin(area.getWidth(), area.getHeight());
            auto iconArea = area.withSizeKeepingCentre(s, s).reduced(s * 0.16f);
            drawIcon(g, static_cast<Icon>(iconId), iconArea, col, 1.7f);
            return;
        }

        g.setColour(col);
        g.setFont(juce::FontOptions(juce::jmax(kUiControlFontMin, area.getHeight() * 0.5f)));
        g.drawText(b.getButtonText(), area, juce::Justification::centred);
    }
};

enum class ResponsiveStripFallback
{
    none,
    overflow
};

struct ResponsiveStripItem
{
    int preferredWidth = 0;
    int minimumWidth = 0;
    // For items with fallback == overflow: the priority tier governs drop order.
    // Higher tiers are dropped to the overflow menu FIRST; the lowest positive
    // tier survives longest. Tier 0 items never overflow.
    int priorityTier = 0;
    bool flexible = false;
    ResponsiveStripFallback fallback = ResponsiveStripFallback::none;
};

struct ResponsiveStripResult
{
    std::vector<juce::Rectangle<int>> bounds;
    juce::Rectangle<int> overflowBounds;
    bool overflowUsed = false;
};

inline ResponsiveStripResult layoutResponsiveStrip(juce::Rectangle<int> area,
                                                   const std::vector<ResponsiveStripItem>& items,
                                                   int gap,
                                                   int overflowButtonWidth = 28)
{
    struct PlacedItem
    {
        int originalIndex = -1;
        int preferredWidth = 0;
        int minimumWidth = 0;
        bool isOverflow = false;
    };

    // Drop every overflow candidate whose tier is >= dropThreshold into the
    // overflow menu; keep the rest in the strip. A dropThreshold above all tiers
    // drops nothing; lowering it sheds tiers one band at a time, highest first.
    auto buildPlacedItems = [&](int dropThreshold) {
        std::vector<PlacedItem> placed;
        placed.reserve(items.size() + 1);

        bool insertedOverflow = false;
        for (size_t i = 0; i < items.size(); ++i)
        {
            const auto& item = items[i];
            const bool isOverflowCandidate = item.fallback == ResponsiveStripFallback::overflow
                                             && item.priorityTier > 0;

            if (isOverflowCandidate && item.priorityTier >= dropThreshold)
            {
                if (!insertedOverflow)
                {
                    placed.push_back({ -1, overflowButtonWidth, overflowButtonWidth, true });
                    insertedOverflow = true;
                }
                continue;
            }

            placed.push_back({ static_cast<int>(i), item.preferredWidth, item.minimumWidth, false });
        }

        return placed;
    };

    auto requiredWidthFor = [&](const std::vector<PlacedItem>& placed, bool preferred) {
        if (placed.empty())
            return 0;

        int total = gap * static_cast<int>(juce::jmax<int>(0, static_cast<int>(placed.size()) - 1));
        for (const auto& item : placed)
            total += preferred ? item.preferredWidth : item.minimumWidth;
        return total;
    };

    // Distinct droppable tiers, sorted high → low (the order they are shed in).
    std::vector<int> tiers;
    for (const auto& item : items)
        if (item.fallback == ResponsiveStripFallback::overflow && item.priorityTier > 0
            && std::find(tiers.begin(), tiers.end(), item.priorityTier) == tiers.end())
            tiers.push_back(item.priorityTier);
    std::sort(tiers.begin(), tiers.end(), std::greater<int>());

    // A threshold above every tier keeps the whole strip; lowering it sheds the
    // highest tier first. Pick the highest threshold (fewest items dropped) that
    // still fits at minimum widths. If even shedding everything won't fit, fall
    // back to shedding everything droppable as a best effort.
    const int dropNothing = std::numeric_limits<int>::max();
    int chosenThreshold = dropNothing;
    if (requiredWidthFor(buildPlacedItems(dropNothing), false) > area.getWidth())
    {
        chosenThreshold = tiers.empty() ? dropNothing : tiers.back();
        for (int t : tiers)
        {
            if (requiredWidthFor(buildPlacedItems(t), false) <= area.getWidth())
            {
                chosenThreshold = t;
                break;
            }
        }
    }

    auto placed = buildPlacedItems(chosenThreshold);

    ResponsiveStripResult result;
    result.bounds.resize(items.size());
    result.overflowUsed = std::any_of(placed.begin(), placed.end(),
                                      [](const PlacedItem& p) { return p.isOverflow; });

    if (placed.empty())
        return result;

    const int gapCount = juce::jmax<int>(0, static_cast<int>(placed.size()) - 1);
    const int totalGapWidth = gap * gapCount;

    int totalMin = totalGapWidth;
    int totalPreferred = totalGapWidth;
    for (const auto& item : placed)
    {
        totalMin += item.minimumWidth;
        totalPreferred += item.preferredWidth;
    }

    std::vector<int> widths;
    widths.reserve(placed.size());
    for (const auto& item : placed)
        widths.push_back(item.minimumWidth);

    int remaining = juce::jmax(0, area.getWidth() - totalMin);
    const int expandable = juce::jmax(0, totalPreferred - totalMin);

    if (remaining > 0 && expandable > 0)
    {
        std::vector<int> expansion(placed.size(), 0);
        int granted = 0;
        for (size_t i = 0; i < placed.size(); ++i)
        {
            const int delta = juce::jmax(0, placed[i].preferredWidth - placed[i].minimumWidth);
            const int extra = static_cast<int>((static_cast<int64_t>(remaining) * delta) / expandable);
            expansion[i] = extra;
            granted += extra;
        }

        int leftover = remaining - granted;
        for (size_t i = 0; i < placed.size() && leftover > 0; ++i)
        {
            const int delta = juce::jmax(0, placed[i].preferredWidth - placed[i].minimumWidth);
            if (expansion[i] < delta)
            {
                ++expansion[i];
                --leftover;
            }
        }

        for (size_t i = 0; i < placed.size(); ++i)
            widths[i] += expansion[i];
    }

    int x = area.getX();
    for (size_t i = 0; i < placed.size(); ++i)
    {
        juce::Rectangle<int> bounds(x, area.getY(), widths[i], area.getHeight());
        if (placed[i].isOverflow)
            result.overflowBounds = bounds;
        else
            result.bounds[static_cast<size_t>(placed[i].originalIndex)] = bounds;

        x += widths[i] + gap;
    }

    return result;
}

/**
 * Compact parameter control. Defaults to a horizontal row, but can be laid out
 * as a vertical fader in Easy views without changing the APVTS attachment.
 */
class SliderRow : public juce::Component
{
public:
    enum class LabelMode { Off, Positive, Negative };
    enum class ControlMode { Horizontal, Vertical, Knob };
    static constexpr float kKnobStartAngle = juce::MathConstants<float>::pi * 2.0f / 3.0f;
    static constexpr float kKnobEndAngle   = juce::MathConstants<float>::pi * 7.0f / 3.0f;

    SliderRow(const juce::String& name,
              std::function<juce::String(double)> formatter,
              juce::Colour trackColor = kAccent)
        : valueFormatter(std::move(formatter)),
          trackCol(trackColor)
    {
        label.setText(name, juce::dontSendNotification);
        label.setInterceptsMouseClicks(false, false);
        label.setJustificationType(juce::Justification::centredLeft);
        addAndMakeVisible(label);

        slider.setSliderStyle(juce::Slider::LinearHorizontal);
        slider.setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
        slider.setColour(juce::Slider::trackColourId, trackCol);
        slider.setColour(juce::Slider::backgroundColourId, trackCol.withAlpha(0.18f));
        slider.onValueChange = [this] { updateValue(); };
        addAndMakeVisible(slider);

        value.setColour(juce::Label::textColourId, trackCol);
        value.setJustificationType(juce::Justification::centredLeft);
        addAndMakeVisible(value);
        updateLabelAppearance();
    }

    juce::Slider& getSlider() { return slider; }
    juce::Label& getLabel() { return label; }
    juce::Label& getValueLabel() { return value; }
    int getPreferredWidth() const { return getLayoutProfile(false).preferredWidth; }
    int getMinimumWidth() const { return getLayoutProfile(true).minimumWidth; }
    // Layout-only override for cross-row column alignment. Any code deriving a new
    // forced width must start from getNaturalLabelWidthForAvailableWidth(), not
    // from the currently forced width, otherwise resize passes can feed back into
    // themselves and grow the reserved label column on each relayout.
    void setForcedLabelWidth(int width) { forcedLabelWidth = juce::jmax(0, width); }
    void clearForcedLabelWidth() { forcedLabelWidth = -1; }
    /** Force the value-text column to a fixed pixel width. Use this on rate
     *  and division rows that swap visibility so the slider track keeps the
     *  same on-screen position regardless of which formatter is active. */
    void setForcedValueWidth(int width) { forcedValueWidth = juce::jmax(0, width); }
    void clearForcedValueWidth() { forcedValueWidth = -1; }
    int getNaturalLabelWidthForAvailableWidth(int totalWidth) const
    {
        const int resolvedHeight = juce::jmax(18, getHeight() > 0 ? getHeight() : 22);
        return chooseLayout(totalWidth, resolvedHeight, false).labelWidth;
    }
    int getLabelWidthForAvailableWidth(int totalWidth) const
    {
        const int resolvedHeight = juce::jmax(18, getHeight() > 0 ? getHeight() : 22);
        return chooseLayout(totalWidth, resolvedHeight).labelWidth;
    }

    void setTrackColor(juce::Colour c)
    {
        trackCol = c;
        slider.setColour(juce::Slider::trackColourId, c);
        value.setColour(juce::Label::textColourId, c);
        updateLabelAppearance();
    }

    void setVerticalMode(bool shouldUseVertical)
    {
        setControlMode(shouldUseVertical ? ControlMode::Vertical : ControlMode::Horizontal);
    }

    void setKnobMode(bool shouldUseKnob)
    {
        setControlMode(shouldUseKnob ? ControlMode::Knob : ControlMode::Horizontal);
    }

    void setControlMode(ControlMode newMode)
    {
        if (controlMode == newMode)
            return;

        controlMode = newMode;
        if (controlMode == ControlMode::Vertical)
            slider.setSliderStyle(juce::Slider::LinearVertical);
        else if (controlMode == ControlMode::Knob)
        {
            slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
            slider.setRotaryParameters(kKnobStartAngle, kKnobEndAngle, true);
        }
        else
            slider.setSliderStyle(juce::Slider::LinearHorizontal);

        const bool centred = controlMode != ControlMode::Horizontal;
        label.setJustificationType(centred ? juce::Justification::centred
                                           : juce::Justification::centredLeft);
        value.setJustificationType(centred ? juce::Justification::centred
                                           : juce::Justification::centredLeft);
        resized();
        repaint();
    }

    void setLabelMode(LabelMode newMode)
    {
        if (labelMode == newMode)
            return;
        labelMode = newMode;
        updateLabelAppearance();
        repaint();
    }

    LabelMode getLabelMode() const { return labelMode; }
    void setLabelClickHandler(std::function<void()> handler) { onLabelClick = std::move(handler); }

    void updateValue()
    {
        if (valueFormatter)
            value.setText(valueFormatter(slider.getValue()), juce::dontSendNotification);
    }

    /** Set ghost target value. NaN = no ghost. Smoothing happens in tickGhost(). */
    void setGhostValue(float v) { ghostTarget = v; }
    void clearGhost() { setGhostValue(std::numeric_limits<float>::quiet_NaN()); }

    /** Advance ghost smoothing one frame. Call from a 30 Hz timer.
     *  Returns true if a repaint was triggered. */
    bool tickGhost()
    {
        if (std::isnan(ghostTarget))
        {
            if (!std::isnan(ghostSmoothed)) { ghostSmoothed = NaN_; repaint(); return true; }
            return false;
        }

        // Snap on first valid value, then one-pole smooth
        if (std::isnan(ghostSmoothed))
            ghostSmoothed = ghostTarget;
        else
            ghostSmoothed += (ghostTarget - ghostSmoothed) * kGhostSmooth;

        // Only repaint when pixel position actually changes
        float px = ghostToPixelX(ghostSmoothed);
        if (std::abs(px - lastGhostPx) > 0.5f) { lastGhostPx = px; repaint(); return true; }
        return false;
    }

    void paint(juce::Graphics& g) override
    {
        if (labelMode == LabelMode::Off)
            return;

        auto badge = getLabelBadgeBounds();
        if (badge.isEmpty())
            return;

        if (labelMode == LabelMode::Positive)
        {
            g.setColour(trackCol);
            g.fillRect(badge);
        }
        else if (labelMode == LabelMode::Negative)
        {
            g.setColour(trackCol);
            g.drawRect(badge.reduced(0.5f), 1.0f);
        }
    }

    void paintOverChildren(juce::Graphics& g) override
    {
        if (std::isnan(ghostSmoothed)) return;

        auto sb = slider.getBounds();
        float gx = static_cast<float>(sb.getCentreX());
        float gy = static_cast<float>(sb.getCentreY());
        float r = static_cast<float>(juce::jmin(sb.getWidth(), sb.getHeight())) * 0.28f;

        if (controlMode == ControlMode::Vertical)
        {
            double norm = slider.valueToProportionOfLength(static_cast<double>(ghostSmoothed));
            norm = juce::jlimit(0.0, 1.0, norm);
            const int thumb = slider.getLookAndFeel().getSliderThumbRadius(slider) * 2;
            const int trackY = sb.getY() + thumb / 2;
            const int trackH = sb.getHeight() - thumb;
            gy = static_cast<float>(trackY + trackH)
               - static_cast<float>(trackH) * static_cast<float>(norm);
        }
        else if (controlMode == ControlMode::Knob)
        {
            auto kb = sb.toFloat().reduced(2.0f);
            const float diameter = juce::jmin(kb.getWidth(), kb.getHeight());
            kb = juce::Rectangle<float>(diameter, diameter).withCentre(kb.getCentre());

            double norm = slider.valueToProportionOfLength(static_cast<double>(ghostSmoothed));
            norm = juce::jlimit(0.0, 1.0, norm);
            const float start = kKnobStartAngle;
            const float end = kKnobEndAngle;
            const float angle = start + (end - start) * static_cast<float>(norm);
            const float radius = diameter * 0.36f;
            gx = kb.getCentreX() + std::cos(angle) * radius;
            gy = kb.getCentreY() + std::sin(angle) * radius;
            r = juce::jmax(2.5f, diameter * 0.055f);
        }
        else
        {
            gx = ghostToPixelX(ghostSmoothed);
            r = static_cast<float>(sb.getHeight()) * 0.28f;
        }

        g.setColour(juce::Colour(0xccff9800)); // orange ghost
        g.fillEllipse(gx - r, gy - r, r * 2.0f, r * 2.0f);
    }

    void resized() override
    {
        auto b = getLocalBounds();
        if (controlMode == ControlMode::Vertical)
        {
            const int labelH = juce::jlimit(13, 20, juce::roundToInt(static_cast<float>(b.getHeight()) * 0.16f));
            const int valueH = juce::jlimit(13, 20, juce::roundToInt(static_cast<float>(b.getHeight()) * 0.16f));
            const float labelFs = juce::jlimit(kUiLabelFontMin, 12.0f,
                                               static_cast<float>(labelH) * 0.76f);
            const float valueFs = juce::jlimit(kUiValueFontMin, 12.0f,
                                               static_cast<float>(valueH) * 0.76f);
            label.setFont(juce::FontOptions(labelFs));
            value.setFont(juce::FontOptions(valueFs));
            label.setBounds(b.removeFromTop(labelH));
            value.setBounds(b.removeFromBottom(valueH));
            slider.setBounds(b.reduced(3, 1));
            return;
        }

        if (controlMode == ControlMode::Knob)
        {
            const int labelH = juce::jlimit(13, 22, juce::roundToInt(static_cast<float>(b.getHeight()) * 0.18f));
            const int valueH = juce::jlimit(13, 22, juce::roundToInt(static_cast<float>(b.getHeight()) * 0.18f));
            const float labelFs = juce::jlimit(kUiLabelFontMin, 13.0f,
                                               static_cast<float>(labelH) * 0.76f);
            const float valueFs = juce::jlimit(kUiValueFontMin, 13.0f,
                                               static_cast<float>(valueH) * 0.76f);
            label.setFont(juce::FontOptions(labelFs));
            value.setFont(juce::FontOptions(valueFs));
            label.setBounds(b.removeFromTop(labelH));
            value.setBounds(b.removeFromBottom(valueH));

            auto knobArea = b.reduced(1, 2);
            const int size = juce::jmax(1, juce::jmin(knobArea.getWidth(), knobArea.getHeight()));
            slider.setBounds(juce::Rectangle<int>(size, size).withCentre(knobArea.getCentre()));
            return;
        }

        const auto layout = chooseLayout(b.getWidth(), b.getHeight());
        label.setFont(juce::FontOptions(layout.labelFontSize));
        value.setFont(juce::FontOptions(layout.valueFontSize));

        label.setBounds(b.removeFromLeft(layout.labelWidth));
        value.setBounds(b.removeFromRight(layout.valueWidth));
        slider.setBounds(b);
    }

    void mouseUp(const juce::MouseEvent& e) override
    {
        if (onLabelClick && label.getBounds().contains(e.getPosition()))
            onLabelClick();
    }

private:
    struct SliderLayoutProfile
    {
        int labelWidth = 0;
        int valueWidth = 0;
        int minTrackWidth = 0;
        int preferredTrackWidth = 0;
        float labelFontSize = 9.0f;
        float valueFontSize = 9.0f;
        int minimumWidth = 0;
        int preferredWidth = 0;
    };

    juce::Label label, value;
    juce::Slider slider;
    std::function<juce::String(double)> valueFormatter;
    juce::Colour trackCol;
    LabelMode labelMode = LabelMode::Off;
    ControlMode controlMode = ControlMode::Horizontal;
    std::function<void()> onLabelClick;

    // Ghost marker smoothing
    static constexpr float NaN_ = std::numeric_limits<float>::quiet_NaN();
    static constexpr float kGhostSmooth = 0.3f;  // one-pole coeff, ~80 ms at 30 fps
    float ghostTarget   = NaN_;
    float ghostSmoothed = NaN_;
    float lastGhostPx   = -100.0f;
    int forcedLabelWidth = -1;
    int forcedValueWidth = -1;

    // getLabelBadgeBounds() result cache. See the function for why these inputs
    // are sufficient. Message-thread-only access — paint() is the sole caller.
    mutable bool badgeCacheValid_ = false;
    mutable juce::Rectangle<float> cachedBadgeBounds_;
    mutable juce::Rectangle<int> cachedBadgeLabelBounds_;
    mutable juce::String cachedBadgeText_;
    mutable float cachedBadgeFontSize_ = 0.0f;
    mutable int cachedBadgeJustFlags_ = 0;

    juce::String currentValueText() const
    {
        if (!value.getText().isEmpty())
            return value.getText();
        if (valueFormatter)
            return valueFormatter(slider.getValue());
        return {};
    }

    SliderLayoutProfile getLayoutProfile(bool compact, bool applyForcedLabelWidth = true) const
    {
        const int resolvedHeight = juce::jmax(18, getHeight() > 0 ? getHeight() : 22);
        const float maxFs = static_cast<float>(resolvedHeight) * 0.74f;
        const float labelFs = juce::jmax(kUiLabelFontMin, juce::jmin(maxFs, compact ? 11.0f : 12.0f));
        const float valueFs = juce::jmax(kUiValueFontMin, juce::jmin(maxFs, compact ? 11.0f : 12.0f));
        const int labelPadding = compact ? 8 : 12;
        const int valuePadding = compact ? 8 : 12;

        SliderLayoutProfile profile;
        profile.labelFontSize = labelFs;
        profile.valueFontSize = valueFs;
        profile.labelWidth = label.getText().isEmpty() ? 0 : measureTextWidth(label.getText(), labelFs) + labelPadding;
        profile.valueWidth = currentValueText().isEmpty() ? 0 : measureTextWidth(currentValueText(), valueFs) + valuePadding;
        if (applyForcedLabelWidth && forcedLabelWidth >= 0)
            profile.labelWidth = forcedLabelWidth;
        if (applyForcedLabelWidth && forcedValueWidth >= 0)
            profile.valueWidth = forcedValueWidth;
        profile.minTrackWidth = compact ? 40 : 64;
        profile.preferredTrackWidth = compact ? 70 : 112;
        profile.minimumWidth = profile.labelWidth + profile.valueWidth + profile.minTrackWidth;
        profile.preferredWidth = profile.labelWidth + profile.valueWidth + profile.preferredTrackWidth;
        return profile;
    }

    SliderLayoutProfile chooseLayout(int totalWidth, int height, bool applyForcedLabelWidth = true) const
    {
        juce::ignoreUnused(height);

        auto profile = getLayoutProfile(false, applyForcedLabelWidth);
        if (totalWidth > 0 && totalWidth < profile.preferredWidth)
            profile = getLayoutProfile(true, applyForcedLabelWidth);

        int overflow = profile.minimumWidth - totalWidth;
        if (overflow > 0)
        {
            // CRITICAL for cross-row column alignment: when label/value widths
            // are FORCED, the row is part of a coordinated multi-row column.
            // Empty-label rows (e.g. delay Time/Division with a clock button
            // overlay) must shrink to the SAME minimum as their non-empty
            // siblings (e.g. Damp). Otherwise the empty row's label can
            // shrink to 0 while the non-empty row's label only shrinks to
            // ~"M"+2 px, drifting the slider track X by the difference.
            const bool labelForced = applyForcedLabelWidth && forcedLabelWidth >= 0;
            const bool valueForced = applyForcedLabelWidth && forcedValueWidth >= 0;
            const int minLabelWidth = (label.getText().isEmpty() && !labelForced)
                ? 0
                : measureTextWidth("M", profile.labelFontSize) + 2;
            const int minValueWidth = (currentValueText().isEmpty() && !valueForced)
                ? 0
                : measureTextWidth("00", profile.valueFontSize) + 2;
            const int labelShrink = juce::jmin(overflow / 2 + overflow % 2,
                                               juce::jmax(0, profile.labelWidth - minLabelWidth));
            profile.labelWidth -= labelShrink;
            overflow -= labelShrink;
            profile.valueWidth -= juce::jmin(overflow, juce::jmax(0, profile.valueWidth - minValueWidth));
        }

        return profile;
    }

    float ghostToPixelX(float v)
    {
        auto sb = slider.getBounds();
        double norm = slider.valueToProportionOfLength(static_cast<double>(v));
        norm = juce::jlimit(0.0, 1.0, norm);
        int thumbW = slider.getLookAndFeel().getSliderThumbRadius(slider) * 2;
        return static_cast<float>(sb.getX() + thumbW / 2)
             + static_cast<float>(sb.getWidth() - thumbW) * static_cast<float>(norm);
    }

    juce::Rectangle<float> getLabelBadgeBounds() const
    {
        // Cache invalidation key: badge bounds depend only on label's bounds, text,
        // font height, and justification. None of these change during a paint cycle,
        // and most of the time they don't change between frames either — so the
        // measureTextWidth() call (Font + GlyphArrangement construction → HarfBuzz/
        // CoreText resolve, profile-hot under modulation-driven repaints) only runs
        // on actual input changes.
        const auto labelBoundsInt = label.getBounds();
        const auto& text = label.getText();
        const float fontSize = label.getFont().getHeight();
        const int justFlags = label.getJustificationType().getFlags();

        if (badgeCacheValid_
            && cachedBadgeLabelBounds_ == labelBoundsInt
            && cachedBadgeFontSize_ == fontSize
            && cachedBadgeJustFlags_ == justFlags
            && cachedBadgeText_ == text)
        {
            return cachedBadgeBounds_;
        }

        auto compute = [&]() -> juce::Rectangle<float>
        {
            auto lb = labelBoundsInt.toFloat();
            if (lb.isEmpty() || text.isEmpty())
                return {};

            const float textW = static_cast<float>(measureTextWidth(text, fontSize));
            const float insetX = 1.0f;
            const float insetY = 1.0f;
            const float padX = juce::jmax(2.5f, fontSize * 0.22f);
            const float padY = juce::jmax(0.5f, fontSize * 0.10f);
            const float badgeW = juce::jmin(lb.getWidth() - insetX, textW + padX * 2.0f);
            const float badgeH = juce::jmin(lb.getHeight() - insetY * 2.0f, fontSize + padY * 2.0f);
            const auto justification = label.getJustificationType();
            float badgeX;
            if (justification.testFlags(juce::Justification::horizontallyCentred))
                badgeX = std::floor(lb.getCentreX() - badgeW * 0.5f);
            else if (justification.testFlags(juce::Justification::right))
                badgeX = std::floor(lb.getRight() - insetX - badgeW);
            else
                badgeX = std::floor(lb.getX() + insetX);
            const float badgeY = std::floor(lb.getY() + (lb.getHeight() - badgeH) * 0.5f);
            return { badgeX, badgeY, std::floor(badgeW), std::floor(badgeH) };
        };

        cachedBadgeBounds_ = compute();
        cachedBadgeLabelBounds_ = labelBoundsInt;
        cachedBadgeText_ = text;
        cachedBadgeFontSize_ = fontSize;
        cachedBadgeJustFlags_ = justFlags;
        badgeCacheValid_ = true;
        return cachedBadgeBounds_;
    }

    void updateLabelAppearance()
    {
        juce::Colour textColour = kTextSecondary;
        auto border = juce::BorderSize<int>(1, 5, 1, 5);
        if (labelMode == LabelMode::Positive)
        {
            textColour = juce::Colours::white;
            border = juce::BorderSize<int>(1, 3, 1, 3);
        }
        else if (labelMode == LabelMode::Negative)
        {
            textColour = kTextMuted;
            border = juce::BorderSize<int>(1, 3, 1, 3);
        }
        label.setColour(juce::Label::textColourId, textColour);
        label.setBorderSize(border);
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SliderRow)
};

inline std::array<juce::Rectangle<int>, 2> layoutSliderRowPairBounds(juce::Rectangle<int> area,
                                                                      SliderRow& left,
                                                                      SliderRow& right,
                                                                      int gap = 4)
{
    const auto originalArea = area;
    const int availableW = area.getWidth();
    const int safeGap = juce::jmin(gap, juce::jmax(0, availableW / 8));
    const int halfW = juce::jmax(0, (availableW - safeGap) / 2);

    auto leftBounds = area.removeFromLeft(halfW);
    area.removeFromLeft(safeGap);
    auto rightBounds = area;

    const int leftMin = left.getMinimumWidth();
    const int rightMin = right.getMinimumWidth();
    const bool bothFitEqual = leftBounds.getWidth() >= leftMin && rightBounds.getWidth() >= rightMin;

    if (bothFitEqual || availableW <= leftMin + rightMin + safeGap)
        return { leftBounds, rightBounds };

    const std::vector<ResponsiveStripItem> items {
        { left.getPreferredWidth(),  leftMin,  0, true, ResponsiveStripFallback::none },
        { right.getPreferredWidth(), rightMin, 0, true, ResponsiveStripFallback::none }
    };

    auto result = layoutResponsiveStrip(originalArea, items, safeGap);
    return { result.bounds[0], result.bounds[1] };
}

/**
 * Square button that displays a cached SVG curve icon and cycles on click.
 * 5 shapes: Log, SLog, Lin, SExp, Exp.
 * Uses pre-parsed juce::Drawable — no per-frame path construction.
 */
class CurveButton : public juce::Component
{
public:
    CurveButton() { setMouseCursor(juce::MouseCursor::PointingHandCursor); }

    void setCurveShape(int shape) { if (curveShape != shape) { curveShape = shape; repaint(); } }
    int  getCurveShape() const    { return curveShape; }

    std::function<void()> onClick;

    void mouseDown(const juce::MouseEvent&) override { if (onClick) onClick(); }

    void paint(juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat();
        g.setColour(kSurface);
        g.fillRect(b);
        g.setColour(kBorder);
        g.drawRect(b, 1.0f);

        auto& icon = getIcon(curveShape);
        if (icon)
            icon->drawWithin(g, b.reduced(1.0f),
                             juce::RectanglePlacement::centred, 1.0f);
    }

private:
    int curveShape = 2; // 0=Log, 1=SLog, 2=Lin, 3=SExp, 4=Exp

    static constexpr int kNumShapes = 5;

    static std::unique_ptr<juce::Drawable>& getIcon(int shape)
    {
        static std::unique_ptr<juce::Drawable> icons[kNumShapes];
        static bool inited = false;
        if (!inited)
        {
            // Amber (#FF6F00) curve paths in a 16×16 viewBox
            static const char* svgs[kNumShapes] = {
                // Log (convex — slow start, cubic)
                R"(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 16 16"><path d="M2 14 C9 14 13 10 14 2" stroke="#FF6F00" fill="none" stroke-width="1.5" stroke-linecap="round"/></svg>)",
                // SLog (mild convex — slow start, quadratic)
                R"(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 16 16"><path d="M2 14 C6 14 11 7 14 2" stroke="#FF6F00" fill="none" stroke-width="1.5" stroke-linecap="round"/></svg>)",
                // Lin (straight diagonal)
                R"(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 16 16"><path d="M2 14 L14 2" stroke="#FF6F00" fill="none" stroke-width="1.5" stroke-linecap="round"/></svg>)",
                // SExp (mild concave — mild fast start, quadratic)
                R"(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 16 16"><path d="M2 14 C2 9 7 2 14 2" stroke="#FF6F00" fill="none" stroke-width="1.5" stroke-linecap="round"/></svg>)",
                // Exp (concave — fast start, cubic)
                R"(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 16 16"><path d="M2 14 C2 5 5 2 14 2" stroke="#FF6F00" fill="none" stroke-width="1.5" stroke-linecap="round"/></svg>)",
            };
            for (int i = 0; i < kNumShapes; ++i)
                if (auto xml = juce::parseXML(svgs[i]))
                    icons[i] = juce::Drawable::createFromSVG(*xml);
            inited = true;
        }
        return icons[juce::jlimit(0, kNumShapes - 1, shape)];
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CurveButton)
};

/**
 * Two SliderRows side by side in a 2-column grid.
 * Height = one row height. Each SliderRow gets 50% width minus gap.
 */
class SliderPair : public juce::Component
{
public:
    SliderPair(std::unique_ptr<SliderRow> l, std::unique_ptr<SliderRow> r)
        : left(std::move(l)), right(std::move(r))
    {
        addAndMakeVisible(*left);
        addAndMakeVisible(*right);
    }

    SliderRow& getLeft() { return *left; }
    SliderRow& getRight() { return *right; }

    void resized() override
    {
        auto bounds = layoutSliderRowPairBounds(getLocalBounds(), *left, *right);
        left->setBounds(bounds[0]);
        right->setBounds(bounds[1]);
    }

private:
    std::unique_ptr<SliderRow> left, right;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SliderPair)
};

/**
 * Look-and-feel for the BPM-sync clock button. Two visual states driven by
 * the button's toggleState():
 *   - Off  → grey kSurface background, dim icon
 *   - Sync → orange fill (syncFill member), white icon
 * Used by LFO 1/2/3, Drift 1/2/3 and Delay rows. Owners must declare the
 * LnF instance BEFORE any button using it (so destruction order = button
 * first, LnF second), and must NEVER call setLookAndFeel(nullptr) on the
 * buttons during teardown — JUCE's normal Component destruction is enough.
 */
class ClockButtonLnF : public juce::LookAndFeel_V4
{
public:
    juce::Colour syncFill { 0xffFF6F00 };  // amber-orange, shared sync indicator
    juce::Path icon;

    ClockButtonLnF()
    {
        // 16×16 viewport — classic 10:10 clock pose
        icon.addEllipse(2.5f, 2.5f, 11.0f, 11.0f);
        icon.startNewSubPath(8.0f, 8.0f); icon.lineTo(5.5f, 5.5f);
        icon.startNewSubPath(8.0f, 8.0f); icon.lineTo(10.5f, 5.5f);
    }

    void drawButtonBackground(juce::Graphics& g, juce::Button& b,
                              const juce::Colour&, bool over, bool down) override
    {
        const bool on = b.getToggleState();
        auto base = on ? syncFill : kSurface;
        if (down)      base = base.darker(0.15f);
        else if (over) base = base.brighter(0.10f);
        g.setColour(base);
        g.fillRect(b.getLocalBounds());
        g.setColour(kBorder);
        g.drawRect(b.getLocalBounds(), 1);
    }

    void drawButtonText(juce::Graphics& g, juce::TextButton& b,
                        bool over, bool /*down*/) override
    {
        auto bounds = b.getLocalBounds().toFloat().reduced(3.0f);
        const bool on = b.getToggleState();
        g.setColour(on ? juce::Colours::white : (over ? syncFill : kDim));
        if (b.getButtonText() == "SYNC")
        {
            const float fs = juce::jmax(kUiControlFontMin, bounds.getHeight() * 0.50f);
            if (bounds.getWidth() >= static_cast<float>(measureTextWidth("SYNC", fs) + 8))
            {
                g.setFont(juce::FontOptions(fs, juce::Font::bold));
                g.drawText(b.getButtonText(), bounds, juce::Justification::centred);
                return;
            }
        }

        if (bounds.getWidth() >= 64.0f && b.getButtonText().isNotEmpty())
        {
            auto iconBounds = bounds.removeFromLeft(bounds.getHeight());
            g.strokePath(icon, juce::PathStrokeType(1.35f),
                         icon.getTransformToScaleToFit(iconBounds.reduced(1.0f), true));

            g.setFont(juce::FontOptions(juce::jmax(kUiControlFontMin, bounds.getHeight() * 0.48f),
                                        juce::Font::bold));
            g.drawText(b.getButtonText(), bounds, juce::Justification::centred);
            return;
        }

        g.strokePath(icon, juce::PathStrokeType(1.4f),
                     icon.getTransformToScaleToFit(bounds, true));
    }
};

/**
 * Vertical slider whose value axis is flipped so the MINIMUM sits at the top.
 *
 * Used for the A↔B blend slider: genAlpha runs −2 (toward A) … +2 (toward B), and
 * we want A at the top to align with the Impulse A field above it. Overriding both
 * proportion↔value conversions inverts display AND drag consistently, preserves the
 * parameter skew (delegates to the base impl), and stays transparent to
 * juce::SliderAttachment — which maps value↔parameter and never sees the flip.
 */
class FlippedVerticalSlider : public juce::Slider
{
public:
    double proportionOfLengthToValue(double proportion) override
    {
        return juce::Slider::proportionOfLengthToValue(1.0 - proportion);
    }
    double valueToProportionOfLength(double value) override
    {
        return 1.0 - juce::Slider::valueToProportionOfLength(value);
    }

    // Detents for the A1/B1 anchors (±1), snapped in PROPORTION (on-screen)
    // space rather than value space. The genAlpha range's quadratic skew is
    // steep at ±1, so a value-space snap there spans well under a pixel and is
    // effectively unhittable — whereas at the centre the skew is flat and the
    // value-space 0-snap has a wide, reliable catch. Snapping by screen distance
    // gives a uniform detent at A1/B1, which matters now that the numeric readout
    // is hidden. Only in linear mode (range ≈ [-2,2]); other injection modes use
    // a [0,N] range and must not anchor-snap. Drag only — typed/automation values
    // keep their value-space legalisation (snapGenerationAlpha).
    double snapValue(double attemptedValue, DragMode dragMode) override
    {
        if (dragMode != notDragging && getMinimum() < -0.5)
        {
            constexpr double kAnchorPropSnap = 0.04;  // ~4% of track each side
            const double p = valueToProportionOfLength(attemptedValue);
            for (const double anchor : { -1.0, 1.0 })
                if (std::abs(p - valueToProportionOfLength(anchor)) < kAnchorPropSnap)
                    return anchor;
        }
        return juce::Slider::snapValue(attemptedValue, dragMode);
    }
};

/**
 * Look-and-feel for the A↔B blend slider: a vertical A→B colour-gradient track
 * (A periwinkle at top, B yellow at bottom, pivoting through a near-white neutral)
 * with a round thumb whose fill tracks its position via abBlendColour(). Handles
 * both single-thumb (linear / step-in / combo modes) and TwoValueVertical (layer
 * mode). The drift ghost is drawn separately by PromptPanel::paintOverChildren.
 *
 * Owner must declare this LnF BEFORE the slider that uses it (so the slider is
 * destroyed first) and must NEVER call setLookAndFeel(nullptr) — normal JUCE
 * component teardown is sufficient (see JUCE safety rules).
 */
class AlphaSliderLnF : public juce::LookAndFeel_V4
{
public:
    void drawLinearSlider(juce::Graphics& g, int x, int y, int width, int height,
                          float sliderPos, float minSliderPos, float maxSliderPos,
                          juce::Slider::SliderStyle style, juce::Slider& slider) override
    {
        const bool twoValue = (style == juce::Slider::TwoValueVertical);
        const bool vertical = twoValue || style == juce::Slider::LinearVertical;
        if (! vertical)
        {
            juce::LookAndFeel_V4::drawLinearSlider(g, x, y, width, height, sliderPos,
                                                   minSliderPos, maxSliderPos, style, slider);
            return;
        }

        auto area = juce::Rectangle<int>(x, y, width, height).toFloat();
        // Match the default LookAndFeel_V4 linear-slider weight so this slider
        // doesn't read as disproportionately fat next to the others: a thin
        // track and a thumb sized by getSliderThumbRadius (the same value JUCE
        // uses as the region inset, so the ghost in PromptPanel stays aligned).
        const float trackW = juce::jmin(6.0f, area.getWidth() * 0.25f);
        const float cx = area.getCentreX();

        // Vertical A(top) → B(bottom) gradient with a dark neutral-grey pivot.
        juce::ColourGradient grad(kImpulseA, cx, area.getY(),
                                  kImpulseB, cx, area.getBottom(), false);
        grad.addColour(0.5, kImpulseMid);
        grad.addColour(0.75, kImpulseBWarm);  // warm the grey→gold half off the olive path
        juce::Path track;
        track.startNewSubPath(cx, area.getBottom());
        track.lineTo(cx, area.getY());
        g.setGradientFill(grad);
        g.strokePath(track, { trackW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded });

        const float thumbDia = (float) getSliderThumbRadius(slider);
        auto thumbAt = [&](float posY)
        {
            const float t = juce::jlimit(0.0f, 1.0f,
                                         (posY - area.getY()) / juce::jmax(1.0f, area.getHeight()));
            auto r = juce::Rectangle<float>(thumbDia, thumbDia).withCentre({ cx, posY });
            g.setColour(abBlendColour(t));
            g.fillEllipse(r);
            g.setColour(juce::Colours::white.withAlpha(0.9f));
            g.drawEllipse(r, 1.2f);
        };

        if (twoValue)
        {
            thumbAt(minSliderPos);
            thumbAt(maxSliderPos);
        }
        else
        {
            thumbAt(sliderPos);
        }
    }
};

/**
 * A momentary action button drawn as a small Union Jack — replaces the "EN" text
 * on the prompt-translation control. Clicking it translates the prompts to English
 * in place. Full saturation when enabled, dimmed when disabled; a white ring while
 * pressed and a faint ring on hover for affordance. A simplified-but-recognisable
 * rendering (the red saltire is centred on the white rather than counterchanged,
 * which reads fine at icon size). While a translation is actually running the flag
 * pulses (setPulsing(true)) so the user sees it work; the pulse timer runs only
 * during translation.
 */
class UnionJackButton : public juce::Button,
                        private juce::Timer
{
public:
    UnionJackButton() : juce::Button("EN") {}
    ~UnionJackButton() override { stopTimer(); }  // JUCE: stop timer before teardown

    /** Breathe the flag while a translation runs. Timer is active only while on. */
    void setPulsing(bool shouldPulse)
    {
        if (shouldPulse == pulsing_) return;
        pulsing_ = shouldPulse;
        if (pulsing_) { pulsePhase_ = 0.0f; startTimerHz(30); }
        else            stopTimer();
        repaint();
    }

    void paintButton(juce::Graphics& g, bool shouldDrawButtonAsHighlighted,
                     bool shouldDrawButtonAsDown) override
    {
        auto r = getLocalBounds().toFloat().reduced(1.0f);
        if (r.isEmpty()) return;
        const float corner = 2.0f;
        float a = isEnabled() ? 1.0f : 0.40f;  // full when enabled, dim when disabled
        if (pulsing_)  // breathe between dim and full while translating
            a = 0.40f + 0.60f * (0.5f + 0.5f * std::sin(pulsePhase_));

        juce::Graphics::ScopedSaveState save(g);
        juce::Path clip;
        clip.addRoundedRectangle(r, corner);
        g.reduceClipRegion(clip);

        const auto blue  = juce::Colour(0xff012169).withMultipliedAlpha(a);
        const auto white = juce::Colour(0xffffffff).withMultipliedAlpha(a);
        const auto red   = juce::Colour(0xffC8102E).withMultipliedAlpha(a);

        g.setColour(blue);
        g.fillRect(r);

        const float w = r.getWidth(), h = r.getHeight();
        const float dW = juce::jmax(2.0f, h * 0.24f);  // white saltire thickness
        const float dR = juce::jmax(1.0f, dW * 0.45f); // red saltire thickness

        // Diagonal saltires (corner to corner).
        const juce::Line<float> diag1(r.getTopLeft(), r.getBottomRight());
        const juce::Line<float> diag2(r.getTopRight(), r.getBottomLeft());
        g.setColour(white);
        g.drawLine(diag1, dW);
        g.drawLine(diag2, dW);
        g.setColour(red);
        g.drawLine(diag1, dR);
        g.drawLine(diag2, dR);

        // Upright St George cross over the saltires.
        const float cW = juce::jmax(3.0f, h * 0.36f);  // white cross arm width
        const float cR = juce::jmax(2.0f, cW * 0.5f);  // red cross arm width
        g.setColour(white);
        g.fillRect(juce::Rectangle<float>(r.getCentreX() - cW * 0.5f, r.getY(), cW, h));
        g.fillRect(juce::Rectangle<float>(r.getX(), r.getCentreY() - cW * 0.5f, w, cW));
        g.setColour(red);
        g.fillRect(juce::Rectangle<float>(r.getCentreX() - cR * 0.5f, r.getY(), cR, h));
        g.fillRect(juce::Rectangle<float>(r.getX(), r.getCentreY() - cR * 0.5f, w, cR));

        // Affordance ring: solid white while pressed, faint on hover.
        if (shouldDrawButtonAsDown)
        {
            g.setColour(juce::Colours::white.withAlpha(0.9f));
            g.drawRoundedRectangle(r, corner, 1.2f);
        }
        else if (shouldDrawButtonAsHighlighted)
        {
            g.setColour(juce::Colours::white.withAlpha(0.45f));
            g.drawRoundedRectangle(r, corner, 1.0f);
        }
    }

private:
    void timerCallback() override
    {
        pulsePhase_ += 0.25f;  // ~0.84 s period at 30 Hz
        if (pulsePhase_ > juce::MathConstants<float>::twoPi)
            pulsePhase_ -= juce::MathConstants<float>::twoPi;
        repaint();
    }

    bool  pulsing_    = false;
    float pulsePhase_ = 0.0f;
};
