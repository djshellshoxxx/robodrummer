// Production state replacement against concurrent/reentrant Apply and Undo.
#pragma once
#include "PresetRegressionTests.h"
#include <atomic>
#include <functional>
#include <thread>

namespace state_transaction_checks {
enum class Notification { Begin, Value, End };

class NotificationHook final : public juce::AudioProcessorParameter::Listener {
public:
    NotificationHook(Notification when, std::function<void()> action) : when_(when), action_(std::move(action)) {}
    void parameterValueChanged(int, float) override { fire(Notification::Value); }
    void parameterGestureChanged(int, bool starting) override {
        if (starting) begins.fetch_add(1); else ends.fetch_add(1);
        fire(starting ? Notification::Begin : Notification::End);
    }
    std::atomic<bool> fired{};
    std::atomic<int> begins{}, ends{};
private:
    void fire(Notification notification) {
        if (notification == when_ && !fired.exchange(true)) action_();
    }
    Notification when_;
    std::function<void()> action_;
};

inline juce::MemoryBlock replacementState(RoboDrummerAudioProcessor& processor) {
    preset_regressions::setPlain(processor, "inputGain", 18.0f);
    juce::MemoryBlock replacement;
    processor.getStateInformation(replacement);
    preset_regressions::setPlain(processor, "inputGain", 0.0f);
    return replacement;
}

inline bool equalsState(RoboDrummerAudioProcessor& processor, const juce::MemoryBlock& expected) {
    juce::MemoryBlock actual;
    processor.getStateInformation(actual);
    return actual == expected;
}

template <class Check>
void reentrantReplacement(Check check) {
    for (const auto stage : {Notification::Begin, Notification::Value, Notification::End}) {
        for (bool undo : {false, true}) {
            RoboDrummerAudioProcessor processor;
            processor.prepareToPlay(48000.0, 512);
            const auto replacement = replacementState(processor);
            preset_regressions::measure(processor);
            const auto proposal = processor.getAutoSetupResult().proposal;
            check(proposal.inputGainDb.apply, "fixture proposes an input-gain change");
            if (undo) check(processor.applyAutoSetup(proposal), "prepare a fresh Undo transaction");
            auto* parameter = processor.getParameters().getParameter("inputGain");
            NotificationHook hook(stage, [&] {
                processor.setStateInformation(replacement.getData(), static_cast<int>(replacement.getSize()));
            });
            parameter->addListener(&hook);
            const bool applied = undo ? processor.undoAutoSetup() : processor.applyAutoSetup(proposal);
            parameter->removeListener(&hook);
            check(hook.fired.load(), "state restore reenters the real host notification boundary");
            check(!applied, "interrupted old-generation Apply/Undo reports no completed transaction");
            check(equalsState(processor, replacement), "nested host-state replacement wins over every stale field write");
            check(!processor.canUndoAutoSetup(), "interrupted transaction cannot publish a stale Undo");
            check(hook.begins.load() == hook.ends.load(), "every opened host gesture closes after nested invalidation");
            check(!processor.applyAutoSetup(proposal), "retained proposal stays rejected after nested replacement");
            processor.releaseResources();
        }
    }
}

template <class Check>
void concurrentReplacement(Check check) {
    RoboDrummerAudioProcessor processor;
    processor.prepareToPlay(48000.0, 512);
    const auto replacement = replacementState(processor);
    preset_regressions::measure(processor);
    const auto proposal = processor.getAutoSetupResult().proposal;
    check(proposal.inputGainDb.apply, "concurrent fixture proposes input gain");
    const auto originalGeneration = processor.getAutoSetupGeneration();
    juce::WaitableEvent requested, completed;
    std::thread host;
    auto* parameter = processor.getParameters().getParameter("inputGain");
    NotificationHook hook(Notification::Begin, [&] {
        host = std::thread([&] {
            requested.signal();
            processor.setStateInformation(replacement.getData(), static_cast<int>(replacement.getSize()));
            completed.signal();
        });
        check(requested.wait(5000), "host restore request overlaps the Apply transaction");
        // Never join a restore waiting for this transaction's gate. A bounded
        // probe verifies it cannot complete inside Apply's host notification.
        check(!completed.wait(25), "concurrent host restore waits for Apply to release its transaction gate");
        check(processor.getAutoSetupGeneration() == originalGeneration, "blocked restore cannot invalidate a half-applied transaction");
    });
    parameter->addListener(&hook);
    const bool applied = processor.applyAutoSetup(proposal);
    parameter->removeListener(&hook);
    if (host.joinable()) host.join(); // only after Apply has returned and released its gate
    check(hook.fired.load() && applied, "real Apply completed before queued host replacement");
    check(completed.wait(5000), "queued state restore completes after the transaction");
    check(equalsState(processor, replacement), "queued host replacement wins as a complete serialized transaction");
    check(!processor.canUndoAutoSetup() && !processor.applyAutoSetup(proposal), "replacement invalidates the old proposal and Undo");
    processor.releaseResources();
}

template <class Check>
void nestedPresetReplacement(Check check) {
    for (bool userFile : {false, true}) {
        RoboDrummerAudioProcessor processor;
        processor.prepareToPlay(48000.0, 512);
        preset_regressions::setPlain(processor, "kit", 2.0f);
        preset_regressions::setPlain(processor, "roomSize", 0.91f);
        juce::MemoryBlock replacement;
        processor.getStateInformation(replacement);
        preset_regressions::setPlain(processor, "kit", 0.0f);
        preset_regressions::setPlain(processor, "roomSize", 0.0f);
        juce::TemporaryFile temporary(".rdpreset");
        juce::XmlElement xml("RoboDrummerPreset");
        xml.setAttribute("type", "Kit"); xml.setAttribute("version", 1);
        preset_regressions::parameter(xml, "kit", "0");
        preset_regressions::parameter(xml, "roomSize", "0.4");
        check(xml.writeTo(temporary.getFile()), "write real user preset transaction fixture");
        auto* parameter = processor.getParameters().getParameter("kit");
        NotificationHook hook(Notification::Begin, [&] {
            processor.setStateInformation(replacement.getData(), static_cast<int>(replacement.getSize()));
        });
        parameter->addListener(&hook);
        const bool loaded = userFile
            ? robodrummer::PresetManager::loadUserFile(processor, robodrummer::PresetType::Kit, temporary.getFile())
            : robodrummer::PresetManager::load(processor, robodrummer::PresetType::Kit, {"Studio Kit", true});
        parameter->removeListener(&hook);
        check(hook.fired.load() && !loaded, "factory/user preset stops after nested host-state replacement");
        check(equalsState(processor, replacement), "nested state wins over later factory/user preset fields");
        check(hook.begins.load() == hook.ends.load(), "preset notification gestures stay balanced after reentrancy");
        processor.releaseResources();
    }
}

template <class Check>
void run(Check check) {
    reentrantReplacement(check);
    concurrentReplacement(check);
    nestedPresetReplacement(check);
}
} // namespace state_transaction_checks
