#pragma once
#include <JuceHeader.h>
#include "../PluginProcessor.h"

/**
 * Show a context menu for MIDI CC Learn on a named APVTS parameter.
 * Call from a right-click handler on any learnable control.
 */
inline void showMidiLearnMenu(T5ynthProcessor& proc,
                               const juce::String& paramId,
                               juce::Point<int> screenPos)
{
    const int boundCc = proc.findBoundCc(paramId);
    const bool learningThis = proc.isMidiLearnActive()
                           && proc.getMidiLearnParamId() == paramId;

    juce::PopupMenu menu;
    menu.addItem(1, learningThis ? "Cancel MIDI Learn" : "MIDI Learn");

    if (boundCc >= 0)
    {
        menu.addSeparator();
        menu.addItem(2, "Clear CC " + juce::String(boundCc) + " binding");
        menu.addItem(3, "Bound to CC " + juce::String(boundCc), false, false);
    }

    menu.showMenuAsync(
        juce::PopupMenu::Options().withTargetScreenArea(
            juce::Rectangle<int>(screenPos, screenPos)),
        [procPtr = &proc, paramId](int result)
        {
            if (result == 1)
            {
                if (procPtr->isMidiLearnActive()
                    && procPtr->getMidiLearnParamId() == paramId)
                    procPtr->cancelMidiLearn();
                else
                    procPtr->startMidiLearn(paramId);
            }
            else if (result == 2)
            {
                const int cc = procPtr->findBoundCc(paramId);
                if (cc >= 0) procPtr->clearCcMapping(cc);
            }
        });
}
