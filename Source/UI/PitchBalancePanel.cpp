#include "PitchBalancePanel.h"
#include "PianoRollStyle.h"

namespace
{
juce::String u8(const char* text) { return juce::String::fromUTF8(text); }
constexpr double toleranceCents = 5.0;

void noFocus(juce::Component& c)
{
    c.setWantsKeyboardFocus(false);
    c.setMouseClickGrabsKeyboardFocus(false);
}
} // namespace

PitchBalancePanel::PitchBalancePanel()
{
    setWantsKeyboardFocus(false);
    setMouseClickGrabsKeyboardFocus(false);
    for (auto* s : { &centerSlider, &driftSlider })
    {
        s->setSliderStyle(juce::Slider::LinearHorizontal);
        s->setTextBoxStyle(juce::Slider::TextBoxRight, false, 44, 18);
        s->setRange(0.0, 100.0, 1.0);
        s->setTextValueSuffix("%");
        s->setTextBoxIsEditable(false);
        noFocus(*s);
        s->onDragStart = [this] { if (onGestureBegin) onGestureBegin(); };
        s->onValueChange = [this]
        {
            balance.center = centerSlider.getValue() / 100.0;
            balance.drift = driftSlider.getValue() / 100.0;
            notify();
        };
        addAndMakeVisible(*s);
    }

    const char* const names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    keyBox.addItem(u8("반음 (조 없음)"), 1);
    for (int i = 0; i < 12; ++i)
        keyBox.addItem(names[i], i + 2);
    noFocus(keyBox);
    keyBox.onChange = [this]
    {
        if (syncing)
            return;
        if (onGestureBegin)
            onGestureBegin();
        balance.scale.root = keyBox.getSelectedId() - 2;
        minorToggle.setEnabled(balance.scale.root >= 0);
        notify();
    };
    addAndMakeVisible(keyBox);

    minorToggle.setButtonText(u8("단조"));
    toleranceToggle.setButtonText(u8("5센트 이내는 그대로"));
    for (auto* t : { &minorToggle, &toleranceToggle })
    {
        noFocus(*t);
        t->onClick = [this]
        {
            if (onGestureBegin)
                onGestureBegin();
            balance.scale.minor = minorToggle.getToggleState();
            balance.toleranceCents = toleranceToggle.getToggleState() ? toleranceCents : 0.0;
            notify();
        };
        addAndMakeVisible(*t);
    }

    closeButton.setButtonText(u8("닫기"));
    noFocus(closeButton);
    closeButton.onClick = [this] { if (onClosed) onClosed(); };
    addAndMakeVisible(closeButton);
    setBalance(balance);
}

void PitchBalancePanel::setBalance(const PitchBalance& newBalance)
{
    const juce::ScopedValueSetter<bool> guard(syncing, true);
    balance = newBalance;
    centerSlider.setValue(balance.center * 100.0, juce::dontSendNotification);
    driftSlider.setValue(balance.drift * 100.0, juce::dontSendNotification);
    keyBox.setSelectedId(balance.scale.root + 2, juce::dontSendNotification);
    minorToggle.setToggleState(balance.scale.minor, juce::dontSendNotification);
    minorToggle.setEnabled(balance.scale.root >= 0);
    toleranceToggle.setToggleState(balance.toleranceCents > 0.0, juce::dontSendNotification);
}

void PitchBalancePanel::notify()
{
    if (! syncing && onChanged)
        onChanged(balance);
}

void PitchBalancePanel::resized()
{
    auto r = getLocalBounds().reduced(8);
    auto title = r.removeFromTop(20);
    closeButton.setBounds(title.removeFromRight(48));
    r.removeFromTop(4);
    for (auto* s : { &centerSlider, &driftSlider })
    {
        auto line = r.removeFromTop(24);
        line.removeFromLeft(70);
        s->setBounds(line);
    }
    r.removeFromTop(4);
    auto keys = r.removeFromTop(24);
    keyBox.setBounds(keys.removeFromLeft(120));
    keys.removeFromLeft(6);
    minorToggle.setBounds(keys.removeFromLeft(60));
    toleranceToggle.setBounds(keys);
}

void PitchBalancePanel::paint(juce::Graphics& g)
{
    g.setColour(juce::Colour(0xf0202228));
    g.fillRoundedRectangle(getLocalBounds().toFloat(), 6.0f);
    g.setColour(rollstyle::voicedFill.withAlpha(0.7f));
    g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(0.5f), 6.0f, 1.0f);
    auto r = getLocalBounds().reduced(8);
    g.setColour(juce::Colours::white.withAlpha(0.9f));
    g.setFont(13.0f);
    g.drawText(u8("음정 균형 (선택한 블록, 없으면 전체)"), r.removeFromTop(20), juce::Justification::centredLeft, true);
    r.removeFromTop(4);
    g.setFont(11.5f);
    g.setColour(juce::Colours::white.withAlpha(0.65f));
    g.drawText(u8("중심 음정"), r.removeFromTop(24).withWidth(70), juce::Justification::centredLeft, false);
    g.drawText(u8("음정 흐름"), r.removeFromTop(24).withWidth(70), juce::Justification::centredLeft, false);
}
