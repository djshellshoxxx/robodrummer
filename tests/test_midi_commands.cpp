#include "core/JamState.h"
#include <cassert>
int main(){
    using namespace robodrummer;
    JamState s;
    applyMidiCommand(s,MidiCommand::Fill); assert(s.fillRequested);
    applyMidiCommand(s,MidiCommand::NextSection); assert(s.queuedSectionDelta==1);
    for(int i=0;i<20;++i) applyMidiCommand(s,MidiCommand::IntensityUp); assert(s.intensity==1.0f);
    for(int i=0;i<30;++i) applyMidiCommand(s,MidiCommand::IntensityDown); assert(s.intensity==0.0f);
    applyMidiCommand(s,MidiCommand::HalfTime); assert(s.timeScale==0.5);
    applyMidiCommand(s,MidiCommand::DoubleTime); assert(s.timeScale==2.0);
    applyMidiCommand(s,MidiCommand::Stop); assert(s.stopped);
    applyMidiCommand(s,MidiCommand::Resume); assert(!s.stopped);
    applyMidiCommand(s,MidiCommand::ResetListening); assert(s.resetListeningRequested);
}
