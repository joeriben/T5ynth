#pragma once
#include <JuceHeader.h>
#include <vector>

/**
 * The knobs an AUTHORED LRO instrument gives the player.
 *
 * THE KNOBS COME FROM THE LIBRARY, not from the author. A library entry declares
 * what is playable about its body, as one line that is both the value the code
 * uses and its own description — `kbowl = 0.50 ; bowl[0.0..1.0]: which measured
 * bowl the mode series is`. Every such line the author KEEPS in the body it
 * writes becomes a knob here: the name, the range and the words under it are the
 * library's, the starting value is the author's, and the wiring to the Csound
 * channel is the host's (backend/lco_write.py, `read_controls`/`wire_controls`).
 *
 * So the panel shows the library's vocabulary and can show nothing else, and a
 * slider that moves nothing cannot exist — the author never writes the wire. A
 * body that keeps no library line gets no knobs, not a grid of plausible ones
 * (docs/plans/PLAN_lro_param_panel.md §1).
 *
 * Only ONE parser exists, and it is the Python one. This type is the decoded
 * shape as it arrives over the wire and as it is stored in a preset — a second
 * C++ parser of the same contract would be a predicate with two homes, and this
 * project has paid for that twice already.
 */
struct LroControls
{
    /** One knob: a slider under the player's hand, and a Csound channel. */
    struct Knob
    {
        juce::String channel;   // "lroP1a" — the Csound channel this writes
        juce::String paramId;   // "lro_p1a" — the APVTS parameter that holds it
        int part = 1;           // 1..3: which column it belongs to
        juce::String slot;      // "a".."d": which row of that column
        juce::String name;      // what the player reads — the library's own word
        juce::String gloss;     // the library's one line on what it does
        float value = 0.5f;     // where the author put it when it wrote the body
    };

    /** A column: one library entry the body draws on, under its library name. */
    struct Part
    {
        int number = 1;         // 1..3
        juce::String name;      // "singing bowl" — the entry, as the library names it
    };

    std::vector<Part> parts;
    std::vector<Knob> knobs;
    /** Lines the backend threw away, with the reason — a name the library does
     *  not declare, a fourth column, a fifth knob in a column. Carried so the
     *  decision travels with the sound (it goes into the preset and into the
     *  authoring response), NOT drawn: nothing in the GUI reads this yet, so a
     *  dropped line is currently a knob that is simply not there. Per-knob notes
     *  (a clamped starting value, a bracket that disagrees with the library's)
     *  stay in the backend's response and do not travel at all. */
    juce::StringArray refused;

    /** Which LAYERS the body has — the subset of 1..3 whose `kvolN` it reads.
     *
     *  NOT the parts above, and never to be counted with the same number. A part
     *  here is a library entry a parameter line survived from; a layer is one of
     *  the scaffold's three mix variables, and the author scales EVERY part it
     *  builds by its own `kvolN` — each end of an `a > b` and each side of an
     *  `a through b` included, because a fader per part belongs to the player and
     *  the relation does not get to take it away (BJ 2026-08-06). The two still
     *  cross: two layers built from one entry are one column. Taken from the body
     *  by `read_controls`, because the `kvolN` names are the scaffold's and fixed
     *  — where "which entry is layer 2" is not decidable at all. */
    std::vector<int> layers;

    bool isEmpty() const { return knobs.empty(); }

    /** Knobs of one part, in the order their lines stand in the body. */
    std::vector<Knob> knobsOf(int partNumber) const
    {
        std::vector<Knob> out;
        for (const auto& k : knobs)
            if (k.part == partNumber)
                out.push_back(k);
        return out;
    }

    /** Decode the backend's `controls` object (docs/IPC_PROTOCOL.md). Anything
     *  missing or malformed yields an EMPTY set rather than a partial guess:
     *  the panel showing no knobs is honest, showing a knob that is not wired
     *  is the one outcome this whole path exists to prevent. */
    static LroControls fromVar(const juce::var& v)
    {
        LroControls out;
        if (auto* obj = v.getDynamicObject())
        {
            if (auto* arr = obj->getProperty("parts").getArray())
                for (auto& e : *arr)
                {
                    Part p;
                    p.number = static_cast<int>(e.getProperty("n", juce::var(0)));
                    p.name   = e.getProperty("name", juce::var()).toString();
                    if (p.number >= 1 && p.number <= 3)
                        out.parts.push_back(p);
                }
            if (auto* arr = obj->getProperty("params").getArray())
                for (auto& e : *arr)
                {
                    Knob k;
                    k.channel = e.getProperty("ch", juce::var()).toString();
                    k.part    = static_cast<int>(e.getProperty("part", juce::var(0)));
                    k.slot    = e.getProperty("slot", juce::var()).toString();
                    k.name    = e.getProperty("name", juce::var()).toString();
                    k.gloss   = e.getProperty("gloss", juce::var()).toString();
                    k.value   = juce::jlimit(0.0f, 1.0f,
                                    static_cast<float>(static_cast<double>(
                                        e.getProperty("value", juce::var(0.5)))));
                    // The APVTS id is the channel in this project's parameter
                    // spelling — the one place the two namings meet, so a knob
                    // can never be wired to a different parameter than the
                    // channel it names.
                    if (k.channel.startsWith("lroP") && k.channel.length() == 6)
                        k.paramId = "lro_p" + k.channel.substring(4).toLowerCase();
                    if (k.paramId.isNotEmpty() && k.part >= 1 && k.part <= 3
                        && k.name.isNotEmpty())
                        out.knobs.push_back(k);
                }
            if (auto* arr = obj->getProperty("refused").getArray())
                for (auto& e : *arr)
                    out.refused.add(e.toString());
            if (auto* arr = obj->getProperty("layers").getArray())
                for (auto& e : *arr)
                {
                    const int n = static_cast<int>(e);
                    if (n >= 1 && n <= 3)
                        out.layers.push_back(n);
                }
        }
        // A part nobody has a knob in is a heading over nothing.
        for (int i = static_cast<int>(out.parts.size()) - 1; i >= 0; --i)
            if (out.knobsOf(out.parts[static_cast<size_t>(i)].number).empty())
                out.parts.erase(out.parts.begin() + i);
        return out;
    }

    /** Back to the same JSON the backend sends, so a preset can carry it. */
    juce::var toVar() const
    {
        auto* obj = new juce::DynamicObject();
        juce::Array<juce::var> partArr, knobArr, refusedArr;
        for (const auto& p : parts)
        {
            auto* po = new juce::DynamicObject();
            po->setProperty("n", p.number);
            po->setProperty("name", p.name);
            partArr.add(juce::var(po));
        }
        for (const auto& k : knobs)
        {
            auto* ko = new juce::DynamicObject();
            ko->setProperty("ch", k.channel);
            ko->setProperty("part", k.part);
            ko->setProperty("slot", k.slot);
            ko->setProperty("name", k.name);
            ko->setProperty("gloss", k.gloss);
            ko->setProperty("value", k.value);
            knobArr.add(juce::var(ko));
        }
        for (const auto& r : refused)
            refusedArr.add(r);
        juce::Array<juce::var> layerArr;
        for (int n : layers)
            layerArr.add(n);
        obj->setProperty("parts", partArr);
        obj->setProperty("params", knobArr);
        obj->setProperty("refused", refusedArr);
        obj->setProperty("layers", layerArr);
        return juce::var(obj);
    }
};
