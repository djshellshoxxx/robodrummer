#pragma once
#include "plugin/PluginProcessor.h"
#include "core/MidiBlockPolicy.h"
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <limits>
#include <thread>

// Defined globally to match the narrow friend declaration in the processor.
// Tests exercise the real processBlock path; only sampler producer/reclaimer
// setup and observations cross this seam. No copied callback methods.
struct RoboDrummerProcessorTestAccess {
    static void stopTimer(RoboDrummerAudioProcessor& processor) { processor.stopTimer(); }
    static robodrummer::DrumSamplePlayer* player(RoboDrummerAudioProcessor& processor) { return processor.player_; }
    static robodrummer::DrumSamplePlayer* ownedPlayer(RoboDrummerAudioProcessor& processor) { return processor.playerHandoff_.current(); }
    static void publish(RoboDrummerAudioProcessor& processor, std::unique_ptr<robodrummer::DrumSamplePlayer> player) {
        processor.playerHandoff_.publish(std::move(player));
    }
    static void drainRetired(RoboDrummerAudioProcessor& processor) { processor.playerHandoff_.drainRetired(); }
};

namespace robodrummer::processor_safety_tests {

struct SafetyPlayHead final : juce::AudioPlayHead {
    double ppq{}, bpm{120.0};
    bool playing{true};
    int numerator{4}, denominator{4};
    juce::Optional<PositionInfo> getPosition() const override {
        PositionInfo position;
        position.setBpm(bpm);
        position.setPpqPosition(ppq);
        position.setIsPlaying(playing);
        position.setTimeSignature(TimeSignature{numerator, denominator});
        return position;
    }
};

struct SafetyRig {
    static constexpr int Block = 512;
    static constexpr double SampleRate = 48000.0;
    RoboDrummerAudioProcessor processor;
    SafetyPlayHead playHead;
    juce::AudioBuffer<float> audio{2, Block};

    SafetyRig() {
        RoboDrummerProcessorTestAccess::stopTimer(processor);
        processor.setPlayHead(&playHead);
        processor.prepareToPlay(SampleRate, Block);
        parameter("outputMode", 1.0f);
        parameter("leadershipMode", 0.0f);
        // Warm lazy framework paths before counting just the real callback.
        juce::MidiBuffer midi;
        midi.ensureSize(65536);
        for (int block=0; block<8; ++block) {
            audio.clear(); midi.clear();
            processor.processBlock(audio, midi);
            advance();
        }
    }
    ~SafetyRig() { processor.releaseResources(); }
    void parameter(const char* id, float value) {
        if (auto* parameter = processor.getParameters().getParameter(id))
            parameter->setValueNotifyingHost(parameter->convertTo0to1(value));
    }
    void advance() { playHead.ppq += audio.getNumSamples() / SampleRate * playHead.bpm / 60.0; }
    template <class Audit>
    bool process(juce::MidiBuffer& midi, Audit& audit) {
        audio.clear();
        const bool clean = audit([&] { processor.processBlock(audio, midi); });
        advance();
        return clean;
    }
};

inline int noteOns(const juce::MidiBuffer& midi, int channel=10) {
    int count=0;
    for (const auto metadata : midi) {
        const MidiMessageView view(metadata.data, metadata.numBytes);
        if (view.isNoteOn() && view.getChannel()==channel) ++count;
    }
    return count;
}

inline int exactMessages(const juce::MidiBuffer& midi, const std::uint8_t* bytes, int size) {
    int count=0;
    for (const auto metadata : midi)
        if (metadata.numBytes==size && std::memcmp(metadata.data, bytes, static_cast<std::size_t>(size))==0) ++count;
    return count;
}

inline std::unique_ptr<DrumSamplePlayer> replacementPlayer() {
    auto player=std::make_unique<DrumSamplePlayer>();
    DrumSamplePlayer::Sample sample;
    sample.left.assign(64, 0.1f);
    player->setSample(DrumInstrument::Kick, std::move(sample));
    return player;
}

inline void configureArrangement(SafetyRig& rig, bool middleManual) {
    for (int index=0; index<RoboDrummerAudioProcessor::ArrangementSlotCount; ++index) {
        ArrangementSlot slot;
        slot.enabled=index==0 || index==2 || index==4;
        slot.section.bars=1;
        slot.section.autoAdvance=!(middleManual && index==2);
        slot.section.style=index==0 ? JamStyle::Rock : (index==2 ? JamStyle::Blues : JamStyle::Reggae);
        slot.section.transition=SectionTransition::None;
        rig.processor.setArrangementSlot(index, slot);
    }
    rig.parameter("arrangementEnabled", 1.0f);
}

} // namespace robodrummer::processor_safety_tests

