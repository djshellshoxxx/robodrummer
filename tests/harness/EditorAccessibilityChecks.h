#pragma once
#include "plugin/PluginEditor.h"
#include <set>

namespace robodrummer::test {

inline void visitUi(juce::Component& component, const std::function<void(juce::Component&)>& visit) {
    visit(component);
    for (auto* child : component.getChildren()) visitUi(*child, visit);
}
inline juce::Component* findUiById(juce::Component& root, const juce::String& id) {
    juce::Component* result = nullptr;
    visitUi(root, [&](juce::Component& child) { if (child.getComponentID() == id) result = &child; });
    return result;
}
inline bool sameUiPixels(const juce::Image& a, const juce::Image& b) {
    if (a.getBounds() != b.getBounds() || a.isNull() || b.isNull()) return false;
    for (int y = 0; y < a.getHeight(); ++y)
        for (int x = 0; x < a.getWidth(); ++x)
            if (a.getPixelAt(x, y) != b.getPixelAt(x, y)) return false;
    return true;
}

template <typename Check>
void runAllTabAccessibilityChecks(RoboDrummerAudioProcessorEditor& editor, Check&& check) {
    const bool ownPeer = editor.getPeer() == nullptr;
    if (ownPeer) editor.addToDesktop(0);
    std::set<juce::String> uniqueParameters;
    std::size_t instances = 0;
    for (int tab = 0; tab < RoboDrummerAudioProcessorEditor::TabCount; ++tab) {
        editor.showTabForTest(tab);
        juce::MessageManager::getInstance()->runDispatchLoopUntil(10);
        auto traverser = editor.createKeyboardFocusTraverser();
        check(traverser != nullptr, "every tab exposes keyboard traversal");
        if (traverser == nullptr) continue;
        const auto order = traverser->getAllComponents(&editor);
        std::set<juce::Component*> walked;
        auto* next = traverser->getDefaultComponent(&editor);
        for (int step = 0; next != nullptr && step < 1000 && walked.insert(next).second; ++step)
            next = traverser->getNextComponent(next);
        check(walked.size() == static_cast<std::size_t>(order.size()), "every tab has a complete finite keyboard walk");
        visitUi(editor, [&](juce::Component& control) {
            if (!control.getProperties().contains("paramID") || !rdui::FocusRing::visibleWithin(&control, &editor)) return;
            ++instances; uniqueParameters.insert(control.getProperties()["paramID"].toString());
            check(control.getTitle().isNotEmpty() && control.getDescription().isNotEmpty(),
                  "every visible parameter widget instance has an accessibility name and description");
            auto* accessibility = control.getAccessibilityHandler();
            check(accessibility != nullptr && accessibility->getTitle().isNotEmpty(),
                  "every visible parameter instance exposes a named accessibility handler");
            if (!control.isEnabled()) return;
            const auto count = std::count(order.begin(), order.end(), &control);
            check(count == 1 && walked.count(&control) == 1,
                  "every actual visible enabled parameter instance is reached exactly once, including duplicates");
        });
    }
    check(instances > uniqueParameters.size() && uniqueParameters.size() >= 100,
          "all nine tabs check duplicate controls independently rather than deduplicating parameter IDs");
    if (ownPeer) editor.removeFromDesktop();
}

template <typename Check>
void runReducedMotionChecks(Check&& check) {
    const auto file = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getNonexistentChildFile("robodrummer-motion", ".xml", false);
    struct Restore {
        juce::File previous, temporary;
        rdui::UiPrefs preferences;
        ~Restore() { rdui::EditorSettings::fileOverride() = previous; rdui::uiPrefs() = preferences; temporary.deleteFile(); }
    } restore{rdui::EditorSettings::fileOverride(), file, rdui::uiPrefs()};
    rdui::EditorSettings::fileOverride() = file;
    auto processor = std::make_unique<RoboDrummerAudioProcessor>();
    processor->prepareToPlay(48000.0, 512);
    const auto write = [&](const juce::String& xml) { check(file.replaceWithText(xml), "write isolated motion preferences"); };
    write("<RoboDrummerSettings tourSeen=\"1\"/>");
    int reads = 0;
    auto editor = std::make_unique<RoboDrummerAudioProcessorEditor>(*processor, [&]() -> std::optional<bool> { ++reads; return true; });
    editor->addToDesktop(0);
    editor->setVisible(true);
    check(reads == 1 && editor->timerRateHz() == 5, "absent saved motion choice follows the injected OS preference");
    auto stored = juce::XmlDocument::parse(file);
    check(stored != nullptr && !stored->hasAttribute("reducedMotion"), "inferred OS motion preference is not persisted as an explicit user override");
    editor->showTabForTest(7);
    rdui::AnalysisPanel* analysis = nullptr;
    visitUi(*editor, [&](juce::Component& component) { if (auto* panel = dynamic_cast<rdui::AnalysisPanel*>(&component)) analysis = panel; });
    check(analysis != nullptr, "analysis panel is present for frozen onset checks");
    if (analysis != nullptr) {
        const auto frozenCursor = analysis->displayedOnsetCursorForTest();
        const auto frozenCount = analysis->displayedOnsetCountForTest();
        const auto frozenImage = analysis->createComponentSnapshot(analysis->onsetPresentationBoundsForTest());
        juce::AudioBuffer<float> buffer(2, 512);
        juce::MidiBuffer midi;
        long long sample = 0;
        const auto play = [&](int blocks) {
            for (int block = 0; block < blocks; ++block) {
                for (int n = 0; n < 512; ++n, ++sample) {
                    const auto phase = sample % 24000;
                    const float guitar = phase < 3000 ? 0.7f * std::exp(-static_cast<float>(phase) / 500.0f) : 0.0f;
                    buffer.setSample(0, n, guitar); buffer.setSample(1, n, guitar);
                }
                midi.clear(); processor->processBlock(buffer, midi);
            }
        };
        play(200); analysis->refresh();
        check(sameUiPixels(frozenImage, analysis->createComponentSnapshot(analysis->onsetPresentationBoundsForTest())),
              "actual rendered onset presentation stays pixel-identical while frozen audio analysis advances");
        check(analysis->displayedOnsetCursorForTest() == frozenCursor && analysis->displayedOnsetCountForTest() == frozenCount,
              "onset marks and beat-grid clock remain frozen while analysis advances");
        AnalysisOnsetMark mark;
        check(processor->popOnsetMarks(&mark, 1) == 0, "frozen presentation still drains the audio onset FIFO");
        auto* update = dynamic_cast<juce::Button*>(findUiById(*editor, "analysisUpdateOnsets"));
        check(update != nullptr && update->isVisible() && update->getTitle().isNotEmpty() && update->getDescription().isNotEmpty(),
              "frozen display has a visible named Update action");
        if (update != nullptr) {
            auto* handler = update->getAccessibilityHandler();
            check(handler != nullptr && handler->getActions().invoke(juce::AccessibilityActionType::press),
                  "Update can be invoked through its actual accessibility action");
            const auto deadline = juce::Time::getMillisecondCounterHiRes() + 2000.0;
            while (analysis->displayedOnsetCursorForTest() != processor->getAnalysisSampleCursor()
                   && juce::Time::getMillisecondCounterHiRes() < deadline)
                juce::MessageManager::getInstance()->runDispatchLoopUntil(10);
            check(analysis->displayedOnsetCursorForTest() == processor->getAnalysisSampleCursor()
                  && analysis->displayedOnsetCountForTest() > 0, "accessible Update captures the current onset marks and beat grid");
            check(!sameUiPixels(frozenImage, analysis->createComponentSnapshot(analysis->onsetPresentationBoundsForTest())),
                  "accessible Update refreshes the actual rendered onset snapshot");
        }
        editor->showTabForTest(0); play(100);
        const auto deadline = juce::Time::getMillisecondCounterHiRes() + 2000.0;
        while (analysis->consumedOnsetCursorForTest() != processor->getAnalysisSampleCursor()
               && juce::Time::getMillisecondCounterHiRes() < deadline)
            juce::MessageManager::getInstance()->runDispatchLoopUntil(10);
        check(analysis->consumedOnsetCursorForTest() == processor->getAnalysisSampleCursor()
              && processor->popOnsetMarks(&mark, 1) == 0, "editor timer drains onset FIFO even when the analysis tab is hidden");
    }
    runAllTabAccessibilityChecks(*editor, check);
    editor.reset();
    for (bool explicitChoice : {false, true}) {
        write(juce::String("<RoboDrummerSettings tourSeen=\"1\" reducedMotion=\"") + (explicitChoice ? "1" : "0") + "\"/>");
        reads = 0;
        editor = std::make_unique<RoboDrummerAudioProcessorEditor>(*processor, [&]() -> std::optional<bool> { ++reads; return !explicitChoice; });
        check(reads == 0 && editor->timerRateHz() == (explicitChoice ? 5 : 20), "explicit saved motion on or off wins without querying the OS");
        editor.reset();
    }
    write("<RoboDrummerSettings tourSeen=\"1\"/>");
    editor = std::make_unique<RoboDrummerAudioProcessorEditor>(*processor, [] { return std::optional<bool>{}; });
    check(editor->timerRateHz() == 20, "unsupported or failed platform preference query falls back to motion off");
    editor.reset(); processor->releaseResources();
}

} // namespace robodrummer::test