// Check: callable(bool), using the harness's ordinary assertion accounting.
// Audit: generic callable(operation)->bool. It runs operation synchronously and
// returns true only when BOTH allocation and deallocation deltas are zero.
// All buffers/sample construction and inspection below happen outside Audit.
template <class Check, class Audit>
void runProcessorSafetyChecks(Check&& check, Audit&& audit) {
    using namespace robodrummer;
    using namespace robodrummer::processor_safety_tests;
    std::printf("[processor safety: non-owning MIDI and bounded overflow]\n");
    {
        SafetyRig rig;
        std::array<std::uint8_t,32> sysex{};
        sysex.front()=0xf0; sysex.back()=0xf7;
        // Distinct prebuffered host containers must not trade ownership with
        // the processor scratch buffer, allocate, or free during callbacks.
        std::array<juce::MidiBuffer,8> hosts;
        for (auto& host : hosts) host.ensureSize(65536);
        int generated=0;
        for (auto& host : hosts) {
            host.addEvent(sysex.data(), static_cast<int>(sysex.size()), 0);
            rig.playHead.ppq=0.0; // include a generated downbeat in the same block
            check(rig.process(host, audit));
            check(exactMessages(host,sysex.data(),static_cast<int>(sysex.size()))==1);
            generated+=noteOns(host);
        }
        check(generated>0);

        juce::MidiBuffer host;
        host.ensureSize(131072);
        std::array<std::uint8_t,16384> oversized{};
        oversized.front()=0xf0; oversized.back()=0xf7;
        host.addEvent(oversized.data(),static_cast<int>(oversized.size()),0);
        check(rig.process(host,audit));
        check(exactMessages(host,oversized.data(),static_cast<int>(oversized.size()))==0);
        check(host.data.size()<=static_cast<int>(MidiBlockPolicy::OutputCapacityBytes));

        host.clear();
        std::array<std::uint8_t,1024> cumulative{};
        cumulative.front()=0xf0; cumulative.back()=0xf7;
        for (int message=0; message<12; ++message)
            host.addEvent(cumulative.data(),static_cast<int>(cumulative.size()),0);
        const std::uint8_t clock=0xf8;
        host.addEvent(&clock,1,1);
        check(rig.process(host,audit));
        check(exactMessages(host,cumulative.data(),static_cast<int>(cumulative.size()))==4);
        check(exactMessages(host,&clock,1)==1);

        // Learning and clock tracking still run after ordinary output fills.
        int humanizeTarget=-1;
        for (int target=0;target<kLearnTargetCount;++target)
            if (std::strcmp(kLearnTargets[static_cast<std::size_t>(target)].id,"humanize")==0) humanizeTarget=target;
        check(humanizeTarget>=0);
        if (humanizeTarget>=0) rig.processor.getMidiLearn().armTarget(humanizeTarget);
        const std::array<std::uint8_t,3> learnedCc{0xb2,74,117};
        host.clear();
        for (int message=0;message<600;++message)
            host.addEvent(juce::MidiMessage::noteOn(1,60,static_cast<juce::uint8>(100)),0);
        host.addEvent(learnedCc.data(),3,2);
        check(rig.process(host,audit));
        check(rig.processor.getMidiLearn().targetForCc(74)==humanizeTarget);
        check(exactMessages(host,learnedCc.data(),3)==0);
        // Admitted transport keeps its bytes and clock timing. A full output
        // quota instead triggers the explicit stop path tested below.
        // Fill the clock's48-interval window after the earlier single tick and
        // intervening learning block, so the expected steady tempo is exact.
        for (int block=0;block<49;++block) {
            host.clear();
            for (int message=0;message<380;++message)
                host.addEvent(juce::MidiMessage::noteOn(1,60,static_cast<juce::uint8>(100)),0);
            host.addEvent(&clock,1,1);
            check(rig.process(host,audit));
            check(exactMessages(host,&clock,1)==1);
        }
        check(rig.processor.isMidiClockActive());
        check(std::abs(rig.processor.getMidiClockBpm()-234.375)<0.1);

        // Overflowing ordinary pass-through never starves a stop/controller.
        host.clear();
        for (int message=0; message<600; ++message)
            host.addEvent(juce::MidiMessage::noteOn(1,60,static_cast<juce::uint8>(100)),0);
        const std::array<std::uint8_t,3> allNotesOff{0xb0,123,0}, allSoundOff{0xb0,120,0};
        const std::uint8_t stop=0xfc;
        host.addEvent(allNotesOff.data(),3,1);
        host.addEvent(allSoundOff.data(),3,2);
        host.addEvent(&stop,1,3);
        host.addEvent(juce::MidiMessage::noteOn(16,45,1.0f),4);
        check(rig.process(host,audit));
        check(exactMessages(host,allNotesOff.data(),3)==1);
        check(exactMessages(host,allSoundOff.data(),3)==1);
        check(exactMessages(host,&stop,1)==1);
        check(rig.processor.isDrummerStopped());

        host.clear();
        host.addEvent(juce::MidiMessage::noteOn(16,46,1.0f),0);
        check(rig.process(host,audit));
        check(!rig.processor.isDrummerStopped());
        // This is below the input-scan limit but exceeds the24-command array.
        // An ignored late Stop must become a fail-safe, including suppression
        // of the subsequent Resume from that same overloaded callback.
        host.clear();
        for (int command=0;command<25;++command)
            host.addEvent(juce::MidiMessage::noteOn(16,36,1.0f),0);
        host.addEvent(juce::MidiMessage::noteOn(16,45,1.0f),0);
        host.addEvent(juce::MidiMessage::noteOn(16,46,1.0f),0);
        check(rig.process(host,audit));
        check(rig.processor.isDrummerStopped());
        check(host.getNumEvents()==33);
        check(exactMessages(host,&stop,1)==1);

        // The late Stop is beyond the scan boundary. The fail-safe must stop
        // regardless, discard the earlier Resume and emit Stop plus32 CCs.
        host.clear();
        host.addEvent(juce::MidiMessage::noteOn(16,46,1.0f),0);
        for (std::size_t message=0; message<MidiBlockPolicy::MaxInputEvents; ++message)
            host.addEvent(juce::MidiMessage::noteOn(1,60,static_cast<juce::uint8>(100)),0);
        host.addEvent(juce::MidiMessage::noteOn(16,45,1.0f),0);
        check(rig.process(host,audit));
        check(rig.processor.isDrummerStopped());
        check(host.getNumEvents()==33);
        check(exactMessages(host,&stop,1)==1);
        std::array<bool,16> sawNotes{},sawSound{};
        for (const auto metadata : host) {
            const MidiMessageView view(metadata.data,metadata.numBytes);
            if (view.isMidiStop()) continue;
            check(view.isAllNotesOff() || view.isAllSoundOff());
            const int channel=view.getChannel();
            check(channel>=1 && channel<=16);
            if (channel>=1 && channel<=16) {
                if (view.isAllNotesOff()) sawNotes[static_cast<std::size_t>(channel-1)]=true;
                if (view.isAllSoundOff()) sawSound[static_cast<std::size_t>(channel-1)]=true;
            }
        }
        for (std::size_t channel=0;channel<16;++channel) check(sawNotes[channel] && sawSound[channel]);
        host.clear();
        check(rig.process(host,audit));
        check(rig.processor.isDrummerStopped() && noteOns(host)==0);
        host.clear();
        host.addEvent(juce::MidiMessage::noteOn(16,46,1.0f),0);
        check(rig.process(host,audit));
        check(!rig.processor.isDrummerStopped());
    }

    std::printf("[processor safety: deferred zero-block panic and later recovery]\n");
    {
        SafetyRig rig;
        juce::MidiBuffer host;
        host.ensureSize(65536);
        const auto criticalCount=MidiBlockPolicy::CriticalBytes / (3+MidiBlockPolicy::EventOverheadBytes) + 1;
        const auto overloadCritical = [&] {
            host.clear();
            for (std::size_t message=0;message<criticalCount;++message)
                host.addEvent(juce::MidiMessage::noteOff(1,60),0);
        };
        rig.audio.setSize(2,0);
        overloadCritical();
        check(rig.process(host,audit));
        check(rig.processor.isDrummerStopped());
        check(host.getNumEvents()==0); // no invalid event offsets in an empty block
        rig.audio.setSize(2,SafetyRig::Block);
        host.clear(); check(rig.process(host,audit));
        const std::uint8_t stop=0xfc;
        check(host.getNumEvents()==33 && rig.processor.isDrummerStopped());
        check(exactMessages(host,&stop,1)==1);
        host.clear(); check(rig.process(host,audit));
        check(host.getNumEvents()==0 && rig.processor.isDrummerStopped());
        rig.playHead.playing=false;
        host.clear(); check(rig.process(host,audit));
        check(rig.processor.isDrummerStopped());
        rig.playHead.playing=true;
        host.clear(); check(rig.process(host,audit));
        check(!rig.processor.isDrummerStopped());
        for (const std::uint8_t resumeStatus : {0xfa,0xfb}) {
            overloadCritical();
            check(rig.process(host,audit));
            check(rig.processor.isDrummerStopped() && host.getNumEvents()==33);
            check(exactMessages(host,&stop,1)==1);
            host.clear(); host.addEvent(&resumeStatus,1,0);
            check(rig.process(host,audit));
            check(!rig.processor.isDrummerStopped());
            check(exactMessages(host,&resumeStatus,1)==1);
        }
        // A transport message must never be silently dropped after ordinary
        // output saturates; stop downstream clocks and voices deterministically.
        for (const std::uint8_t transport : {0xf8,0xfa,0xfb}) {
            host.clear();
            for (int message=0;message<600;++message)
                host.addEvent(juce::MidiMessage::noteOn(1,60,static_cast<juce::uint8>(100)),0);
            host.addEvent(&transport,1,1);
            check(rig.process(host,audit));
            check(rig.processor.isDrummerStopped() && host.getNumEvents()==33);
            check(exactMessages(host,&stop,1)==1);
            host.clear(); host.addEvent(juce::MidiMessage::noteOn(16,46,1.0f),0);
            check(rig.process(host,audit));
            check(!rig.processor.isDrummerStopped());
        }
    }

    std::printf("[processor safety: bounded actual host seeks]\n");
    for (const bool manual : {false,true}) {
        SafetyRig rig;
        configureArrangement(rig,manual);
        juce::MidiBuffer host;
        host.ensureSize(65536);
        check(rig.process(host,audit));
        check(rig.processor.getCurrentArrangementSection()==0);
        rig.playHead.ppq=1.0e9;
        const auto start=std::chrono::steady_clock::now();
        host.clear(); check(rig.process(host,audit));
        host.clear(); check(rig.process(host,audit));
        const auto elapsed=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
        std::printf("  two distant-seek callbacks: %.6fs (manual=%d)\n",elapsed,manual);
        check(elapsed<0.25); // catches the measured proportional seek loop
        check(rig.processor.getCurrentArrangementSection()==(manual ? 2 : 4));
        check(rig.processor.getJamStyle()==(manual ? JamStyle::Blues : JamStyle::Reggae));
        rig.playHead.ppq=0.0;
        host.clear(); check(rig.process(host,audit));
        host.clear(); check(rig.process(host,audit));
        check(rig.processor.getCurrentArrangementSection()==(manual ? 2 : 4));
        // This crosses the old Riff Lock int-guard boundary as well as the
        // arrangement loop. It must execute only bounded decay/seek work.
        rig.playHead.ppq=1.0e12;
        const auto extremeStart=std::chrono::steady_clock::now();
        host.clear(); check(rig.process(host,audit));
        host.clear(); check(rig.process(host,audit));
        const auto extremeElapsed=std::chrono::duration<double>(std::chrono::steady_clock::now()-extremeStart).count();
        std::printf("  two extreme-seek callbacks: %.6fs (manual=%d)\n",extremeElapsed,manual);
        check(extremeElapsed<0.25);
        check(rig.processor.getCurrentArrangementSection()==(manual ? 2 : 4));
        if (manual) {
            rig.processor.requestCommand(MidiCommand::NextSection);
            host.clear(); check(rig.process(host,audit));
            check(rig.processor.getCurrentArrangementSection()==4);
        }
    }

    std::printf("[processor safety: sample-domain host timeline bounds]\n");
    for (const bool manual : {false,true}) {
        SafetyRig rig;
        configureArrangement(rig,manual);
        rig.parameter("outputMode",2.0f); // inspect real internal audio and generated MIDI
        auto player=replacementPlayer();
        DrumSamplePlayer::Sample crash;
        crash.left.assign(64,0.1f);
        player->setSample(DrumInstrument::Crash,std::move(crash));
        RoboDrummerProcessorTestAccess::publish(rig.processor,std::move(player));
        juce::MidiBuffer host;
        host.ensureSize(65536);
        const auto processAndInspect=[&] {
            host.clear();
            check(rig.process(host,audit));
            bool finiteAudio=true;
            for (int channel=0;channel<rig.audio.getNumChannels();++channel)
                for (int sample=0;sample<rig.audio.getNumSamples();++sample)
                    finiteAudio=finiteAudio && std::isfinite(rig.audio.getSample(channel,sample));
            check(finiteAudio);
            bool validMidi=true;
            int previousOffset=-1;
            for (const auto metadata : host) {
                validMidi=validMidi && metadata.numBytes>0
                    && metadata.samplePosition>=previousOffset
                    && metadata.samplePosition>=0 && metadata.samplePosition<SafetyRig::Block;
                previousOffset=metadata.samplePosition;
            }
            check(validMidi);
            check(host.data.size()<=static_cast<int>(MidiBlockPolicy::OutputCapacityBytes));
            check(!rig.processor.isDrummerStopped());
            check(rig.processor.getDrummerBarNumber()>=1);
            check(rig.processor.getDrummerBeatInBar()>=1 && rig.processor.getDrummerBeatInBar()<=4);
        };
        const auto checkRecovery=[&](double position) {
            rig.playHead.ppq=position;
            // A fresh event proves recovery renders music rather than merely
            // producing finite silent buffers after a saturated/invalid seek.
            rig.processor.requestCommand(MidiCommand::Crash);
            processAndInspect();
            check(rig.processor.getDrummerBarNumber()==1);
            check(rig.processor.getDrummerBeatInBar()==1);
            check(noteOns(host)>0);
            bool audible=false;
            for (int channel=0;channel<rig.audio.getNumChannels();++channel)
                for (int sample=0;sample<rig.audio.getNumSamples();++sample)
                    audible=audible || std::abs(rig.audio.getSample(channel,sample))>1.0e-7f;
            check(audible);
            check(rig.processor.getCurrentArrangementSection()==(manual ? 2 : 4));
        };
        processAndInspect();
        long long saturatedBar=0;
        for (const double position : {1.0e15,std::numeric_limits<double>::max()}) {
            rig.playHead.ppq=position;
            const auto start=std::chrono::steady_clock::now();
            processAndInspect();
            processAndInspect();
            const auto elapsed=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
            std::printf("  two sample-bound seek callbacks: %.6fs (manual=%d, ppq=%.3g)\n",elapsed,manual,position);
            check(elapsed<0.25); // gross proportional-work regression, not a realtime deadline
            const auto bar=rig.processor.getDrummerBarNumber();
            check(bar>1);
            if (saturatedBar==0) saturatedBar=bar;
            check(bar==saturatedBar); // both finite inputs saturate at the same sample ceiling
            check(rig.processor.getCurrentArrangementSection()==(manual ? 2 : 4));
            check(rig.processor.getJamStyle()==(manual ? JamStyle::Blues : JamStyle::Reggae));
            for (const double invalid : {std::numeric_limits<double>::quiet_NaN(),
                                         std::numeric_limits<double>::infinity(),
                                         -std::numeric_limits<double>::infinity()}) {
                rig.playHead.ppq=invalid;
                processAndInspect();
                check(rig.processor.getDrummerBarNumber()==saturatedBar); // nonfinite PPQ is ignored
            }
            checkRecovery(0.0);
        }
        // Finite negative PPQ clamps to zero, including inputs far below the
        // representable sample domain. Each begins on the saturated timeline.
        for (const double negative : {-1.0,-std::numeric_limits<double>::max()}) {
            rig.playHead.ppq=1.0e15;
            processAndInspect();
            check(rig.processor.getDrummerBarNumber()==saturatedBar);
            checkRecovery(negative);
            checkRecovery(0.0);
        }
        if (manual) {
            rig.processor.requestCommand(MidiCommand::NextSection);
            processAndInspect();
            check(rig.processor.getCurrentArrangementSection()==4);
        }
    }

    std::printf("[processor safety: real sampler ownership under pressure]\n");
    {
        SafetyRig rig;
        juce::MidiBuffer host;
        host.ensureSize(65536);
        DrumSamplePlayer* lastAdopted=nullptr;
        DrumSamplePlayer* newest=nullptr;
        for (int publication=0;publication<6;++publication) {
            auto replacement=replacementPlayer();
            newest=replacement.get();
            RoboDrummerProcessorTestAccess::publish(rig.processor,std::move(replacement));
            host.clear(); check(rig.process(host,audit));
            if (publication<4) lastAdopted=newest;
            check(RoboDrummerProcessorTestAccess::player(rig.processor)==lastAdopted);
            check(RoboDrummerProcessorTestAccess::ownedPlayer(rig.processor)==lastAdopted);
        }
        // A fifth/sixth publication cannot destroy or lose the current sampler
        // when all four retired slots are occupied. The newest waits owned.
        check(lastAdopted!=newest);
        RoboDrummerProcessorTestAccess::drainRetired(rig.processor);
        host.clear(); check(rig.process(host,audit));
        check(RoboDrummerProcessorTestAccess::player(rig.processor)==newest);
        check(RoboDrummerProcessorTestAccess::ownedPlayer(rig.processor)==newest);
        RoboDrummerProcessorTestAccess::drainRetired(rig.processor);
        RoboDrummerProcessorTestAccess::publish(rig.processor,replacementPlayer());
        rig.processor.releaseResources();
        rig.processor.prepareToPlay(44100.0,SafetyRig::Block);
        check(RoboDrummerProcessorTestAccess::player(rig.processor)!=nullptr);
        check(RoboDrummerProcessorTestAccess::player(rig.processor)==RoboDrummerProcessorTestAccess::ownedPlayer(rig.processor));
        host.clear(); check(rig.process(host,audit));
    }

    std::printf("[processor safety: coherent actual mapping state capture]\n");
    {
        SafetyRig source,restored;
        std::atomic<bool> stop{false};
        constexpr int note=70;
        const int fill=static_cast<int>(MidiCommand::Fill),crash=static_cast<int>(MidiCommand::Crash);
        std::thread writer([&] {
            while (!stop.load(std::memory_order_acquire)) {
                source.processor.getMidiLearn().bindNote(note,fill,1);
                source.processor.getMidiLearn().bindNote(note,crash,2);
            }
        });
        int observedRows=0,mixed=0;
        for (int capture=0;capture<200;++capture) {
            const auto state=source.processor.createMidiMapState();
            restored.processor.applyMidiMapState(state);
            for (const auto& row : state) {
                if (!row.hasType("Note") || static_cast<int>(row.getProperty("note"))!=note) continue;
                ++observedRows;
                const int command=row.getProperty("command"),channel=row.getProperty("channel");
                if (!((command==fill && channel==1) || (command==crash && channel==2))) ++mixed;
                const auto roundTrip=restored.processor.getMidiLearn().noteBindingForNote(note);
                if (roundTrip.command!=command || roundTrip.channel!=channel) ++mixed;
            }
        }
        stop.store(true,std::memory_order_release);
        writer.join();
        check(observedRows>0);
        check(mixed==0);
    }
}
